#include "formats/TextWindow.h"

#include <algorithm>

namespace reaction {

namespace {

constexpr int kLineHeight = 8;  // text_window and draw_wrapped_text step y by 8

void hline(Image& img, int x0, int x1, int y, std::uint8_t c) {
    if (x0 > x1) std::swap(x0, x1);
    for (int x = x0; x <= x1; ++x) img.set(x, y, c);
}

void vline(Image& img, int x, int y0, int y1, std::uint8_t c) {
    if (y0 > y1) std::swap(y0, y1);
    for (int y = y0; y <= y1; ++y) img.set(x, y, c);
}

}  // namespace

void fillRect(Image& img, int x, int y, int w, int h, std::uint8_t c) {
    for (int yy = y; yy < y + h; ++yy) hline(img, x, x + w - 1, yy, c);
}

// draw_rect_outline: four draw_line calls, end points included.
void drawRectOutline(Image& img, int x, int y, int w, int h, std::uint8_t c) {
    hline(img, x, x + w, y, c);
    hline(img, x, x + w, y + h, c);
    vline(img, x + w, y, y + h, c);
    vline(img, x, y, y + h, c);
}

// draw_bevel_box: fill_rect_xy(x, y, x + w, y + h) (inclusive), then an
// outline in colour ^ 8, where 0 and 13 become 7 (so the menu colour 5 gets a
// grey frame).
void drawBevelBox(Image& img, int x, int y, int w, int h, std::uint8_t c) {
    fillRect(img, x, y, w + 1, h + 1, c);
    std::uint8_t edge = c ^ 8;
    if (edge == 0 || edge == 13) edge = 7;
    drawRectOutline(img, x, y, w, h, edge);
}

// recolor_rect (driver slot 3): within w x h pixels, 'from' becomes 'to'.
void recolorRect(Image& img, int x, int y, int w, int h, std::uint8_t from, std::uint8_t to) {
    for (int yy = std::max(y, 0); yy < std::min(y + h, img.height()); ++yy)
        for (int xx = std::max(x, 0); xx < std::min(x + w, img.width()); ++xx)
            if (img.at(xx, yy) == from) img.set(xx, yy, to);
}

TextWindowLayout layoutTextWindow(const Font& font, std::string_view text, int x, int y) {
    TextWindowLayout lay;
    lay.lineStart.push_back(0);
    int w = 0, n = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\n') {
            lay.maxWidth = std::max(lay.maxWidth, w);
            w = 0;
            ++lay.lines;
            lay.lineStart.push_back(static_cast<int>(i + 1));
        } else {
            // An item: a space at the start of a line (w == 0, so also after
            // zero-width characters, as in the original).
            if (w == 0 && c == ' ') {
                lay.hotkeys += i + 1 < text.size() ? text[i + 1] : '\0';
                if (lay.firstItem == -1) lay.firstItem = lay.lines;
                ++n;
            }
            w += font.charWidth(static_cast<std::uint8_t>(c));
        }
    }
    lay.lines = std::clamp(lay.lines, 0, (192 - y) / kLineHeight);
    lay.boxX2 = x + lay.maxWidth + 8;
    lay.boxY2 = y + lay.lines * kLineHeight + 6;
    if (text.empty() || text.back() != '\n') --n;
    lay.items = n;
    return lay;
}

TextWindowColors textWindowColors(int y, const TextWindowOptions& opt) {
    TextWindowColors col;
    if ((y & 1) && opt.fillColor == 15) col = {0, 7, 8, 15};
    return col;
}

TextWindowLayout drawTextWindow(Image& img, const Font& font, std::string_view text, int x, int y,
                                const TextWindowOptions& opt) {
    const TextWindowLayout lay = layoutTextWindow(font, text, x, y);
    const TextWindowColors col = textWindowColors(y, opt);
    // The port's fill colour: the background of opaque text ("OK").
    std::uint8_t portFill = opt.fillColor;
    if (y & 1) {
        fillRect(img, x + 1, y + 1, lay.boxX2 - x, lay.boxY2 - y, opt.fillColor);
    } else {
        portFill = col.box;
        drawBevelBox(img, x, y, lay.boxX2 - x, lay.boxY2 - y, col.box);
    }
    if (opt.showOk) {
        font.draw(img, lay.boxX2 - 17, lay.boxY2 - 8, "OK", {1, 11, portFill});
        drawRectOutline(img, lay.boxX2 - 20, lay.boxY2 - 10, 20, 10, 11);
    }

    // n: the item number of the current line, -1 before the first item.
    int n = !text.empty() && text[0] == ' ' ? 0 : -1;
    for (int i = 0; i < lay.lines; ++i) {
        const auto from = static_cast<std::size_t>(lay.lineStart[static_cast<std::size_t>(i)]);
        const auto to = static_cast<std::size_t>(lay.lineStart[static_cast<std::size_t>(i) + 1]) - 1;
        std::string line(text.substr(from, to - from));
        if (n >= 0 && n < 32 && (opt.disabled >> n & 1) && !line.empty()) line[0] = '^';
        const std::uint8_t c = n < 0 ? col.text : (n == opt.selected ? col.selected : col.item);
        font.draw(img, x + 5, i * kLineHeight + y + 5, line, {0, c, portFill});
        if (to + 1 < text.size() && text[to + 1] == ' ') ++n;
    }

    if (lay.firstItem != -1 && opt.selected >= 0 && opt.selected < lay.items)
        highlightItem(img, lay, x, y, opt.selected, col.box, kSelectionColor);
    return lay;
}

void highlightItem(Image& img, const TextWindowLayout& lay, int x, int y, int item, std::uint8_t from,
                   std::uint8_t to) {
    recolorRect(img, x + 3, (lay.firstItem + item) * kLineHeight + y + 4, lay.maxWidth + 3,
                kLineHeight + 1, from, to);
}

// A literal port of draw_wrapped_text, including its quirks: the width counts
// every character since the last break (so a line can end one character past
// the limit before backing up to the last space), and a line without a space
// breaks at the previous break point.
int drawWrappedText(Image& img, const Font& font, std::string_view text, int widthChars, int x, int y,
                    std::uint8_t color) {
    std::string buf(text);  // the original temporarily NUL-terminates text_buf
    const auto cstr = [&](std::size_t from) {
        std::string_view v(buf.data() + from, buf.size() - from);
        return v.substr(0, v.find('\0'));
    };
    std::size_t start = 0, lastsp = 0;
    int w = 0;
    for (std::size_t i = 0; i < cstr(0).size(); ++i) {
        w += font.charWidth(static_cast<std::uint8_t>(buf[i]));
        if (buf[i] == ' ' || buf[i] == '\n') lastsp = i;
        if (buf[i] == '\n' || w > widthChars * 8) {
            const char c = buf[lastsp];
            buf[lastsp] = '\0';
            font.draw(img, x, y, cstr(start), {0, color, 0});
            buf[lastsp] = c;
            y += kLineHeight;
            w = 0;
            start = lastsp + 1;
            i = lastsp;
            if (y >= 0xC0) break;
        }
    }
    if (w > 0 && y < 0xC0) {
        font.draw(img, x, y, cstr(std::min(start, buf.size())), {0, color, 0});
        y += kLineHeight;
    }
    return y;
}

}  // namespace reaction
