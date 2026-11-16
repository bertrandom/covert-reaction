// pic, pic-all: decode .PIC pictures to BMP, in the MCGA colours.
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "Tool.h"
#include "core/Image.h"
#include "formats/McgaPalette.h"
#include "formats/Pic.h"

using namespace reaction;

namespace {

const PaletteMap& paletteArg(const std::vector<std::string>& args, std::size_t i) {
    if (args.size() <= i) return palettes::kGame;
    const PaletteMap* p = palettes::byName(args[i]);
    if (!p) throw std::runtime_error("unknown palette '" + args[i] + "' (ega, game, face, chase)");
    return *p;
}

Picture decodeAndDescribe(const std::string& name, const Bytes& bytes) {
    std::size_t end = 0;
    Picture pic = decodePicture(bytes, 0, &end);
    std::printf("%-14s %3dx%-3d flags %02Xh %s, colour map %zu bytes, data %zu/%zu bytes\n", name.c_str(),
                pic.header.width, pic.header.height, pic.header.flags, pic.header.nibbles() ? "4-bit" : "8-bit",
                pic.header.colorMap.size(), end, bytes.size());
    return pic;
}

}  // namespace

REACTION_COMMAND("pic", "pic NAME OUT.bmp [ega|game|face|chase]: decode a .PIC (default palette: game)",
              [](const GameData& d, const std::vector<std::string>& args) {
                  if (args.size() < 2) throw std::runtime_error("usage: pic NAME OUT.bmp [palette]");
                  const Picture pic = decodeAndDescribe(args[0], d.read(args[0]));
                  writeBmp(args[1], pic.image, mcgaPalette(paletteArg(args, 2)));
                  return 0;
              });

REACTION_COMMAND("pic-all", "pic-all OUTDIR [palette]: decode every .PIC to OUTDIR/NAME.bmp",
              [](const GameData& d, const std::vector<std::string>& args) {
                  if (args.empty()) throw std::runtime_error("usage: pic-all OUTDIR [palette]");
                  const std::filesystem::path out = args[0];
                  std::filesystem::create_directories(out);
                  const Palette pal = mcgaPalette(paletteArg(args, 1));
                  int failures = 0;
                  for (const auto& e : std::filesystem::directory_iterator(d.dir())) {
                      std::string name = e.path().filename().string();
                      std::string upper = name;
                      for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                      if (upper.size() < 4 || upper.substr(upper.size() - 4) != ".PIC") continue;
                      try {
                          const Picture pic = decodeAndDescribe(name, d.read(name));
                          writeBmp(out / (upper.substr(0, upper.size() - 4) + ".bmp"), pic.image, pal);
                      } catch (const std::exception& ex) {
                          std::fprintf(stderr, "%s: %s\n", name.c_str(), ex.what());
                          ++failures;
                      }
                  }
                  return failures ? 1 : 0;
              });
