// The game library's paper-style screens and small drawing helpers
// (covert-decomp GAMELIB/PERSON.C draw_page_frame, draw_title_tab,
// draw_card_panel, draw_text_centered, wrap_text_buffer; MPSLIB/GFX.C draw_line,
// fill_rect_xy), drawing into an Image with the port's font 1.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "core/Image.h"
#include "formats/Font.h"
#include "game/Face.h"

namespace reaction {

// gfx_line: a line, both end points included.
void drawLine(Image& img, int x1, int y1, int x2, int y2, std::uint8_t c);
// fill_rect_xy: the inclusive rectangle (x1, y1)-(x2, y2).
void fillRectXY(Image& img, int x1, int y1, int x2, int y2, std::uint8_t c);

// draw_text_centered: transparent text centred on x.
void drawTextCentered(Image& img, const Font& font, std::string_view s, int x, int y, std::uint8_t color);

// draw_title_tab and draw_page_frame: the folder page with a title tab; leaves
// the page white from (8, 14).
void drawTitleTab(Image& img, const FaceSprites& sprites, int x, int y, std::uint8_t color);
void drawPageFrame(Image& img, const Font& font, const FaceSprites& sprites, std::string_view title,
                   std::uint8_t color);

// draw_card_panel: a white card from (x, y) to the bottom of the screen.
void drawCardPanel(Image& img, int x, int y);

// wrap_text_buffer: breaks the text into lines of about `width` characters by
// turning spaces into '\n' ('^' forces a break), ending it with '\n'.
void wrapTextBuffer(std::string& text, int width);

}  // namespace reaction
