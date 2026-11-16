// What every scene can reach: the game files, the loaded fonts, the screen.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "app/Screen.h"
#include "audio/Audio.h"
#include "core/GameData.h"
#include "core/Settings.h"
#include "formats/Font.h"
#include "game/Career.h"

namespace reaction {

struct App {
    const GameData& data;
    Screen& screen;
    FontSet fonts;
    // The game in progress: the original's tables, the career, the random numbers.
    GameState game{};
    // The difficulty picked in FINAL's practice menu (0-3), -1 if none: the
    // practice mini-games use it instead of their defaults.
    int practiceLevel = -1;
    // Covert Reaction's own options (the main menu's Options) and where they're kept
    // (empty in scripted test runs: defaults, never saved).
    Settings settings{};
    std::filesystem::path settingsFile{};
    void saveSettings() const {
        if (!settingsFile.empty()) settings.save(settingsFile);
    }
    // Options bit 8 (animations) follows Settings::animations: the main menu's
    // Options and Alt-A (appHotkeys) change the setting, and the setting wins
    // where the original turns the bit back on (options |= 0Ch on a fresh start
    // or a new case, |= 8 when FINAL starts after a case). The runner calls this
    // every frame, between the scene's ticks.
    void syncAnimations() {
        auto& o = game.world.hdr.options;
        o = static_cast<std::int16_t>(settings.animations ? (o | 8) : (o & ~8));
    }

    // The sound driver (null: no sound at all, as in scripted runs). sound() is
    // play_sound(n, a, b) as FINAL and GAME call it, when options & 4 (sound on);
    // the mini-games keep their own options and call driverSound().
    audio::AudioEngine* audio = nullptr;
    // COVERT_SOUND_LOG=1 prints each call to stderr (scripted runs have no audio).
    void sound(int n, int a = 0, int b = 0) const {
        static const bool log = std::getenv("COVERT_SOUND_LOG") != nullptr;
        if (log) std::fprintf(stderr, "play_sound(%d)%s\n", n, (game.world.hdr.options & 4) ? "" : " (sound off)");
        if (game.world.hdr.options & 4) driverSound(n, a, b);
    }
    void driverSound(int n, int a = 0, int b = 0) const {
        if (audio) audio->play(n, a, b);
    }

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

}  // namespace reaction
