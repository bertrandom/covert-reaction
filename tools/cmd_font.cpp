// reaction-tool font-sheet OUT.bmp [SCALE]
//            font-text FONT "string" OUT.bmp [SCALE] [--transparent]
//            font-window "line\n item\n ..." OUT.bmp [SCALE] [--sel N] [--odd]
// ("text" is taken by the keyed-text lookup in cmd_text.cpp.)
#include <cstdio>
#include <string>

#include "Tool.h"
#include "formats/Font.h"
#include "formats/TextWindow.h"

using namespace reaction;

namespace {

// The 16 EGA/VGA default colours: the game's text colours (15 white, 7 grey,
// 5 menu background, 14 highlight, 11 "OK") are indices into the default
// palette set by init_mode.
Palette egaPalette() {
    static constexpr std::uint8_t ega[16][3] = {
        {0, 0, 0},    {0, 0, 42},  {0, 42, 0},  {0, 42, 42},  {42, 0, 0},  {42, 0, 42},
        {42, 21, 0},  {42, 42, 42}, {21, 21, 21}, {21, 21, 63}, {21, 63, 21}, {21, 63, 63},
        {63, 21, 21}, {63, 21, 63}, {63, 63, 21}, {63, 63, 63}};
    Palette p;
    for (int i = 0; i < 16; ++i) p.colors[static_cast<std::size_t>(i)] = Palette::fromVga6(ega[i][0], ega[i][1], ega[i][2]);
    // A few extra colours for the sheet's grid.
    p.colors[16] = {40, 40, 56};
    p.colors[17] = {70, 70, 96};
    return p;
}

Image scaled(const Image& src, int s) {
    if (s <= 1) return src;
    Image out(src.width() * s, src.height() * s);
    for (int y = 0; y < out.height(); ++y)
        for (int x = 0; x < out.width(); ++x) out.set(x, y, src.at(x / s, y / s));
    return out;
}

int argInt(const std::vector<std::string>& a, std::size_t i, int def) {
    return i < a.size() && !a[i].empty() && a[i][0] != '-' ? std::stoi(a[i]) : def;
}

bool hasFlag(const std::vector<std::string>& a, const std::string& f) {
    for (const auto& s : a)
        if (s == f) return true;
    return false;
}

// Unescapes \n, \\ and \xHH (for colour codes) in a command-line string.
std::string unescape(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out += s[i];
            continue;
        }
        char c = s[++i];
        if (c == 'n') out += '\n';
        else if (c == 'x' && i + 2 < s.size()) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else out += c;
    }
    return out;
}

}  // namespace

REACTION_COMMAND("font-sheet", "font-sheet OUT.bmp [SCALE]: every glyph of every font in FONTS.CV",
              [](const GameData& d, const std::vector<std::string>& a) {
                  if (a.empty()) return 2;
                  const FontSet fonts = FontSet::load(d);
                  constexpr int cols = 16, cellW = 12, cellH = 12, pad = 4;
                  const int sheetW = pad * 2 + cols * cellW;
                  // Per font: a title line, the glyph grid, two sample lines.
                  int h = pad;
                  for (int f = 1; f <= fonts.size(); ++f)
                      h += 10 + (fonts.font(f).count() + cols - 1) / cols * cellH + 24 + pad;
                  Image img(std::max(sheetW, 320), h, 0);
                  int y = pad;
                  for (int f = 1; f <= fonts.size(); ++f) {
                      const Font& font = fonts.font(f);
                      char title[96];
                      std::snprintf(title, sizeof title, "Font %d: %d-%d h%d+%d sp%d", f, font.first(),
                                    font.last(), font.glyphHeight(), font.extraRows(), font.spacing());
                      fonts.font(1).draw(img, pad, y, title, {0, 14, 0});
                      y += 10;
                      for (int i = 0; i < font.count(); ++i) {
                          const auto ch = static_cast<std::uint8_t>(font.first() + i);
                          const int gx = pad + i % cols * cellW + 1, gy = y + i / cols * cellH + 1;
                          // Cell background: the advance (width + spacing) by
                          // the height + extra rows; glyph width darker.
                          for (int yy = 0; yy < font.height(); ++yy)
                              for (int xx = 0; xx < font.charWidth(ch); ++xx)
                                  img.set(gx + xx, gy + yy, xx < font.glyphWidth(ch) ? 16 : 17);
                          const std::string s(1, static_cast<char>(ch));
                          font.draw(img, gx, gy, s, {0, 15, 0});
                      }
                      y += (font.count() + cols - 1) / cols * cellH + 2;
                      font.draw(img, pad, y, "The quick brown fox jumps over the lazy dog.", {0, 15, 0});
                      y += font.height() + 2;
                      font.draw(img, pad, y, "CIA AGENT MAX REMINGTON 0123456789 (?!)", {1, 15, 5});
                      y += 12 + pad;
                  }
                  writeBmp(a[0], scaled(img, argInt(a, 1, 1)), egaPalette());
                  return 0;
              });

REACTION_COMMAND("font-text",
              "font-text FONT STRING OUT.bmp [SCALE] [--transparent]: draw a string (\\n, \\xHH escapes)",
              [](const GameData& d, const std::vector<std::string>& a) {
                  if (a.size() < 3) return 2;
                  const FontSet fonts = FontSet::load(d);
                  const Font& font = fonts.font(std::stoi(a[0]));
                  const std::string s = unescape(a[1]);
                  const TextAttr attr{hasFlag(a, "--transparent") ? 0 : 1, 15, 5};
                  // One line per '\n', 8 pixels apart as the game spaces lines.
                  int w = 0, lines = 0;
                  std::size_t start = 0;
                  while (start <= s.size()) {
                      std::size_t nl = s.find('\n', start);
                      if (nl == std::string::npos) nl = s.size();
                      w = std::max(w, font.textWidth(s.substr(start, nl - start)));
                      ++lines;
                      start = nl + 1;
                  }
                  Image img(w + 4, lines * 8 + 4, 1);
                  start = 0;
                  for (int l = 0; start <= s.size(); ++l) {
                      std::size_t nl = s.find('\n', start);
                      if (nl == std::string::npos) nl = s.size();
                      const auto r = font.draw(img, 2, 2 + l * 8, s.substr(start, nl - start), attr);
                      std::printf("line %d: width %d, end x %d\n", l, r.x - 2, r.x);
                      start = nl + 1;
                  }
                  writeBmp(a[2], scaled(img, argInt(a, 3, 1)), egaPalette());
                  return 0;
              });

REACTION_COMMAND("font-window",
              "font-window TEXT OUT.bmp [SCALE] [--sel N] [--odd] [--ok]: a text window/menu as text_window draws it",
              [](const GameData& d, const std::vector<std::string>& a) {
                  if (a.size() < 2) return 2;
                  const FontSet fonts = FontSet::load(d);
                  const std::string s = unescape(a[0]);
                  TextWindowOptions opt;
                  for (std::size_t i = 0; i + 1 < a.size(); ++i)
                      if (a[i] == "--sel") opt.selected = std::stoi(a[i + 1]);
                  opt.showOk = hasFlag(a, "--ok");
                  const int x = 40, y = hasFlag(a, "--odd") ? 41 : 40;
                  if (hasFlag(a, "--odd")) opt.fillColor = 15;
                  Image screen(320, 200, 1);
                  const TextWindowLayout lay = drawTextWindow(screen, fonts.font(1), s, x, y, opt);
                  std::printf("lines %d, max width %d, items %d (first at line %d), box to (%d,%d), hotkeys \"%s\"\n",
                              lay.lines, lay.maxWidth, lay.items, lay.firstItem, lay.boxX2, lay.boxY2,
                              lay.hotkeys.c_str());
                  writeBmp(a[1], scaled(screen, argInt(a, 2, 1)), egaPalette());
                  return 0;
              });
