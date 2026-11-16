// The original's text windows and menus (TEXTWIN.C draw_text_window, UI.C
// draw_bevel_box) and wrapped text (PERSON.C draw_wrapped_text), as pure
// functions over an Image. Input handling (text_window's key loop) is not here.
//
// A window's text is lines ended by '\n'. A line starting with a space is a menu
// item; its second character is the item's hotkey. Lines are 8 pixels apart
// whatever the font; text starts at (x + 5, y + 5). Text after the last '\n'
// is not shown (draw_text_window only counts terminated lines).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/Image.h"
#include "formats/Font.h"

namespace redux {

struct TextWindowLayout {
    std::vector<int> lineStart;   // offset of each line in the text (line_start)
    int lines = 0;                // shown lines (nlines, at most (192 - y) / 8)
    int maxWidth = 0;             // widest terminated line, in pixels (max_width)
    std::string hotkeys;          // second character of each item line (hotkeys)
    int firstItem = -1;           // line of the first item, -1 if none (first_item)
    int items = 0;                // nitems (one less if the text has no final '\n')
    int boxX2 = 0, boxY2 = 0;     // bottom-right corner of the box (box_x2, box_y2)
};

struct TextWindowColors {
    std::uint8_t text = 15, item = 7, selected = 7, box = 5;
};

struct TextWindowOptions {
    int selected = -1;            // highlighted item (text_window's default_sel), -1: none
    unsigned disabled = 0;        // bit n: item n is drawn with '^' for its leading space
    bool showOk = false;          // the "OK" button in the bottom-right corner (show_ok)
    bool noSelect = false;        // menu_flag (Menu): items aren't highlighted or chosen
    // Odd y: no bevel, the box is filled with this (the port's fillcolor);
    // white (15) switches to dark text. Even y always draws the colour-5 bevel box.
    std::uint8_t fillColor = 5;
};

// Highlight colour of the selected item: text_window recolours the box colour
// to 14 under the item (drv_recolor_rect).
inline constexpr std::uint8_t kSelectionColor = 14;

// The line/box geometry of a window at (x, y) (first half of draw_text_window).
TextWindowLayout layoutTextWindow(const Font& font, std::string_view text, int x, int y);

// The colours draw_text_window picks for a window at (x, y).
TextWindowColors textWindowColors(int y, const TextWindowOptions& opt);

// Draws the box, the lines and the selection highlight (draw_text_window plus
// the initial highlight of text_window; the anim_active variant isn't covered).
TextWindowLayout drawTextWindow(Image& img, const Font& font, std::string_view text, int x, int y,
                                const TextWindowOptions& opt = {});

// Highlights (or, with from/to swapped, un-highlights) a menu item the way
// text_window does: recolor_rect(x + 3, (firstItem + item) * 8 + y + 4,
// maxWidth + 3, 9, from, to).
void highlightItem(Image& img, const TextWindowLayout& lay, int x, int y, int item,
                   std::uint8_t from, std::uint8_t to);

// Box primitives with the original's coordinates.
void fillRect(Image& img, int x, int y, int w, int h, std::uint8_t c);  // gfx_fill_rect: w x h
void drawRectOutline(Image& img, int x, int y, int w, int h, std::uint8_t c);  // (w+1) x (h+1)
void drawBevelBox(Image& img, int x, int y, int w, int h, std::uint8_t c);
void recolorRect(Image& img, int x, int y, int w, int h, std::uint8_t from, std::uint8_t to);

// draw_wrapped_text: transparent text wrapped at the last space before the line
// would exceed width * 8 pixels, or at '\n'; lines 8 pixels apart, stopping at
// y = 192. Returns the y below the last line.
int drawWrappedText(Image& img, const Font& font, std::string_view text, int widthChars, int x, int y,
                    std::uint8_t color);

}  // namespace redux
