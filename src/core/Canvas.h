// A small true-colour software canvas for the screens Covert Reaction draws
// itself, at the window's real pixel size (the setup screen, app/SetupScene.h),
// as opposed to the game's 320x200 indexed frames (core/Image.h). Anti-aliased
// shapes: every shape is a signed distance function, and a pixel's coverage is
// its distance to the edge (0.5 px either side). No SDL here.
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace reaction {

struct Color {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;

    static constexpr Color hex(std::uint32_t rgb, std::uint8_t alpha = 255) {
        return {static_cast<std::uint8_t>(rgb >> 16 & 255), static_cast<std::uint8_t>(rgb >> 8 & 255),
                static_cast<std::uint8_t>(rgb & 255), alpha};
    }
    constexpr Color withAlpha(std::uint8_t alpha) const { return {r, g, b, alpha}; }
    // Linear mix towards `o` (t in [0, 1]); alpha too.
    Color mix(Color o, float t) const;
};

struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    RectF inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};

class Canvas {
public:
    Canvas() = default;
    Canvas(int width, int height) { resize(width, height); }

    int width() const { return w_; }
    int height() const { return h_; }
    // Opaque pixels, 0xAARRGGBB (SDL_PIXELFORMAT_ARGB8888), rows top first.
    const std::uint32_t* pixels() const { return px_.data(); }
    void resize(int width, int height);

    // Drawing is clipped to this rectangle (and the canvas).
    void setClip(RectF r);
    void resetClip();

    void clear(Color c);
    // Top to bottom, dithered (no banding on big areas).
    void verticalGradient(Color top, Color bottom);
    // A soft round glow: `c` at the centre fading to nothing at `radius`.
    void radialGlow(float cx, float cy, float radius, Color c);

    void fillRect(RectF r, Color c);
    void fillRoundRect(RectF r, float radius, Color c);
    void strokeRoundRect(RectF r, float radius, float thickness, Color c);   // inside the rectangle
    void fillCircle(float cx, float cy, float radius, Color c);
    void strokeCircle(float cx, float cy, float radius, float thickness, Color c);
    // An arc of a circle (angles in radians, clockwise from 3 o'clock, a0 < a1), round ends.
    void strokeArc(float cx, float cy, float radius, float a0, float a1, float thickness, Color c);
    // A line with round caps.
    void line(float x0, float y0, float x1, float y1, float thickness, Color c);
    // A filled triangle with rounded corners (`round` px).
    void fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2, float round, Color c);

    // Blends `c` through an 8-bit coverage mask (glyphs) at (x, y).
    void drawMask(const std::uint8_t* mask, int mw, int mh, int stride, int x, int y, Color c);
    // One pixel, `coverage` of `c` (in [0, 1]).
    void blend(int x, int y, Color c, float coverage);

private:
    template <class Sdf>
    void fillSdf(float x0, float y0, float x1, float y1, Color c, Sdf sdf);

    int w_ = 0, h_ = 0;
    std::vector<std::uint32_t> px_;
    int cx0_ = 0, cy0_ = 0, cx1_ = 0, cy1_ = 0;   // clip, [x0, x1)
};

// Saves the canvas: PNG if `path` ends in .png, else a 24-bit BMP. Throws on failure.
void writeImage(const std::filesystem::path& path, const Canvas& canvas);

}  // namespace reaction
