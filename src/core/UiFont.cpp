#include "core/UiFont.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

#include "core/BuiltinFont.h"
#include "core/Image.h"
#include "stb_truetype.h"

namespace reaction {

namespace fs = std::filesystem;

namespace {

// ---- UTF-8 ------------------------------------------------------------------

std::u32string decode(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (n < 0 || i + static_cast<std::size_t>(n) >= s.size()) {   // a bad lead byte, or cut short
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        char32_t cp = n == 0 ? c : n == 1 ? (c & 0x1F) : n == 2 ? (c & 0x0F) : (c & 0x07);
        bool ok = true;
        for (int k = 1; k <= n; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            if ((cc & 0xC0) != 0x80) ok = false;
            cp = cp << 6 | (cc & 0x3F);
        }
        out.push_back(ok ? cp : 0xFFFD);
        i += ok ? static_cast<std::size_t>(n) + 1 : 1;
    }
    return out;
}

std::string encode(std::u32string_view s) {
    std::string out;
    for (char32_t c : s) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | c >> 6);
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | c >> 12);
            out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | c >> 18);
            out += static_cast<char>(0x80 | (c >> 12 & 0x3F));
            out += static_cast<char>(0x80 | (c >> 6 & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// The built-in font's cell for a code point (it has printable ASCII only).
char builtinChar(char32_t c) {
    if (c == U'\u2026') return builtin_font::kEllipsis;
    if (c == U'\u00B7' || c == U'\u2013' || c == U'\u2014') return '-';
    if (c == U'\u2019' || c == U'\u2018') return '\'';
    if (c == U'\u201C' || c == U'\u201D') return '"';
    return c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?';
}

bool readFile(const fs::path& file, std::vector<unsigned char>& out) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return false;
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}

// The font's subfamily ("Regular", "Bold"...), from its name table (Windows, US English).
std::string styleName(const stbtt_fontinfo& info) {
    int len = 0;
    const char* s = stbtt_GetFontNameString(&info, &len, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP,
                                            STBTT_MS_LANG_ENGLISH, 2);
    std::string out;
    for (int i = 0; s && i + 1 < len; i += 2)   // UTF-16BE; the names are ASCII
        if (s[i] == 0) out += s[i + 1];
    return out;
}

}  // namespace

struct UiFont::Impl {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    int ascent = 0, descent = 0, lineGap = 0;
    float unitsPerEm = 1;
};

UiFont::~UiFont() = default;

std::unique_ptr<UiFont> UiFont::load(const fs::path& file, int index, std::string_view style) {
    auto impl = std::make_unique<Impl>();
    if (!readFile(file, impl->data)) return nullptr;
    const int faces = std::max(stbtt_GetNumberOfFonts(impl->data.data()), 1);
    bool found = false;
    for (int i = style.empty() ? index : 0; i < (style.empty() ? index + 1 : faces); ++i) {
        const int offset = stbtt_GetFontOffsetForIndex(impl->data.data(), i);
        if (offset < 0 || !stbtt_InitFont(&impl->info, impl->data.data(), offset)) continue;
        if (!style.empty() && styleName(impl->info) != style) continue;
        found = true;
        break;
    }
    // A text font: it must have the Latin letters.
    if (!found || !stbtt_FindGlyphIndex(&impl->info, 'A') || !stbtt_FindGlyphIndex(&impl->info, 'g')) return nullptr;
    stbtt_GetFontVMetrics(&impl->info, &impl->ascent, &impl->descent, &impl->lineGap);
    impl->unitsPerEm = 1 / stbtt_ScaleForMappingEmToPixels(&impl->info, 1);
    if (impl->ascent <= 0) return nullptr;
    std::unique_ptr<UiFont> font(new UiFont);
    font->impl_ = std::move(impl);
    font->file_ = file;
    return font;
}

std::unique_ptr<UiFont> UiFont::builtin() { return std::unique_ptr<UiFont>(new UiFont); }

int UiFont::builtinScale(float px) const { return std::max(1, static_cast<int>(std::lround(px / 9.f))); }

float UiFont::ascent(float px) const {
    if (!impl_) return 7.f * static_cast<float>(builtinScale(px));
    return static_cast<float>(impl_->ascent) * px / impl_->unitsPerEm;
}

float UiFont::descent(float px) const {
    if (!impl_) return 2.f * static_cast<float>(builtinScale(px));
    return static_cast<float>(-impl_->descent) * px / impl_->unitsPerEm;
}

float UiFont::capHeight(float px) const {
    if (!impl_) return 7.f * static_cast<float>(builtinScale(px));
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!stbtt_GetCodepointBox(&impl_->info, 'H', &x0, &y0, &x1, &y1) || y1 <= 0) return ascent(px) * 0.72f;
    return static_cast<float>(y1) * px / impl_->unitsPerEm;
}

int UiFont::glyphIndex(char32_t cp) const {
    if (!impl_) return static_cast<unsigned char>(builtinChar(cp));
    int g = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp));
    if (!g && cp == U'\u2026') g = -1;   // (drawn as "...")
    return g;
}

float UiFont::advance(int index, float px) const {
    if (!impl_) return 8.f * static_cast<float>(builtinScale(px));
    int adv = 0, lsb = 0;
    stbtt_GetGlyphHMetrics(&impl_->info, index, &adv, &lsb);
    return static_cast<float>(adv) * px / impl_->unitsPerEm + embolden_ * px;
}

float UiFont::kern(int a, int b, float px) const {
    if (!impl_ || a <= 0 || b <= 0) return 0;
    return static_cast<float>(stbtt_GetGlyphKernAdvance(&impl_->info, a, b)) * px / impl_->unitsPerEm;
}

const UiFont::Glyph& UiFont::glyph(int index, float px, float shift) const {
    const auto sizeKey = static_cast<std::uint64_t>(std::lround(px * 8));
    const auto shiftKey = static_cast<std::uint64_t>(std::clamp(static_cast<int>(shift * 4), 0, 3));
    const std::uint64_t key = static_cast<std::uint64_t>(index) << 24 | sizeKey << 2 | shiftKey;
    if (auto it = cache_.find(key); it != cache_.end()) return it->second;
    Glyph g;
    if (!impl_) {
        // The 8x8 cell, scaled by whole pixels.
        const int s = builtinScale(px);
        Image cell(8, 8, 0);
        const char ch = static_cast<char>(index);
        builtin_font::drawText(cell, 0, 0, std::string_view(&ch, 1), 1);
        g.w = g.h = 8 * s;
        g.xoff = 0;
        g.yoff = -7 * s;
        g.alpha.assign(static_cast<std::size_t>(g.w) * g.h, 0);
        for (int y = 0; y < 8 * s; ++y)
            for (int x = 0; x < 8 * s; ++x)
                if (cell.at(x / s, y / s)) g.alpha[static_cast<std::size_t>(y) * g.w + x] = 255;
    } else {
        const float scale = px / impl_->unitsPerEm;
        const float sx = static_cast<float>(shiftKey) / 4;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBoxSubpixel(&impl_->info, index, scale, scale, sx, 0, &x0, &y0, &x1, &y1);
        const int w = x1 - x0, h = y1 - y0;
        if (w > 0 && h > 0) {
            std::vector<std::uint8_t> a(static_cast<std::size_t>(w) * h, 0);
            stbtt_MakeGlyphBitmapSubpixel(&impl_->info, a.data(), w, h, w, scale, scale, sx, 0, index);
            const float bold = embolden_ * px;
            if (bold > 0) {
                // Widen: each pixel takes the strongest of the ones up to `bold` to its left.
                const int whole = static_cast<int>(bold), extra = whole + 1;
                const float frac = bold - static_cast<float>(whole);
                g.w = w + extra;
                g.h = h;
                g.alpha.assign(static_cast<std::size_t>(g.w) * h, 0);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < g.w; ++x) {
                        float v = 0;
                        for (int k = 0; k <= whole; ++k)
                            if (x - k >= 0 && x - k < w) v = std::max(v, static_cast<float>(a[static_cast<std::size_t>(y) * w + x - k]));
                        if (x - whole - 1 >= 0 && x - whole - 1 < w)
                            v = std::max(v, frac * a[static_cast<std::size_t>(y) * w + x - whole - 1]);
                        g.alpha[static_cast<std::size_t>(y) * g.w + x] = static_cast<std::uint8_t>(v);
                    }
            } else {
                g.w = w;
                g.h = h;
                g.alpha = std::move(a);
            }
            g.xoff = x0;
            g.yoff = y0;
        }
    }
    return cache_.emplace(key, std::move(g)).first->second;
}

float UiFont::measure(std::string_view utf8, float px) const {
    float x = 0;
    int prev = 0;
    for (char32_t cp : decode(utf8)) {
        const int g = glyphIndex(cp);
        if (g < 0) {   // no "…": three dots
            x += 3 * advance(glyphIndex('.'), px);
            prev = 0;
            continue;
        }
        x += kern(prev, g, px) + advance(g, px);
        prev = g;
    }
    return x;
}

float UiFont::draw(Canvas& canvas, float x, float baseline, std::string_view utf8, float px, Color c) const {
    int prev = 0;
    const float by = std::round(baseline);
    auto one = [&](int g) {
        const float fx = std::floor(x), shift = x - fx;
        const Glyph& gl = glyph(g, px, impl_ ? shift : 0);
        if (!gl.alpha.empty())
            canvas.drawMask(gl.alpha.data(), gl.w, gl.h, gl.w, static_cast<int>(fx) + gl.xoff,
                            static_cast<int>(by) + gl.yoff, c);
        x += advance(g, px);
    };
    for (char32_t cp : decode(utf8)) {
        int g = glyphIndex(cp);
        if (g < 0) {
            const int dot = glyphIndex('.');
            for (int i = 0; i < 3; ++i) one(dot);
            prev = 0;
            continue;
        }
        x += kern(prev, g, px);
        one(g);
        prev = g;
    }
    return x;
}

// ---- The system's font --------------------------------------------------------

namespace {

struct Face {
    Face(fs::path f = {}, int i = 0, std::string s = {}) : file(std::move(f)), index(i), style(std::move(s)) {}
    fs::path file;
    int index = 0;
    std::string style;   // non-empty: the face with this style (in a .ttc)
};

struct Family {
    Face regular;
    std::vector<Face> bold;   // the first that loads; none: synthetic bold
};

fs::path envPath(const char* name) {
#ifdef _WIN32
    std::wstring wname(name, name + std::char_traits<char>::length(name));
    const wchar_t* v = _wgetenv(wname.c_str());
    return v && *v ? fs::path(v) : fs::path();
#else
    const char* v = std::getenv(name);
    return v && *v ? fs::path(v) : fs::path();
#endif
}

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
// fontconfig's choice for `pattern`, if fc-match is there.
fs::path fcMatch(const char* pattern) {
    const std::string cmd = std::string("fc-match -f '%{file}' '") + pattern + "' 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return {};
    std::string out;
    char buf[512];
    while (std::size_t n = std::fread(buf, 1, sizeof buf, p)) out.append(buf, n);
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out.empty() ? fs::path() : fs::path(out);
}
#endif

std::vector<Family> candidates() {
    std::vector<Family> out;
    if (fs::path f = envPath("COVERT_UI_FONT"); !f.empty()) {
        Family fam{{f}, {}};
        if (fs::path b = envPath("COVERT_UI_FONT_BOLD"); !b.empty()) fam.bold.push_back({b});
        out.push_back(fam);
        return out;
    }
#if defined(__EMSCRIPTEN__)
    // (No system fonts in a browser: the built-in font.)
#elif defined(__APPLE__)
    const fs::path sys = "/System/Library/Fonts";
    out.push_back({{sys / "SFNS.ttf"}, {}});   // variable: the default instance, synthetic bold
    out.push_back({{sys / "HelveticaNeue.ttc"}, {{sys / "HelveticaNeue.ttc", 0, "Bold"}}});
    out.push_back({{sys / "Helvetica.ttc"}, {{sys / "Helvetica.ttc", 0, "Bold"}}});
    out.push_back({{sys / "Supplemental" / "Arial.ttf"}, {{sys / "Supplemental" / "Arial Bold.ttf"}}});
#elif defined(_WIN32)
    fs::path win = envPath("WINDIR");
    if (win.empty()) win = "C:\\Windows";
    const fs::path fonts = win / "Fonts";
    out.push_back({{fonts / "segoeui.ttf"}, {{fonts / "seguisb.ttf"}, {fonts / "segoeuib.ttf"}}});
    out.push_back({{fonts / "arial.ttf"}, {{fonts / "arialbd.ttf"}}});
#else
    if (fs::path r = fcMatch("sans-serif:style=Regular"); !r.empty()) {
        Family fam{{r}, {}};
        if (fs::path b = fcMatch("sans-serif:bold"); !b.empty() && b != r) fam.bold.push_back({b});
        out.push_back(fam);
    }
    // The usual places of the common families (a fixed list: no search).
    const fs::path u = "/usr/share/fonts";
    auto add = [&](std::vector<fs::path> regular, std::vector<fs::path> bold) {
        for (std::size_t i = 0; i < regular.size(); ++i) {
            Family fam{{u / regular[i]}, {}};
            if (i < bold.size() && !bold[i].empty()) fam.bold.push_back({u / bold[i]});
            out.push_back(fam);
        }
    };
    add({"truetype/dejavu/DejaVuSans.ttf", "TTF/DejaVuSans.ttf", "dejavu/DejaVuSans.ttf",
         "dejavu-sans-fonts/DejaVuSans.ttf"},
        {"truetype/dejavu/DejaVuSans-Bold.ttf", "TTF/DejaVuSans-Bold.ttf", "dejavu/DejaVuSans-Bold.ttf",
         "dejavu-sans-fonts/DejaVuSans-Bold.ttf"});
    add({"truetype/noto/NotoSans-Regular.ttf", "noto/NotoSans-Regular.ttf", "google-noto/NotoSans-Regular.ttf",
         "google-noto-vf/NotoSans[wght].ttf"},
        {"truetype/noto/NotoSans-Bold.ttf", "noto/NotoSans-Bold.ttf", "google-noto/NotoSans-Bold.ttf", ""});
    add({"truetype/liberation/LiberationSans-Regular.ttf", "liberation/LiberationSans-Regular.ttf",
         "liberation-sans/LiberationSans-Regular.ttf", "TTF/LiberationSans-Regular.ttf"},
        {"truetype/liberation/LiberationSans-Bold.ttf", "liberation/LiberationSans-Bold.ttf",
         "liberation-sans/LiberationSans-Bold.ttf", "TTF/LiberationSans-Bold.ttf"});
    add({"opentype/cantarell/Cantarell-Regular.otf", "cantarell/Cantarell-VF.otf",
         "abattis-cantarell-fonts/Cantarell-Regular.otf"},
        {"opentype/cantarell/Cantarell-Bold.otf", "", "abattis-cantarell-fonts/Cantarell-Bold.otf"});
    add({"truetype/ubuntu/Ubuntu-R.ttf", "truetype/ubuntu/Ubuntu[wdth,wght].ttf"}, {"truetype/ubuntu/Ubuntu-B.ttf", ""});
#endif
    return out;
}

constexpr float kSyntheticBold = 0.045f;   // x the size

}  // namespace

std::string UiFonts::describe() const {
    if (regular->isBuiltin()) return "the built-in 8x8 font";
    std::string s = regular->file().string();
    if (bold->emboldened()) s += " (bold: synthetic)";
    else s += " (bold: " + bold->file().string() + ")";
    return s;
}

const UiFonts& systemUiFonts() {
    static const UiFonts fonts = [] {
        UiFonts f;
        const char* env = std::getenv("COVERT_UI_FONT");
        const bool forceBuiltin = env && std::string_view(env) == "builtin";
        for (const Family& fam : forceBuiltin ? std::vector<Family>{} : candidates()) {
            f.regular = UiFont::load(fam.regular.file, fam.regular.index, fam.regular.style);
            if (!f.regular) continue;
            for (const Face& b : fam.bold)
                if ((f.bold = UiFont::load(b.file, b.index, b.style))) break;
            if (!f.bold) {
                f.bold = UiFont::load(fam.regular.file, fam.regular.index, fam.regular.style);
                f.bold->setEmbolden(kSyntheticBold);
            }
            break;
        }
        if (!f.regular) {
            f.regular = UiFont::builtin();
            f.bold = UiFont::builtin();
        }
        std::fprintf(stderr, "ui font: %s\n", f.describe().c_str());
        return f;
    }();
    return fonts;
}

// ---- Layout ---------------------------------------------------------------------

std::vector<std::string> wrapText(const UiFont& font, std::string_view text, float px, float maxWidth) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        const std::string_view para = text.substr(start, nl == std::string_view::npos ? text.npos : nl - start);
        const std::u32string cps = decode(para);
        std::u32string line;
        std::size_t i = 0;
        auto width = [&](const std::u32string& s) { return font.measure(encode(s), px); };
        if (cps.empty()) lines.emplace_back();
        while (i < cps.size()) {
            // The next word (with the spaces before it).
            std::size_t j = i;
            while (j < cps.size() && cps[j] == U' ') ++j;
            while (j < cps.size() && cps[j] != U' ') ++j;
            std::u32string candidate = line + cps.substr(i, j - i);
            if (width(candidate) <= maxWidth || line.empty()) {
                if (line.empty()) {
                    // Strip leading spaces of a line; a word too long: break it.
                    std::size_t k = i;
                    while (k < j && cps[k] == U' ') ++k;
                    std::u32string word = cps.substr(k, j - k);
                    if (width(word) > maxWidth) {
                        std::u32string part;
                        std::size_t m = 0;
                        for (; m < word.size(); ++m) {
                            if (!part.empty() && width(part + word[m]) > maxWidth) break;
                            part += word[m];
                        }
                        lines.push_back(encode(part));
                        i = k + m;
                        continue;
                    }
                    line = word;
                } else {
                    line = candidate;
                }
                i = j;
            } else {
                lines.push_back(encode(line));
                line.clear();
            }
        }
        if (!line.empty()) lines.push_back(encode(line));
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return lines;
}

std::string elideRight(const UiFont& font, std::string_view text, float px, float maxWidth) {
    if (font.measure(text, px) <= maxWidth) return std::string(text);
    std::u32string cps = decode(text);
    while (!cps.empty() && font.measure(encode(cps) + "\u2026", px) > maxWidth) cps.pop_back();
    while (!cps.empty() && cps.back() == U' ') cps.pop_back();
    return encode(cps) + "\u2026";
}

std::string elideMiddle(const UiFont& font, std::string_view text, float px, float maxWidth) {
    if (font.measure(text, px) <= maxWidth) return std::string(text);
    const std::u32string cps = decode(text);
    // Keep more of the end (the file's name) than of the start.
    std::size_t keep = cps.size();
    while (keep > 1) {
        --keep;
        const std::size_t tail = (keep * 3 + 4) / 5, head = keep - tail;
        const std::string s = encode(cps.substr(0, head)) + "\u2026" + encode(cps.substr(cps.size() - tail));
        if (font.measure(s, px) <= maxWidth) return s;
    }
    return "\u2026";
}

}  // namespace reaction
