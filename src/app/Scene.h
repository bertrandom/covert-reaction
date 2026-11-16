// A screen of the game (title, menu, a puzzle, ...). The app runs one at a time.
#pragma once

#include <SDL.h>

#include <memory>

#include "core/GameData.h"
#include "core/Image.h"

namespace reaction {

struct Frame {
    Image image{320, 200};
    Palette palette{};
};

class Scene {
public:
    virtual ~Scene() = default;
    virtual void handle(const SDL_Event&) {}
    // Called at a fixed rate (kTicksPerSecond).
    virtual void tick() {}
    virtual void draw(Frame& frame) = 0;
    // Non-null: switch to that scene. A scene returns itself to keep running.
    virtual std::unique_ptr<Scene> next() { return nullptr; }
    bool quit = false;
};

// The original game is paced by the VGA retrace (70 Hz in mode 13h).
constexpr int kTicksPerSecond = 70;

}  // namespace reaction
