#include "app/Screen.h"

#include <stdexcept>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

EM_JS(void, reaction_js_toggle_fullscreen, (), {
    if (Module.reactionToggleFullscreen) Module.reactionToggleFullscreen();
});
#endif

namespace reaction {

namespace {
#ifdef __EMSCRIPTEN__
// In a browser the canvas is the 320x200 frame itself; the page stretches it to
// 4:3 with CSS (image-rendering: pixelated), so the browser does the nearest-
// neighbour scaling and SDL reports mouse positions in canvas pixels.
constexpr int kLogicalW = 320, kLogicalH = 200;
#else
// 320x200 is shown at 4:3 (the pixels of mode 13h are taller than wide).
constexpr int kLogicalW = 320 * 3, kLogicalH = 240 * 3;
#endif
}

Screen::Screen(const char* title, int scale, bool hidden) : headless_(hidden) {
    if (headless_) return;   // scripted test run: no window, frames are only saved
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
#ifdef __EMSCRIPTEN__
    (void)scale;
    window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               kWidth, kHeight, 0);
    if (!window_) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
    // (No PRESENTVSYNC: the browser's main loop already runs on animation frames.)
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
#else
    window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               320 * scale, 240 * scale,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
#endif
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
    if (headless_) return;
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
    if (headless_) return;
#ifdef __EMSCRIPTEN__
    // The page puts its game container (not the bare canvas) in full screen, so its
    // CSS keeps the 4:3 letterbox (Module.reactionToggleFullscreen, web/shell.html).
    reaction_js_toggle_fullscreen();
    (void)fullscreen_;
#else
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(window_, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
#endif
}

SDL_Point Screen::toFrame(int wx, int wy) const {
    if (headless_) return {wx, wy};
    float lx = 0, ly = 0;
    SDL_RenderWindowToLogical(renderer_, wx, wy, &lx, &ly);
    return {static_cast<int>(lx * kWidth / kLogicalW), static_cast<int>(ly * kHeight / kLogicalH)};
}

}  // namespace reaction
