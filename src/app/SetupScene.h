// The setup screen: where the game files are and, optionally, the MT-32 ROMs.
// The game files are chosen as a folder that holds them or as one of the
// releases' installers, archives or disk images, and imported either way into
// the per-user folder's `data` (SetupConfig::importDir; formats/GameImport.h),
// which the game then uses. Shown at start-up when the game files aren't
// found (first run, or the saved folder is gone), and from the main menu's
// Options ("Game files and ROMs..."). It runs before the game files are loaded,
// so it draws with the built-in 8x8 font (core/BuiltinFont.h), as PickerScene.
//
// Two rows (status + a "Choose..." button each), START (greyed until the game
// files are chosen; "Done" from the Options) and Quit ("Cancel" from the
// Options). Keys: Up/Down or Tab/Shift+Tab move between the buttons, Enter or
// Space presses one, Esc quits (cancels). The mouse: hovering selects a
// button, a click presses it. "Choose..." opens the system's folder panel on
// macOS, else the in-game browser (PickerScene), then comes back here; a game
// source chosen is imported at once (on a thread: the row shows the progress,
// then the result).
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/PickerScene.h"
#include "app/Scene.h"
#include "app/Screen.h"
#include "audio/Synth.h"
#include "formats/GameImport.h"

namespace reaction {

struct SetupConfig {
    std::filesystem::path gameDir;   // the game folder so far (may be empty, or not hold the game)
    std::filesystem::path romDir;    // the MT-32 ROM folder chosen so far (empty: none)
    // The usual places the ROMs are looked for (audio::mt32RomDirs without the
    // chosen folder), for a game folder: what's found there shows when no ROM
    // folder is chosen. Null: none.
    std::function<std::vector<std::filesystem::path>(const std::filesystem::path& gameDir)> autoRomDirs;
    bool nativePanel = false;             // macOS: the system's folder panel instead of PickerScene
    std::filesystem::path browserStart;   // PickerScene's first folder for the game files (COVERT_PICKER_DIR)
    bool inGame = false;                  // from the Options: "Done" / "Cancel"
    // Where the chosen game files are imported (the per-user folder's `data`).
    // Empty: no import, the folder chosen is used as it is.
    std::filesystem::path importDir;
    bool syncImport = false;              // import without a thread (scripted runs: reproducible)
};

class SetupScene : public Scene {
public:
    // In game (config.inGame), `done` gives the scene that follows Done
    // (accepted) or Cancel. Otherwise the runner watches accepted(), and Quit
    // sets `quit`.
    using Done = std::function<std::unique_ptr<Scene>(bool accepted, const SetupScene&)>;
    SetupScene(const Screen& screen, SetupConfig config, Done done = {});

    void handle(const SDL_Event& e) override;
    void tick() override;
    void draw(Frame& frame) override;
    std::unique_ptr<Scene> next() override;

    bool accepted() const { return accepted_; }   // START / Done
    const std::filesystem::path& gameDir() const { return game_; }
    const std::filesystem::path& romDir() const { return roms_; }
    bool gameChanged() const { return game_ != config_.gameDir || imported_; }
    bool importing() const { return job_ != nullptr; }

private:
    enum Button { kChooseGame, kChooseRoms, kStart, kQuit, kButtons };

    void refresh();                 // the rows' status, after a change
    void press(int button);
    void choose(PickerScene::Mode mode);
    void take(PickerScene::Mode mode, const std::filesystem::path& dir);
    void finishImport();   // the import job is done: take its result
    bool enabled(int button) const;
    void focusNext(int step);
    int buttonAt(int x, int y) const;

    const Screen& screen_;
    SetupConfig config_;
    Done done_;
    std::filesystem::path game_, roms_;
    bool gameOk_ = false;
    std::vector<std::string> gameMissing_;   // marker files the game folder lacks
    std::optional<audio::Mt32Roms> romSet_;  // found in roms_ (or, with none chosen, the usual places)
    std::filesystem::path romFoundIn_;       // where (with none chosen)
    int focus_ = kChooseGame;
    bool accepted_ = false, cancelled_ = false;
    std::unique_ptr<PickerScene> picker_;    // the in-game browser, while it's open
    PickerScene::Mode pickerMode_ = PickerScene::Mode::GameFiles;
    std::unique_ptr<ImportJob> job_;         // the import under way
    std::filesystem::path importFrom_;       // what the last import was from
    std::string importError_;                // why it failed (empty: it didn't)
    bool imported_ = false;                  // the game files were imported here
};

}  // namespace reaction
