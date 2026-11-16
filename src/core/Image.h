// 8-bit indexed images and palettes: the game draws into 320x200 pages of
// palette indices (MCGA mode 13h), so the engine does too.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace reaction {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
};

// 256 colours, 8 bits per channel (VGA DAC values are 6-bit: use fromVga6).
struct Palette {
    std::array<Rgb, 256> colors{};

    static Rgb fromVga6(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        auto x = [](std::uint8_t v) { return static_cast<std::uint8_t>((v & 63) << 2 | (v & 63) >> 4); };
        return {x(r), x(g), x(b)};
    }
};

class Image {
public:
    Image() = default;
    Image(int width, int height, std::uint8_t fill = 0)
        : w_(width), h_(height), px_(static_cast<std::size_t>(width) * height, fill) {}

    int width() const { return w_; }
    int height() const { return h_; }
    bool empty() const { return px_.empty(); }

    std::uint8_t at(int x, int y) const { return px_[static_cast<std::size_t>(y) * w_ + x]; }
    void set(int x, int y, std::uint8_t c) {
        if (x >= 0 && y >= 0 && x < w_ && y < h_) px_[static_cast<std::size_t>(y) * w_ + x] = c;
    }
    std::uint8_t* row(int y) { return px_.data() + static_cast<std::size_t>(y) * w_; }
    const std::uint8_t* row(int y) const { return px_.data() + static_cast<std::size_t>(y) * w_; }
    std::vector<std::uint8_t>& pixels() { return px_; }
    const std::vector<std::uint8_t>& pixels() const { return px_; }

    void fill(std::uint8_t c) { std::fill(px_.begin(), px_.end(), c); }
    // Copies a rectangle of `src` to (dx, dy), clipped; `transparent` < 0: opaque.
    void blit(const Image& src, int sx, int sy, int w, int h, int dx, int dy, int transparent = -1);

private:
    int w_ = 0, h_ = 0;
    std::vector<std::uint8_t> px_;
};

// Writes an 8-bit indexed BMP (for the dump tool and debugging).
void writeBmp(const std::filesystem::path& path, const Image& img, const Palette& pal);

}  // namespace reaction
