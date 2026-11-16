#include "formats/Font.h"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace redux {

namespace {

std::uint16_t le16(std::span<const std::uint8_t> b, std::size_t at) {
    return static_cast<std::uint16_t>(b[at] | b[at + 1] << 8);
}

}  // namespace

// ---- FontSet ---------------------------------------------------------------

FontSet FontSet::parse(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 2) throw std::runtime_error("FONTS.CV: too short");
    const int n = le16(bytes, 0);
    if (bytes.size() < 2 + 2 * static_cast<std::size_t>(n))
        throw std::runtime_error("FONTS.CV: truncated font table");
    auto file = std::make_shared<const Bytes>(bytes.begin(), bytes.end());
    FontSet set;
    for (int k = 1; k <= n; ++k) {
        const std::size_t off = le16(bytes, 2 * static_cast<std::size_t>(k));
        if (off < 8 || off > bytes.size())
            throw std::runtime_error("FONTS.CV: bad offset for font " + std::to_string(k));
        set.fonts_.push_back(Font(file, off));
    }
    return set;
}

const Font& FontSet::font(int n) const {
    if (!has(n)) throw std::out_of_range("no font " + std::to_string(n));
    return fonts_[static_cast<std::size_t>(n - 1)];
}

// ---- Font ------------------------------------------------------------------

Font::Font(std::shared_ptr<const Bytes> file, std::size_t dataOffset)
    : file_(std::move(file)), data_(static_cast<std::ptrdiff_t>(dataOffset)) {
    first_ = byteAt(data_ - 8);
    last_ = byteAt(data_ - 7);
    bytesPerRow_ = byteAt(data_ - 6);
    fixed_ = byteAt(data_ - 5);
    height_ = byteAt(data_ - 4);
    spacing_ = byteAt(data_ - 3);
    extra_ = byteAt(data_ - 2);
    // text_setup turns bytes per row into a shift (bpr - 1) and reads a 16-bit
    // word per glyph row, so only 1 and 2 make sense.
    if (last_ < first_ || bytesPerRow_ < 1 || bytesPerRow_ > 2)
        throw std::runtime_error("FONTS.CV: bad font header");
}

std::uint8_t Font::byteAt(std::ptrdiff_t offset) const {
    if (offset < 0 || offset >= static_cast<std::ptrdiff_t>(file_->size())) return 0;
    return (*file_)[static_cast<std::size_t>(offset)];
}

// The width table ends right before the 8-byte header: text_setup's
// width_table = data - 9 - (last - first), indexed by (ch - first) & FFh.
std::uint8_t Font::tableWidth(std::uint8_t ch) const {
    const int idx = (ch - first_) & 0xFF;
    return byteAt(data_ - 8 - count() + idx);
}

int Font::glyphWidth(std::uint8_t ch) const {
    if (ch < first_ || ch > last_) return 0;
    return fixed_ ? fixed_ : tableWidth(ch);
}

// mgfx_char_width: 0 for ch >= 80h (a colour code) or outside [first, last];
// otherwise the fixed width or the table width, plus the spacing.
int Font::charWidth(std::uint8_t ch) const {
    if (ch & 0x80 || ch < first_ || ch > last_) return 0;
    return glyphWidth(ch) + spacing_;
}

int Font::textWidth(std::string_view s) const {
    int w = 0;
    for (char c : s) w += charWidth(static_cast<std::uint8_t>(c));
    return w;
}

bool Font::pixel(std::uint8_t ch, int col, int row) const {
    if (ch < first_ || ch > last_ || col < 0 || col >= 8 * bytesPerRow_ || row < 0 ||
        row >= height())
        return false;
    const std::ptrdiff_t at = data_ + static_cast<std::ptrdiff_t>(row) * count() * bytesPerRow_ +
                              (ch - first_) * bytesPerRow_ + col / 8;
    return byteAt(at) >> (7 - col % 8) & 1;
}

// draw_glyphs (MGRAPHIC.ASM), after text_setup:
//  * rows per glyph: height, plus the extra rows if textbg bit 0 is set.
//  * cell width ("glyph_width"): fixed width + spacing, or width[ch] + spacing,
//    in 8-bit arithmetic; x advances by it after every character.
//  * each glyph row is read as a big-endian 16-bit word at
//    data + row * (count << shift) + ((ch - first) << shift), shift = bpr - 1,
//    and its bits are shifted out from the top, one per column. With 1 byte per
//    row the low byte is the *next* character's row, so a cell wider than 8
//    shows that glyph's left edge; FONTS.CV's cells are at most 8 wide.
//  * opaque (textbg == 1): the whole cell, set bits in the text colour, clear
//    bits in the port's fill colour; the spacing column and the extra rows
//    become background.
//  * transparent: set bits only, over cell width - spacing columns, except
//    for the last character of the string, which gets the full cell.
//  * a byte with bit 7 set is not drawn: it sets the colour to byte & 7Fh;
//    the final colour is written back to the port's textcolor.
//  * characters outside [first, last] are not checked: they index the width
//    table and the glyph data out of range (byteAt reads the file image).
// The driver's clipping (clip_text_left/right/vert) drops whole rows, and whole
// characters for fixed-width fonts only; proportional text isn't clipped
// horizontally at all (it would wrap to the next scan line). Clipping pixels
// to the image/clip rectangle gives the same result where the original's is
// well defined.
TextResult Font::draw(Image& img, int x, int y, std::string_view s, const TextAttr& attr,
                      const std::optional<ClipRect>& clip) const {
    const int rows = height_ + ((attr.textbg & 1) ? extra_ : 0);
    const bool opaque = attr.textbg == 1;
    const int shift = bytesPerRow_ - 1;
    const std::ptrdiff_t rowStride = static_cast<std::ptrdiff_t>(count()) << shift;
    const int cellFixed = fixed_ ? (fixed_ + spacing_) & 0xFF : 0;

    int cx0 = 0, cy0 = 0, cx1 = img.width() - 1, cy1 = img.height() - 1;
    if (clip) {
        cx0 = std::max(cx0, clip->x0);
        cy0 = std::max(cy0, clip->y0);
        cx1 = std::min(cx1, clip->x1);
        cy1 = std::min(cy1, clip->y1);
    }
    auto put = [&](int px, int py, std::uint8_t c) {
        if (px >= cx0 && px <= cx1 && py >= cy0 && py <= cy1) img.set(px, py, c);
    };

    std::uint8_t fg = attr.color;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto ch = static_cast<std::uint8_t>(s[i]);
        if (ch == 0) break;
        if (ch & 0x80) {
            fg = ch & 0x7F;
            continue;
        }
        const bool lastChar = i + 1 >= s.size() || s[i + 1] == 0;
        const int idx = (ch - first_) & 0xFF;
        const int cell = cellFixed ? cellFixed : (tableWidth(ch) + spacing_) & 0xFF;
        const int drawn = opaque || lastChar ? cell : (cell - spacing_) & 0xFF;
        const std::ptrdiff_t src = data_ + (static_cast<std::ptrdiff_t>(idx) << shift);
        for (int r = 0; r < rows; ++r) {
            const std::ptrdiff_t at = src + r * rowStride;
            unsigned bits = static_cast<unsigned>(byteAt(at) << 8 | byteAt(at + 1));
            for (int c = 0; c < drawn; ++c, bits <<= 1) {
                const bool set = bits & 0x8000;
                if (set) put(x + c, y + r, fg);
                else if (opaque) put(x + c, y + r, attr.background);
            }
        }
        x += cell;
    }
    return {x, fg};
}

}  // namespace redux
