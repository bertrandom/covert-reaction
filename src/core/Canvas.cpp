#include "core/Canvas.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <string>

#include "miniz.h"

namespace reaction {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// 4x4 ordered dither, in [-0.5, 0.5).
constexpr float kBayer[4][4] = {
    {0 / 16.f - .5f, 8 / 16.f - .5f, 2 / 16.f - .5f, 10 / 16.f - .5f},
    {12 / 16.f - .5f, 4 / 16.f - .5f, 14 / 16.f - .5f, 6 / 16.f - .5f},
    {3 / 16.f - .5f, 11 / 16.f - .5f, 1 / 16.f - .5f, 9 / 16.f - .5f},
    {15 / 16.f - .5f, 7 / 16.f - .5f, 13 / 16.f - .5f, 5 / 16.f - .5f},
};

std::uint32_t pack(float r, float g, float b) {
    auto c = [](float v) { return static_cast<std::uint32_t>(std::clamp(v + 0.5f, 0.f, 255.f)); };
    return 0xFF000000u | c(r) << 16 | c(g) << 8 | c(b);
}

float coverageOf(float d) { return std::clamp(0.5f - d, 0.f, 1.f); }

float roundRectSdf(float px, float py, RectF r, float radius) {
    const float hw = r.w / 2, hh = r.h / 2;
    radius = std::min({radius, hw, hh});
    const float qx = std::abs(px - (r.x + hw)) - (hw - radius);
    const float qy = std::abs(py - (r.y + hh)) - (hh - radius);
    const float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.f) - radius;
}

float segmentDistance(float px, float py, float x0, float y0, float x1, float y1) {
    const float dx = x1 - x0, dy = y1 - y0;
    const float len2 = dx * dx + dy * dy;
    const float t = len2 > 0 ? std::clamp(((px - x0) * dx + (py - y0) * dy) / len2, 0.f, 1.f) : 0.f;
    const float ex = px - (x0 + t * dx), ey = py - (y0 + t * dy);
    return std::sqrt(ex * ex + ey * ey);
}

// Inigo Quilez's exact signed distance to a triangle.
float triangleSdf(float px, float py, const float (&v)[3][2]) {
    float d = 1e30f, s = 0;
    for (int i = 0, j = 2; i < 3; j = i++) {
        const float ex = v[j][0] - v[i][0], ey = v[j][1] - v[i][1];
        const float wx = px - v[i][0], wy = py - v[i][1];
        const float t = std::clamp((wx * ex + wy * ey) / (ex * ex + ey * ey), 0.f, 1.f);
        const float bx = wx - ex * t, by = wy - ey * t;
        d = std::min(d, bx * bx + by * by);
        const float cross = ex * wy - ey * wx;
        s += cross > 0 ? 1.f : -1.f;
    }
    // Inside: all three crosses have the same sign.
    return std::sqrt(d) * (std::abs(s) > 2.5f ? -1.f : 1.f);
}

}  // namespace

Color Color::mix(Color o, float t) const {
    auto m = [t](std::uint8_t from, std::uint8_t to) {
        return static_cast<std::uint8_t>(std::clamp(static_cast<float>(from) + static_cast<float>(to - from) * t + 0.5f, 0.f, 255.f));
    };
    return {m(r, o.r), m(g, o.g), m(b, o.b), m(a, o.a)};
}

void Canvas::resize(int width, int height) {
    w_ = std::max(width, 0);
    h_ = std::max(height, 0);
    px_.assign(static_cast<std::size_t>(w_) * h_, 0xFF000000u);
    resetClip();
}

void Canvas::setClip(RectF r) {
    cx0_ = std::clamp(static_cast<int>(std::floor(r.x)), 0, w_);
    cy0_ = std::clamp(static_cast<int>(std::floor(r.y)), 0, h_);
    cx1_ = std::clamp(static_cast<int>(std::ceil(r.right())), cx0_, w_);
    cy1_ = std::clamp(static_cast<int>(std::ceil(r.bottom())), cy0_, h_);
}

void Canvas::resetClip() {
    cx0_ = cy0_ = 0;
    cx1_ = w_;
    cy1_ = h_;
}

void Canvas::clear(Color c) { std::fill(px_.begin(), px_.end(), pack(c.r, c.g, c.b)); }

void Canvas::verticalGradient(Color top, Color bottom) {
    for (int y = 0; y < h_; ++y) {
        const float t = h_ > 1 ? static_cast<float>(y) / static_cast<float>(h_ - 1) : 0.f;
        const float r = top.r + (bottom.r - top.r) * t, g = top.g + (bottom.g - top.g) * t,
                    b = top.b + (bottom.b - top.b) * t;
        std::uint32_t* row = px_.data() + static_cast<std::size_t>(y) * w_;
        for (int x = 0; x < w_; ++x) {
            const float d = kBayer[y & 3][x & 3];
            row[x] = pack(r + d, g + d, b + d);
        }
    }
}

void Canvas::radialGlow(float cx, float cy, float radius, Color c) {
    if (radius <= 0) return;
    const int x0 = std::max(cx0_, static_cast<int>(cx - radius)), x1 = std::min(cx1_, static_cast<int>(cx + radius) + 1);
    const int y0 = std::max(cy0_, static_cast<int>(cy - radius)), y1 = std::min(cy1_, static_cast<int>(cy + radius) + 1);
    const float alpha = c.a / 255.f;
    for (int y = y0; y < y1; ++y) {
        std::uint32_t* row = px_.data() + static_cast<std::size_t>(y) * w_;
        for (int x = x0; x < x1; ++x) {
            const float dx = (static_cast<float>(x) + 0.5f - cx) / radius, dy = (static_cast<float>(y) + 0.5f - cy) / radius;
            const float d2 = dx * dx + dy * dy;
            if (d2 >= 1) continue;
            const float f = (1 - d2) * (1 - d2) * alpha;   // smooth falloff
            const std::uint32_t p = row[x];
            const float r = static_cast<float>(p >> 16 & 255), g = static_cast<float>(p >> 8 & 255),
                        b = static_cast<float>(p & 255);
            const float dd = kBayer[y & 3][x & 3];
            row[x] = pack(r + (c.r - r) * f + dd, g + (c.g - g) * f + dd, b + (c.b - b) * f + dd);
        }
    }
}

void Canvas::blend(int x, int y, Color c, float coverage) {
    if (x < cx0_ || y < cy0_ || x >= cx1_ || y >= cy1_) return;
    const float a = coverage * (c.a / 255.f);
    if (a <= 0) return;
    std::uint32_t& p = px_[static_cast<std::size_t>(y) * w_ + x];
    if (a >= 1) {
        p = pack(c.r, c.g, c.b);
        return;
    }
    const float r = static_cast<float>(p >> 16 & 255), g = static_cast<float>(p >> 8 & 255),
                b = static_cast<float>(p & 255);
    p = pack(r + (c.r - r) * a, g + (c.g - g) * a, b + (c.b - b) * a);
}

template <class Sdf>
void Canvas::fillSdf(float x0, float y0, float x1, float y1, Color c, Sdf sdf) {
    const int ix0 = std::max(cx0_, static_cast<int>(std::floor(x0 - 1)));
    const int iy0 = std::max(cy0_, static_cast<int>(std::floor(y0 - 1)));
    const int ix1 = std::min(cx1_, static_cast<int>(std::ceil(x1 + 1)));
    const int iy1 = std::min(cy1_, static_cast<int>(std::ceil(y1 + 1)));
    for (int y = iy0; y < iy1; ++y)
        for (int x = ix0; x < ix1; ++x) {
            const float cov = coverageOf(sdf(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f));
            if (cov > 0) blend(x, y, c, cov);
        }
}

void Canvas::fillRect(RectF r, Color c) {
    // Exact coverage per pixel (fractional edges).
    fillSdf(r.x, r.y, r.right(), r.bottom(), c, [r](float px, float py) {
        return std::max(std::max(r.x - px, px - r.right()), std::max(r.y - py, py - r.bottom()));
    });
}

void Canvas::fillRoundRect(RectF r, float radius, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    fillSdf(r.x, r.y, r.right(), r.bottom(), c, [&](float px, float py) { return roundRectSdf(px, py, r, radius); });
}

void Canvas::strokeRoundRect(RectF r, float radius, float thickness, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    fillSdf(r.x, r.y, r.right(), r.bottom(), c, [&](float px, float py) {
        const float d = roundRectSdf(px, py, r, radius);
        return std::max(d, -d - thickness);
    });
}

void Canvas::fillCircle(float cx, float cy, float radius, Color c) {
    fillSdf(cx - radius, cy - radius, cx + radius, cy + radius, c,
            [=](float px, float py) { return std::hypot(px - cx, py - cy) - radius; });
}

void Canvas::strokeCircle(float cx, float cy, float radius, float thickness, Color c) {
    const float mid = radius - thickness / 2;
    fillSdf(cx - radius, cy - radius, cx + radius, cy + radius, c,
            [=](float px, float py) { return std::abs(std::hypot(px - cx, py - cy) - mid) - thickness / 2; });
}

void Canvas::strokeArc(float cx, float cy, float radius, float a0, float a1, float thickness, Color c) {
    const float mid = radius - thickness / 2, half = thickness / 2;
    const float e0x = cx + mid * std::cos(a0), e0y = cy + mid * std::sin(a0);
    const float e1x = cx + mid * std::cos(a1), e1y = cy + mid * std::sin(a1);
    fillSdf(cx - radius, cy - radius, cx + radius, cy + radius, c, [=](float px, float py) {
        float a = std::atan2(py - cy, px - cx);
        while (a < a0) a += 2 * kPi;
        if (a <= a1) return std::abs(std::hypot(px - cx, py - cy) - mid) - half;
        return std::min(std::hypot(px - e0x, py - e0y), std::hypot(px - e1x, py - e1y)) - half;
    });
}

void Canvas::line(float x0, float y0, float x1, float y1, float thickness, Color c) {
    const float h = thickness / 2;
    fillSdf(std::min(x0, x1) - h, std::min(y0, y1) - h, std::max(x0, x1) + h, std::max(y0, y1) + h, c,
            [=](float px, float py) { return segmentDistance(px, py, x0, y0, x1, y1) - h; });
}

void Canvas::fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, float round, Color c) {
    const float v[3][2] = {{x0, y0}, {x1, y1}, {x2, y2}};
    fillSdf(std::min({x0, x1, x2}) - round, std::min({y0, y1, y2}) - round, std::max({x0, x1, x2}) + round,
            std::max({y0, y1, y2}) + round, c, [&](float px, float py) { return triangleSdf(px, py, v) - round; });
}

void Canvas::drawMask(const std::uint8_t* mask, int mw, int mh, int stride, int x, int y, Color c) {
    for (int j = 0; j < mh; ++j) {
        const int ty = y + j;
        if (ty < cy0_ || ty >= cy1_) continue;
        const std::uint8_t* row = mask + static_cast<std::ptrdiff_t>(j) * stride;
        for (int i = 0; i < mw; ++i)
            if (row[i]) blend(x + i, ty, c, row[i] / 255.f);
    }
}

void writeImage(const std::filesystem::path& path, const Canvas& canvas) {
    const int w = canvas.width(), h = canvas.height();
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::vector<std::uint8_t> out;
    if (ext == ".png") {
        std::vector<std::uint8_t> rgb(static_cast<std::size_t>(w) * h * 3);
        for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
            const std::uint32_t p = canvas.pixels()[i];
            rgb[i * 3] = p >> 16 & 255;
            rgb[i * 3 + 1] = p >> 8 & 255;
            rgb[i * 3 + 2] = p & 255;
        }
        std::size_t len = 0;
        void* png = tdefl_write_image_to_png_file_in_memory_ex(rgb.data(), w, h, 3, &len, 6, MZ_FALSE);
        if (!png) throw std::runtime_error("cannot encode " + path.string());
        out.assign(static_cast<std::uint8_t*>(png), static_cast<std::uint8_t*>(png) + len);
        mz_free(png);
    } else {
        const int stride = (w * 3 + 3) & ~3;
        const std::uint32_t size = static_cast<std::uint32_t>(stride * h);
        auto u16 = [&](std::uint32_t v) { out.push_back(v & 255); out.push_back(v >> 8 & 255); };
        auto u32 = [&](std::uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); };
        out.push_back('B'); out.push_back('M');
        u32(14 + 40 + size); u32(0); u32(14 + 40);
        u32(40); u32(static_cast<std::uint32_t>(w)); u32(static_cast<std::uint32_t>(h));
        u16(1); u16(24); u32(0); u32(size);
        u32(2835); u32(2835); u32(0); u32(0);
        for (int y = h - 1; y >= 0; --y) {
            const std::uint32_t* row = canvas.pixels() + static_cast<std::size_t>(y) * w;
            for (int x = 0; x < w; ++x) {
                out.push_back(row[x] & 255);
                out.push_back(row[x] >> 8 & 255);
                out.push_back(row[x] >> 16 & 255);
            }
            out.insert(out.end(), static_cast<std::size_t>(stride - w * 3), 0);
        }
    }
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write " + path.string());
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
}

}  // namespace reaction
