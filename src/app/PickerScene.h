// The first-run screen shown when the game files can't be found: what the
// program needs, and a folder browser to point at the folder with TEXT.DTA. It
// runs before the game files are loaded, so it draws with the built-in 8x8 font
// (core/BuiltinFont.h) in the game's palette, and it needs no App.
//
// Keys: Up/Down/PgUp/PgDn/Home/End move, Enter opens the selected folder (or
// takes it, if it holds the game), Backspace goes up, U uses the folder, Esc
// quits. The mouse: the wheel scrolls, a click selects, a second click opens.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "app/Scene.h"
#include "app/Screen.h"

namespace reaction {

class PickerScene : public Scene {
public:
    // `start`: the folder the browser opens in (the home folder if it's missing).
    PickerScene(const Screen& screen, std::filesystem::path start);

    void handle(const SDL_Event& e) override;
    void draw(Frame& frame) override;

    // The folder the player chose (checked to hold the game files), once chosen.
    const std::optional<std::filesystem::path>& chosen() const { return chosen_; }

private:
    enum class Kind { Up, Folder };
    struct Item {
        Kind kind;
        std::string label;   // for display (ASCII)
        std::filesystem::path path;
        int valid = -1;      // holds the game files: -1 not checked yet, 0, 1
    };

    void enter(const std::filesystem::path& dir, const std::filesystem::path& select = {});
    void goUp();
    void open(int index);
    void use();
    void choose(const std::filesystem::path& dir);
    // Whether item `index` holds the game. Unless `ask`, folders macOS guards
    // (folders::needsPermission) count as not checked yet, so drawing the list
    // never triggers its permission prompts; opening or choosing one does.
    bool valid(int index, bool ask = false);
    void moveTo(int index);
    void say(std::string text, std::uint8_t color);
    int rowAt(int x, int y) const;

    const Screen& screen_;
    std::filesystem::path cur_;   // empty: the list of drives (Windows)
    bool curValid_ = false;
    std::vector<Item> items_;
    int sel_ = 0, top_ = 0;
    std::string message_;
    std::uint8_t messageColor_ = 7;
    std::optional<std::filesystem::path> chosen_;
};

}  // namespace reaction
