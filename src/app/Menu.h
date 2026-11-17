// A text window / menu with the original's interaction (TEXTWIN.C text_window):
// Up/Down (and keypad 8/2) move the highlight, Enter/Space/F1 choose, Esc
// cancels (-1); the chosen item flashes white for 20 retraces before the menu
// closes. Keys only, as in the original (which has no mouse support).
//
// With hotkeys (setHotkeys), the window also takes text_window's Alt keys:
// Alt-A, Alt-S, Alt-J toggle options bits 8, 4, 10h (animations, sound,
// joystick) and play sound 20 if the bit went on, 19 if off; Alt-Q asks
// "Are you sure you want to Quit?" at (x, y & 0FEh): Yes quits, No closes this
// window as Esc does (-1).
#pragma once

#include <SDL.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "core/Image.h"
#include "formats/Font.h"
#include "formats/TextWindow.h"

namespace reaction {

struct App;
class Scene;

// What text_window's Alt keys act on. Empty functions: the Alt keys do nothing
// (a Menu without hotkeys, as before).
struct TextWindowHotkeys {
    // The options word (as a signed 16-bit value: text_window compares
    // (int)options > z to choose between sounds 20 and 19) and how to store it.
    std::function<int()> options;
    std::function<void(int)> setOptions;
    // play_sound(n): the caller keeps the original's options & 4 check (so
    // turning sound off is silent, turning it on plays 20).
    std::function<void(int)> sound;
    // Alt-Q, Yes: ENV[0] = -1, set_exit_state, exit (the game ends).
    std::function<void()> quit;
};

// FINAL's and GAME's hotkeys: app.game.world.hdr.options (Alt-A also changes
// and saves Settings::animations), App::sound, and `scene.quit = true` for
// Alt-Q's Yes. `scene` is the scene showing the menu (it must outlive it).
TextWindowHotkeys appHotkeys(App& app, Scene& scene);

class Menu {
public:
    // `text`: lines ended by '\n'; lines starting with a space are items.
    Menu(const Font& font, std::string text, int x, int y, TextWindowOptions opt = {});

    // Turns on text_window's Alt keys (see TextWindowHotkeys).
    Menu& setHotkeys(TextWindowHotkeys h);

    // Keys only: the original has no mouse support.
    void handle(const SDL_Event& e);
    void tick();
    void draw(Image& frame) const;

    // The chosen item (0-based) or -1 (Esc / no items), once the menu is done.
    std::optional<int> result() const { return done_ ? std::optional<int>(result_) : std::nullopt; }
    const TextWindowLayout& layout() const { return layout_; }
    // The highlighted item, -1 if none.
    int selection() const { return sel_; }

    // The Alt-Q window is open: text_window sets anim_active = 0 for it, so a
    // scene playing an animation behind this window holds it while this is true.
    bool confirmingQuit() const { return confirm_ && !confirmClosed_; }

private:
    void choose(int item);
    void toggle(int bit);

    const Font* font_;
    std::string text_;
    int x_, y_;
    TextWindowOptions opt_;
    TextWindowLayout layout_;
    int sel_;
    int flash_ = -1;          // retraces left of the chosen item's flash
    int result_ = -1;
    bool done_ = false;
    TextWindowHotkeys hotkeys_;
    // Alt-Q's "Are you sure" window (a text_window of its own, with the same
    // hotkeys). After No it stays drawn: the original leaves it on the screen.
    std::shared_ptr<Menu> confirm_;
    bool confirmClosed_ = false;
};

}  // namespace reaction
