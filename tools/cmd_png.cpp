// redux-tool bmp-png IN.bmp OUT.png [SCALE]: an 8-bit indexed BMP (the app's
// --shot screenshots, redux-tool's dumps) as a PNG, for viewers that don't read
// BMP. The PNG is RGB, stored without compression (deflate "stored" blocks).
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "Tool.h"

namespace {

std::uint32_t crc32(const std::uint8_t* p, std::size_t n, std::uint32_t c = 0xFFFFFFFFu) {
    for (std::size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return c;
}

void be32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) o.push_back(static_cast<std::uint8_t>(v >> s));
}

void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
    be32(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc32(td.data(), td.size()) ^ 0xFFFFFFFFu);
}

}  // namespace

REDUX_COMMAND("bmp-png", "bmp-png IN.bmp OUT.png [SCALE]: convert an 8-bit indexed BMP to PNG",
              [](const redux::GameData&, const std::vector<std::string>& args) {
                  if (args.size() < 2) return 2;
                  const int scale = args.size() > 2 ? std::stoi(args[2]) : 1;
                  std::ifstream in(args[0], std::ios::binary);
                  std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                  auto u32 = [&](std::size_t o) {
                      return static_cast<std::uint32_t>(b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24);
                  };
                  if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') return 1;
                  const std::uint32_t off = u32(10), hdr = u32(14);
                  const int w = static_cast<int>(u32(18)), hraw = static_cast<std::int32_t>(u32(22));
                  const int h = hraw < 0 ? -hraw : hraw;
                  if (b[28] != 8) return 1;
                  const std::size_t pal = 14 + hdr;
                  const std::size_t stride = (static_cast<std::size_t>(w) + 3) & ~std::size_t(3);
                  const int W = w * scale, H = h * scale;
                  std::vector<std::uint8_t> raw;
                  for (int y = 0; y < H; ++y) {
                      raw.push_back(0);
                      const int sy = y / scale;
                      const int row = hraw < 0 ? sy : h - 1 - sy;
                      for (int x = 0; x < W; ++x) {
                          const std::uint8_t i = b[off + static_cast<std::size_t>(row) * stride + static_cast<std::size_t>(x / scale)];
                          raw.push_back(b[pal + i * 4u + 2]);
                          raw.push_back(b[pal + i * 4u + 1]);
                          raw.push_back(b[pal + i * 4u]);
                      }
                  }
                  std::vector<std::uint8_t> z{0x78, 0x01};
                  std::uint32_t a = 1, bb = 0;
                  for (auto v : raw) {
                      a = (a + v) % 65521;
                      bb = (bb + a) % 65521;
                  }
                  for (std::size_t p = 0; p < raw.size() || p == 0;) {
                      const std::size_t n = std::min<std::size_t>(65535, raw.size() - p);
                      z.push_back(p + n >= raw.size() ? 1 : 0);
                      z.push_back(static_cast<std::uint8_t>(n));
                      z.push_back(static_cast<std::uint8_t>(n >> 8));
                      z.push_back(static_cast<std::uint8_t>(~n));
                      z.push_back(static_cast<std::uint8_t>(~n >> 8));
                      z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(p),
                               raw.begin() + static_cast<std::ptrdiff_t>(p + n));
                      p += n;
                      if (n == 0) break;
                  }
                  be32(z, bb << 16 | a);
                  std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
                  std::vector<std::uint8_t> ihdr;
                  be32(ihdr, static_cast<std::uint32_t>(W));
                  be32(ihdr, static_cast<std::uint32_t>(H));
                  ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
                  chunk(out, "IHDR", ihdr);
                  chunk(out, "IDAT", z);
                  chunk(out, "IEND", {});
                  std::ofstream o(args[1], std::ios::binary);
                  o.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
                  return o ? 0 : 1;
              });
