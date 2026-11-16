#include "app/Screen.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace redux {

namespace {
// 320x200 is shown at 4:3 (the pixels of mode 13h are taller than wide).
constexpr int kLogicalW = 320 * 3, kLogicalH = 240 * 3;
}

Screen::Screen(const char* title, int scale) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               320 * scale, 240 * scale,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, 0);
    if (!renderer_) throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());
    SDL_RenderSetLogicalSize(renderer_, kLogicalW, kLogicalH);
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                 kWidth, kHeight);
    if (!texture_) throw std::runtime_error(std::string("SDL_CreateTexture: ") + SDL_GetError());
}

Screen::~Screen() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
}

void Screen::present(const Image& frame, const Palette& pal) {
    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(texture_, nullptr, &pixels, &pitch) == 0) {
        std::uint32_t lut[256];
        for (int i = 0; i < 256; ++i) {
            const Rgb& c = pal.colors[static_cast<std::size_t>(i)];
            lut[i] = 0xFF000000u | static_cast<std::uint32_t>(c.r) << 16 |
                     static_cast<std::uint32_t>(c.g) << 8 | c.b;
        }
        for (int y = 0; y < kHeight && y < frame.height(); ++y) {
            auto* dst = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(pixels) + y * pitch);
            const std::uint8_t* src = frame.row(y);
            for (int x = 0; x < kWidth && x < frame.width(); ++x) dst[x] = lut[src[x]];
        }
        SDL_UnlockTexture(texture_);
    }
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

void Screen::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(window_, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

SDL_Point Screen::toFrame(int wx, int wy) const {
    float lx = 0, ly = 0;
    SDL_RenderWindowToLogical(renderer_, wx, wy, &lx, &ly);
    return {static_cast<int>(lx * kWidth / kLogicalW), static_cast<int>(ly * kHeight / kLogicalH)};
}

}  // namespace redux
