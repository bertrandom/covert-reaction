// The SDL2 window: shows a 320x200 indexed frame, stretched to 4:3 like a CRT, with
// nearest-neighbour scaling.
#pragma once

#include <SDL.h>

#include "core/Image.h"

namespace redux {

class Screen {
public:
    static constexpr int kWidth = 320, kHeight = 200;

    // headless: no window or renderer at all (scripted test runs only save frames).
    Screen(const char* title, int scale, bool headless = false);
    ~Screen();
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    void present(const Image& frame, const Palette& pal);
    void toggleFullscreen();
    // Window coordinates -> 320x200 frame coordinates (for the mouse).
    SDL_Point toFrame(int wx, int wy) const;

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    bool fullscreen_ = false;
    bool headless_ = false;
};

}  // namespace redux
