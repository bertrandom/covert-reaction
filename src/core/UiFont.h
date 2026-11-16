// Text for the screens Covert Reaction draws itself in true colour (core/Canvas.h):
// the system's own UI font, read with stb_truetype (third_party/stb), drawn
// anti-aliased from a glyph cache. Sizes are in canvas pixels (the caller
// scales its points by the display's pixel ratio).
//
// The font is found at run time, at fixed places (no search of the disk):
//  - macOS: San Francisco (/System/Library/Fonts/SFNS.ttf, its default
//    instance), else Helvetica Neue, Helvetica, Arial
//  - Windows: Segoe UI (segoeui.ttf; seguisb.ttf or segoeuib.ttf for bold), else Arial
//  - Linux: fontconfig's sans-serif (fc-match), else DejaVu Sans, Noto Sans,
//    Liberation Sans, Cantarell or Ubuntu at their usual paths
// A family without a bold file gets a synthetic bold. If none can be read, the
// built-in 8x8 font (core/BuiltinFont.h), scaled up. COVERT_UI_FONT (and
// COVERT_UI_FONT_BOLD) name a font file instead; COVERT_UI_FONT=builtin forces
// the 8x8 one. No SDL here.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/Canvas.h"

namespace reaction {

class UiFont {
public:
    ~UiFont();
    UiFont(const UiFont&) = delete;
    UiFont& operator=(const UiFont&) = delete;

    // Face `index` of `file` (a .ttf, .otf or .ttc), or the face whose style
    // is `style` ("Bold") if that's set. Null if it can't be read.
    static std::unique_ptr<UiFont> load(const std::filesystem::path& file, int index = 0, std::string_view style = {});
    // The built-in 8x8 font, scaled to whole multiples.
    static std::unique_ptr<UiFont> builtin();

    const std::filesystem::path& file() const { return file_; }
    bool isBuiltin() const { return impl_ == nullptr; }
    // Synthetic bold: glyphs widened by `amount` x the size (0: off).
    void setEmbolden(float amount) { embolden_ = amount; cache_.clear(); }
    bool emboldened() const { return embolden_ > 0; }

    // At size `px` (the em, as CSS's font-size): above and below the baseline.
    float ascent(float px) const;
    float descent(float px) const;   // (positive)
    float lineHeight(float px) const { return ascent(px) + descent(px); }
    // The height of the capitals (for centring a line in a box).
    float capHeight(float px) const;

    float measure(std::string_view utf8, float px) const;
    // Draws `utf8` with its baseline at y; returns the x after it.
    float draw(Canvas& canvas, float x, float baseline, std::string_view utf8, float px, Color c) const;

private:
    struct Impl;
    struct Glyph {
        int w = 0, h = 0, xoff = 0, yoff = 0;
        std::vector<std::uint8_t> alpha;
    };
    UiFont() = default;
    const Glyph& glyph(int index, float px, float shift) const;
    int glyphIndex(char32_t cp) const;
    float advance(int index, float px) const;
    float kern(int a, int b, float px) const;
    int builtinScale(float px) const;

    std::unique_ptr<Impl> impl_;   // null: the built-in font
    std::filesystem::path file_;
    float embolden_ = 0;
    mutable std::unordered_map<std::uint64_t, Glyph> cache_;
};

// The system's UI font, regular and bold, found once (see above). Never null.
struct UiFonts {
    std::unique_ptr<UiFont> regular, bold;
    std::string describe() const;   // "/System/Library/Fonts/SFNS.ttf (bold: synthetic)"
};
const UiFonts& systemUiFonts();

// Text layout helpers.
// `text` broken into lines no wider than `maxWidth` (at spaces; a word too long
// for a line, e.g. a path, is broken where it must). Explicit '\n' too.
std::vector<std::string> wrapText(const UiFont& font, std::string_view text, float px, float maxWidth);
// `text` cut to `maxWidth` with "…" at the end (right), or in the middle
// (middle: for paths, both the start and the file's own name stay).
std::string elideRight(const UiFont& font, std::string_view text, float px, float maxWidth);
std::string elideMiddle(const UiFont& font, std::string_view text, float px, float maxWidth);

}  // namespace reaction
