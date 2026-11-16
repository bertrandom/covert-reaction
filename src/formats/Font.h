// The game's bitmap fonts (FONTS.CV) and the graphics driver's text renderer.
//
// COVERT.EXE loads FONTS.CV right behind the graphics driver's data, at
// DATA:630h (FONT_TABLE in MGRAPHIC.ASM), and the driver's text entry points
// (draw_text, char_width, font_height: slots 20, 4 and 10) read it from there.
//
// File layout (all words little-endian):
//   +0          word  n: number of fonts (3)
//   +2          word  offset[n]: offset of font k's glyph data (k = 1..n) from
//                     the start of the file. Font numbers are 1-based: the
//                     driver draws nothing for font 0 or font > n.
//   For each font, the bytes *before* its glyph data are:
//     data - 8 - count  byte  width[count]: pixel width of each character
//     data - 8          byte  first character code
//     data - 7          byte  last character code (count = last - first + 1)
//     data - 6          byte  bytes per glyph row (1: glyphs up to 8 pixels wide)
//     data - 5          byte  fixed width (0: proportional, use the width table)
//     data - 4          byte  height: rows of the glyph proper
//     data - 3          byte  spacing: blank columns added after every character
//     data - 2          byte  extra rows below the glyph (line gap; drawn only
//                           when the port's textbg has bit 0 set)
//     data - 1          byte  unused (0)
//   data: the glyph bitmaps, row-interleaved: row r of character c is at
//     data + r * (count * bpr) + (c - first) * bpr, most significant bit = the
//     leftmost pixel. There are height + extra rows.
//
// FONTS.CV holds three proportional fonts, all 1 byte per row and 1 column of
// spacing: 1 = 8 rows + 1 (' '..'~', the game's normal text), 2 = 7 + 1
// (' '..DEL, small), 3 = 8 + 1 (' '..'z', the code-breaking puzzle's letters).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core/GameData.h"
#include "core/Image.h"

namespace redux {

// The text fields of the original's RastPort (MPS.H) that the driver reads.
struct TextAttr {
    // Port +0Ah "textbg". 1: opaque (background drawn, glyph cells include the
    // extra rows). 0: transparent (draw_text_transparent). The driver tests bit
    // 0 for the extra rows and == 1 for opaque, so e.g. 3 means transparent
    // with the extra rows (which are blank in FONTS.CV).
    int textbg = 1;
    std::uint8_t color = 15;      // port +0Ch "textcolor"
    std::uint8_t background = 4;  // port +0Eh "fillcolor": the opaque background
};

// A clip rectangle in image coordinates, inclusive ([x0, x1] x [y0, y1]).
struct ClipRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct TextResult {
    int x = 0;                    // x after the text (draw_text's return value)
    std::uint8_t color = 0;       // final colour: the driver writes it back to
                                  // the port's textcolor (colour codes, below)
};

class Font {
public:
    int first() const { return first_; }
    int last() const { return last_; }
    int count() const { return last_ - first_ + 1; }
    int glyphHeight() const { return height_; }
    int extraRows() const { return extra_; }
    int spacing() const { return spacing_; }
    int fixedWidth() const { return fixed_; }
    bool proportional() const { return fixed_ == 0; }
    // font_height (slot 10): height + extra rows: the line height.
    int height() const { return height_ + extra_; }

    // The glyph's own width (from the width table, or the fixed width), without
    // the spacing; 0 if `ch` is outside the font.
    int glyphWidth(std::uint8_t ch) const;
    // char_width (slot 4): advance of a character, width + spacing; 0 for codes
    // outside [first, last], including the colour codes >= 80h.
    int charWidth(std::uint8_t ch) const;
    // text_width (PERSON.C): sum of charWidth over the bytes of `s`.
    int textWidth(std::string_view s) const;

    // Glyph pixel (column, row) of a character, as the font data has it.
    bool pixel(std::uint8_t ch, int col, int row) const;

    // Draws `s` with its top-left corner at (x, y) the way the driver's
    // draw_glyphs does (see Font.cpp for the details): a byte >= 80h switches
    // the colour to (byte & 7Fh), a 0 byte ends the string. Pixels are clipped
    // to the image and to `clip` if given.
    TextResult draw(Image& img, int x, int y, std::string_view s, const TextAttr& attr,
                    const std::optional<ClipRect>& clip = std::nullopt) const;

private:
    friend class FontSet;
    Font(std::shared_ptr<const Bytes> file, std::size_t dataOffset);

    // A byte of the loaded file image; 0 outside it. The driver indexes memory
    // without range checks (characters outside the font, the bits of the next
    // glyph), so reads are relative to the whole file as the game loads it.
    std::uint8_t byteAt(std::ptrdiff_t offset) const;
    std::uint8_t tableWidth(std::uint8_t ch) const;

    std::shared_ptr<const Bytes> file_;
    std::ptrdiff_t data_ = 0;     // offset of the glyph data in the file
    int first_ = 0, last_ = 0, bytesPerRow_ = 1, fixed_ = 0, height_ = 0, spacing_ = 0,
        extra_ = 0;
};

class FontSet {
public:
    static FontSet parse(std::span<const std::uint8_t> bytes);
    static FontSet load(const GameData& data) { return parse(data.read("fonts.cv")); }

    // Number of fonts; valid font numbers are 1..size().
    int size() const { return static_cast<int>(fonts_.size()); }
    bool has(int n) const { return n >= 1 && n <= size(); }
    // Font n (1-based, as in RastPort.font). Throws std::out_of_range.
    const Font& font(int n) const;

private:
    std::vector<Font> fonts_;
};

}  // namespace redux
