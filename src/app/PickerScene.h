// The in-game folder browser the setup screen (SetupScene) opens to choose the
// game files' folder (the one with TEXT.DTA) or the MT-32 ROMs' folder, where
// the system's own panel isn't used (Windows, Linux; macOS in tests). It runs
// before the game files are loaded, so it draws with the built-in 8x8 font
// (core/BuiltinFont.h) in the game's palette, and it needs no App.
//
// Keys: Up/Down/PgUp/PgDn/Home/End move, Enter opens the selected folder (or
// takes it, if it holds what's asked for), Backspace goes up, U uses the
// folder (the selected one if it's marked, else the current one), Esc goes
// back without a choice (`quit`). The mouse: the wheel scrolls, a click
// selects, a second click opens.
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
    // What is being looked for: the game files (GameData::isGameDir) or a
    // usable MT-32 control + PCM ROM pair (audio::findMt32Roms).
    enum class Mode { GameFiles, Mt32Roms };

    // `start`: the folder the browser opens in (the home folder if it's missing).
    PickerScene(const Screen& screen, std::filesystem::path start, Mode mode = Mode::GameFiles);

    void handle(const SDL_Event& e) override;
    void draw(Frame& frame) override;

    // The folder the player chose, once chosen. It holds what's asked for if it
    // was taken with Enter; U takes any folder (the setup screen says what's
    // wrong with it, as it does for the system's panel).
    const std::optional<std::filesystem::path>& chosen() const { return chosen_; }

    // Whether `dir` holds what `mode` asks for.
    static bool holds(Mode mode, const std::filesystem::path& dir);

private:
    enum class Kind { Up, Folder };
    struct Item {
        Kind kind;
        std::string label;   // for display (ASCII)
        std::filesystem::path path;
        int valid = -1;      // holds what's asked for: -1 not checked yet, 0, 1
    };

    void enter(const std::filesystem::path& dir, const std::filesystem::path& select = {});
    void goUp();
    void open(int index);
    void use();
    // Whether item `index` holds what's asked for. Unless `ask`, folders macOS
    // guards (folders::needsPermission) count as not checked yet, so drawing the
    // list never triggers its permission prompts; opening or choosing one does.
    bool valid(int index, bool ask = false);
    void moveTo(int index);
    void say(std::string text, std::uint8_t color);
    int rowAt(int x, int y) const;

    const Screen& screen_;
    Mode mode_;
    std::filesystem::path cur_;   // empty: the list of drives (Windows)
    bool curValid_ = false;
    std::vector<Item> items_;
    int sel_ = 0, top_ = 0;
    std::string message_;
    std::uint8_t messageColor_ = 7;
    std::optional<std::filesystem::path> chosen_;
};

}  // namespace reaction
