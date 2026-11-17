// Covert Reaction's Options window (core/Settings.h), not in the original: from
// the main menu's "Options", and from anywhere in the game with the ` key (the
// key left of 1; main.cpp's Runner holds the scene that was running, so the game
// waits, and shows this over its last frame). Choosing an item changes it and
// saves; "Done", Esc or ` closes it. "Game files and ROMs..." (desktop only)
// opens the setup screen (SetupScene) and comes back here.
#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "app/App.h"
#include "app/Menu.h"
#include "app/Scene.h"

namespace reaction {

class OptionsScene : public Scene {
public:
    // What follows when the window closes; null (or a callback returning null):
    // nothing, closed() turns true (the Runner then resumes the scene it held).
    using Done = std::function<std::unique_ptr<Scene>()>;

    // `background`: the screen to draw over. `overlay`: it's another scene's
    // frame, in any palette: it's shown dimmed (unless !dim: it already is), in
    // the game's palette (which the window's colours need), and ` closes it.
    OptionsScene(App& app, Frame background, Done done, bool overlay, int selected = 0, bool dim = true);

    void handle(const SDL_Event& e) override;
    void tick() override;
    void draw(Frame& f) override;
    std::unique_ptr<Scene> next() override { return std::move(next_); }

    bool closed() const { return closed_; }

    // The key that opens (and closes) the window: ` (by key or by position).
    static bool isOptionsKey(const SDL_Event& e);

private:
    void open(int selected);
    void change(int item);
    void close();
    void openSetup();
    int soundIndex() const;

    App& app_;
    Frame background_;
    Done done_;
    bool overlay_;
    std::optional<Menu> menu_;
    std::unique_ptr<Scene> next_;
    bool closed_ = false;
};

}  // namespace reaction
