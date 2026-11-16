// pan, pan-info: play .PAN animations into BMP frames, list their contents.
#include <cstdio>
#include <stdexcept>
#include <string>

#include "Tool.h"
#include "core/Image.h"
#include "formats/Pan.h"

using namespace reaction;

namespace {

void describe(const std::string& name, const PanFile& f) {
    static const char* const kBackground[] = {"none", "picture", "fill"};
    int images = 0;
    for (const auto& img : f.images) images += !img.empty();
    std::printf("%s: version %d, %s, palette %s (type %d), window (%d,%d)+(%d,%d), %d ticks/frame\n",
                name.c_str(), f.version, f.compressed ? "compressed" : "raw", f.hasPalette ? "yes" : "no",
                f.paletteType, f.x0, f.y0, f.x1, f.y1, f.frameTicks);
    std::printf("  background %s", kBackground[static_cast<int>(f.background)]);
    if (f.background == PanFile::Background::Picture)
        std::printf(" %dx%d", f.backgroundImage.width(), f.backgroundImage.height());
    if (f.background == PanFile::Background::Fill) std::printf(" colour %d", f.backgroundColor);
    std::printf(", %d images, script %zu bytes, %zu bytes parsed\n", images, f.script.size(), f.parsedSize);
}

std::string frameName(int i) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "frame_%04d.bmp", i);
    return buf;
}

}  // namespace

REACTION_COMMAND("pan", "pan NAME OUTDIR [frames]: play a .PAN, writing each frame to OUTDIR (default 300 frames)",
              [](const GameData& d, const std::vector<std::string>& args) {
                  if (args.size() < 2) throw std::runtime_error("usage: pan NAME OUTDIR [frames]");
                  const int maxFrames = args.size() > 2 ? std::stoi(args[2]) : 300;
                  PanFile file = PanFile::parse(d.read(args[0]));
                  describe(args[0], file);
                  const std::filesystem::path out = args[1];
                  std::filesystem::create_directories(out);
                  PanPlayer player(std::move(file));
                  const Palette pal = player.palette();
                  int n = 0;
                  while (n < maxFrames && player.step()) {
                      writeBmp(out / frameName(n), player.frame(), pal);
                      for (int s : player.sounds()) std::printf("  frame %d: sound %d\n", n, s);
                      ++n;
                  }
                  std::printf("%d frames%s, %.3f s each\n", n, player.finished() ? " (script ended)" : "",
                              player.framePeriod());
                  return 0;
              });

REACTION_COMMAND("pan-info", "pan-info NAME [OUTDIR]: describe a .PAN (and dump its background and images)",
              [](const GameData& d, const std::vector<std::string>& args) {
                  if (args.empty()) throw std::runtime_error("usage: pan-info NAME [OUTDIR]");
                  const Bytes bytes = d.read(args[0]);
                  const PanFile f = PanFile::parse(bytes);
                  describe(args[0], f);
                  std::printf("  file size %zu\n", bytes.size());
                  for (std::size_t i = 0; i < f.images.size(); ++i)
                      if (!f.images[i].empty())
                          std::printf("  image %3zu: %dx%d\n", i, f.images[i].width(), f.images[i].height());
                  if (args.size() > 1) {
                      const std::filesystem::path out = args[1];
                      std::filesystem::create_directories(out);
                      const Palette pal = f.palette().value_or(mcgaPalette());
                      if (!f.backgroundImage.empty()) writeBmp(out / "background.bmp", f.backgroundImage, pal);
                      for (std::size_t i = 0; i < f.images.size(); ++i)
                          if (!f.images[i].empty())
                              writeBmp(out / ("image_" + std::to_string(i) + ".bmp"), f.images[i], pal);
                  }
                  return 0;
              });
