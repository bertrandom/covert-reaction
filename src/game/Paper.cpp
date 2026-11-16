#include "game/Paper.h"

#include <cstdlib>

#include "formats/TextWindow.h"

namespace reaction {

void drawLine(Image& img, int x1, int y1, int x2, int y2, std::uint8_t c) {
    const int dx = std::abs(x2 - x1), dy = -std::abs(y2 - y1);
    const int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        img.set(x1, y1, c);
        if (x1 == x2 && y1 == y2) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void fillRectXY(Image& img, int x1, int y1, int x2, int y2, std::uint8_t c) {
    fillRect(img, x1, y1, x2 - x1 + 1, y2 - y1 + 1, c);
}

void drawTextCentered(Image& img, const Font& font, std::string_view s, int x, int y, std::uint8_t color) {
    font.draw(img, x - font.textWidth(s) / 2, y, s, {0, color, 0});
}

void drawTitleTab(Image& img, const FaceSprites& sprites, int x, int y, std::uint8_t color) {
    fillRect(img, x + 10, y, 0x8C, 8, color);
    if (y) drawLine(img, x + 10, y - 1, x + 0x93, y - 1, 5);
    drawLine(img, 0, y + 8, x, y + 8, 8);
    drawLine(img, x + 0x9E, y + 8, 319, y + 8, 8);
    sprites.draw(img, 0x6D, x, y);
    sprites.draw(img, 0x6E, x + 0x93, y);
    recolorRect(img, x, y, 0xA0, 8, 13, color);
}

void drawPageFrame(Image& img, const Font& font, const FaceSprites& sprites, std::string_view title,
                   std::uint8_t color) {
    fillRect(img, 0, 0, 320, 10, 6);
    fillRect(img, 0, 10, 320, 190, color);
    drawTitleTab(img, sprites, 0xA0, 2, color);
    drawLine(img, 0, 0, 0, 199, 6);
    drawLine(img, 319, 0, 319, 199, 6);
    fillRect(img, 8, 14, 304, 186, 15);
    drawLine(img, 10, 13, 312, 13, 7);
    drawLine(img, 312, 13, 312, 199, 8);
    const int w = font.textWidth(title);
    fillRect(img, 0xEF - w / 2, 3, w + 2, 9, 7);
    font.draw(img, 0xF0 - w / 2, 4, title, {0, 0, 0});
}

void drawCardPanel(Image& img, int x, int y) {
    fillRect(img, x, y, 0x130, 200 - y, 15);
    drawLine(img, x + 1, y, x + 0x130, y, 7);
    drawLine(img, x + 0x130, y, x + 0x130, 199, 8);
    drawLine(img, x, y + 1, x, 199, 7);
}

void wrapTextBuffer(std::string& t, int width) {
    int pos = 0;
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '^') {
            t[i] = ' ';
            if (i + 1 < t.size()) t[i + 1] = '\n';
            else t.push_back('\n');  // the original writes over the terminating NUL
            pos = -1;
        }
        if (t[i] == '\n') pos = -1;
        ++pos;
        if (pos > width && t[i] == ' ' && (i + 1 >= t.size() || t[i + 1] != ' ')) {
            t[i] = '\n';
            pos = 0;
        }
    }
    if (pos > 0) t += '\n';
}

}  // namespace reaction
