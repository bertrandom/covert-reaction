// The SDL2 window: shows a 320x200 indexed frame, stretched to 4:3 like a CRT, with
// nearest-neighbour scaling. In a browser the canvas is the bare 320x200 frame and
// the page (web/shell.html) does the 4:3 scaling.
//
// Covert Reaction's own screens (the setup screen) are drawn instead in true
// colour at the window's real pixel size (presentCanvas): outputSize() is that
// size in pixels, which on a HiDPI display is pixelRatio() times the window's
// size in points (the units of mouse events).
#pragma once

#include <SDL.h>

#include "core/Canvas.h"
#include "core/Image.h"

namespace reaction {

class Screen {
public:
    static constexpr int kWidth = 320, kHeight = 200;

    // headless: no window or renderer at all (scripted test runs only save frames).
    Screen(const char* title, int scale, bool headless = false);
    ~Screen();
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    void present(const Image& frame, const Palette& pal);
    // A full-size true-colour image (outputSize()). `changed`: it differs from
    // the last one shown (else the texture isn't uploaded again).
    void presentCanvas(const Canvas& canvas, bool changed = true);
    void toggleFullscreen();
    void setFullscreen(bool on) { if (on != fullscreen_) toggleFullscreen(); }
    bool fullscreen() const { return fullscreen_; }
    // Window coordinates -> 320x200 frame coordinates (for the mouse).
    SDL_Point toFrame(int wx, int wy) const;

    // The drawable size in pixels, and pixels per point (2 on a Retina display).
    // Headless: the size set by setHeadlessSize (default 1280x800 at 1).
    SDL_Point outputSize() const;
    float pixelRatio() const;
    // Canvas pixels per UI point: pixelRatio(), or where the system doesn't
    // scale (X11, Windows without per-monitor DPI), the display's DPI / 96.
    // COVERT_UI_SCALE multiplies it.
    float uiScale() const;
    void setHeadlessSize(int width, int height, float ratio);
    SDL_Window* window() const { return window_; }
    bool headless() const { return headless_; }

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    SDL_Texture* canvasTexture_ = nullptr;   // presentCanvas's, at the canvas's size
    int canvasW_ = 0, canvasH_ = 0;
    bool canvasMode_ = false;                // the last present was presentCanvas
    bool fullscreen_ = false;
    bool headless_ = false;
    int headlessW_ = 1280, headlessH_ = 800;
    float headlessRatio_ = 1;
};

}  // namespace reaction
