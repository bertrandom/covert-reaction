// A plain 8x8 PC bitmap font compiled into the program, for the screens shown
// before the game files are found (their fonts, FONTS.CV, are game files too).
// The glyphs are Daniel Hepper's public-domain font8x8 (font8x8_basic.h): the
// printable ASCII set, plus a few glyphs of our own (below).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "core/Image.h"

namespace reaction::builtin_font {

constexpr int kCharWidth = 8, kCharHeight = 8;

// Extra glyphs outside printable ASCII.
constexpr char kEllipsis = '\x7f';   // "..." in one cell
constexpr char kArrowUp = '\x01';
constexpr char kArrowDown = '\x02';

// Draws `s` with its top-left corner at (x, y), transparent, one 8-pixel cell
// per byte; bytes without a glyph (non-ASCII) show as '?'. Clipped to the image.
// Returns the x after the text.
int drawText(Image& img, int x, int y, std::string_view s, std::uint8_t color);

constexpr int textWidth(std::string_view s) { return static_cast<int>(s.size()) * kCharWidth; }

// `s` cut to at most `maxChars` cells: the end replaced by the ellipsis glyph
// (fitRight), or the start (fitLeft, for paths: the folder's own name stays).
std::string fitRight(std::string_view s, int maxChars);
std::string fitLeft(std::string_view s, int maxChars);

// UTF-8 text (a file name) as one cell per character: each non-ASCII
// character becomes a single '?'.
std::string fromUtf8(std::string_view s);

}  // namespace reaction::builtin_font
