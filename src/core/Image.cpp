#include "core/Image.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace redux {

void Image::blit(const Image& src, int sx, int sy, int w, int h, int dx, int dy, int transparent) {
    for (int y = 0; y < h; ++y) {
        int ty = dy + y, fy = sy + y;
        if (ty < 0 || ty >= h_ || fy < 0 || fy >= src.h_) continue;
        for (int x = 0; x < w; ++x) {
            int tx = dx + x, fx = sx + x;
            if (tx < 0 || tx >= w_ || fx < 0 || fx >= src.w_) continue;
            std::uint8_t c = src.at(fx, fy);
            if (c != transparent) px_[static_cast<std::size_t>(ty) * w_ + tx] = c;
        }
    }
}

void writeBmp(const std::filesystem::path& path, const Image& img, const Palette& pal) {
    const int w = img.width(), h = img.height();
    const int stride = (w + 3) & ~3;
    const std::uint32_t pixelOffset = 14 + 40 + 256 * 4;
    const std::uint32_t fileSize = pixelOffset + static_cast<std::uint32_t>(stride * h);
    std::vector<std::uint8_t> out;
    auto u16 = [&](std::uint32_t v) { out.push_back(v & 255); out.push_back(v >> 8 & 255); };
    auto u32 = [&](std::uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); };
    out.push_back('B'); out.push_back('M');
    u32(fileSize); u32(0); u32(pixelOffset);
    u32(40); u32(static_cast<std::uint32_t>(w)); u32(static_cast<std::uint32_t>(h));
    u16(1); u16(8); u32(0); u32(static_cast<std::uint32_t>(stride * h));
    u32(2835); u32(2835); u32(256); u32(0);
    for (const auto& c : pal.colors) { out.push_back(c.b); out.push_back(c.g); out.push_back(c.r); out.push_back(0); }
    for (int y = h - 1; y >= 0; --y) {
        const std::uint8_t* r = img.row(y);
        out.insert(out.end(), r, r + w);
        out.insert(out.end(), static_cast<std::size_t>(stride - w), 0);
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path.string());
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
}

}  // namespace redux
