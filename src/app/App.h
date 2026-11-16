// What every scene can reach: the game files, the loaded fonts, the screen.
#pragma once

#include "app/Screen.h"
#include "core/GameData.h"
#include "formats/Font.h"

namespace redux {

struct App {
    const GameData& data;
    Screen& screen;
    FontSet fonts;

    // The mouse position of an event, in 320x200 frame coordinates.
    SDL_Point mouse(const SDL_Event& e) const {
        if (e.type == SDL_MOUSEMOTION) return screen.toFrame(e.motion.x, e.motion.y);
        if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP)
            return screen.toFrame(e.button.x, e.button.y);
        int x = 0, y = 0;
        SDL_GetMouseState(&x, &y);
        return screen.toFrame(x, y);
    }
};

}  // namespace redux
