// The setup screen: where the game files are and, optionally, the MT-32 ROMs.
// The game files are chosen as a folder that holds them or as one of the
// releases' installers, archives or disk images, and imported either way into
// the per-user folder's `data/game` (SetupConfig::importDir; formats/GameImport.h),
// which the game then uses. Shown at start-up when the game files aren't
// found (first run, or the saved folder is gone), and from the main menu's
// Options ("Game files and ROMs...").
//
// It's a launcher-style screen of Covert Reaction's own, nothing of the game's:
// drawn in true colour at the window's real pixel size (Scene::fullResolution,
// core/Canvas.h) with the system's UI font (core/UiFont.h), HiDPI-aware, laid
// out again for any window size. Two cards, "Game files" (required) and "MT-32
// ROMs" (optional), each with a status icon, a status line and its buttons;
// START ("Done" from the Options), greyed until the game files are there, and
// Quit ("Cancel").
//
// The buttons open the system's file dialogs (app/FileDialog.h): on macOS one
// "Choose…" (its panel takes a file or a folder), elsewhere "Choose installer
// or archive…" and "Choose folder…". Where there's no dialog (Linux without
// zenity or kdialog) the in-game browser (PickerScene) opens instead. A game
// source chosen is imported at once, on a thread (a progress bar meanwhile).
//
// Keys: Tab / Shift+Tab or the arrows move the focus (a ring shows it), Enter
// or Space presses, Esc is Quit / Cancel. The mouse: hover, click.
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

class UiFont;

struct SetupConfig {
    std::filesystem::path gameDir;   // the game folder so far (may be empty, or not hold the game)
    std::filesystem::path romDir;    // the MT-32 ROM folder chosen so far (empty: none)
    // The usual places the ROMs are looked for (audio::mt32RomDirs without the
    // chosen folder), for a game folder: what's found there shows when no ROM
    // folder is chosen. Null: none.
    std::function<std::vector<std::filesystem::path>(const std::filesystem::path& gameDir)> autoRomDirs;
    // The system's file dialogs may open (false in scripted runs: there only
    // COVERT_TEST_PICK's paths, or the in-game browser, choose).
    bool dialogs = true;
    std::filesystem::path browserStart;   // PickerScene's first folder for the game files (COVERT_PICKER_DIR)
    bool inGame = false;                  // from the Options: "Done" / "Cancel"
    // Where the chosen game files are imported (the per-user folder's `data/game`).
    // Empty: no import, the folder chosen is used as it is.
    std::filesystem::path importDir;
    // Where the chosen MT-32 ROMs are copied (the per-user folder's `data/mt32`: the
    // best control + PCM pair, from a folder, a zip or a 7z; audio::importMt32Roms).
    // Empty: the folder chosen is used as it is.
    std::filesystem::path romImportDir;
    bool syncImport = false;              // import without a thread (scripted runs: reproducible)
};

class SetupScene : public Scene {
public:
    // In game (config.inGame), `done` gives the scene that follows Done
    // (accepted) or Cancel. Otherwise the runner watches accepted(), and Quit
    // sets `quit`.
    using Done = std::function<std::unique_ptr<Scene>(bool accepted, const SetupScene&)>;
    SetupScene(const Screen& screen, SetupConfig config, Done done = {});
    ~SetupScene() override;

    void handle(const SDL_Event& e) override;
    void tick() override;
    void draw(Frame& frame) override;   // (only while the in-game browser is open)
    bool fullResolution() const override { return !picker_; }
    bool drawCanvas(Canvas& canvas) override;
    std::unique_ptr<Scene> next() override;

    bool accepted() const { return accepted_; }   // START / Done
    const std::filesystem::path& gameDir() const { return game_; }
    const std::filesystem::path& romDir() const { return roms_; }
    bool gameChanged() const { return game_ != config_.gameDir || imported_; }
    bool importing() const { return job_ != nullptr; }

private:
    enum class Action { ChooseGame, ChooseGameFile, ChooseGameFolder, ChooseRoms, ChooseRomFile, ChooseRomFolder, ClearRoms, Quit, Start };
    struct Button {
        Action action;
        std::string label;
        bool primary = false;
        RectF rect{};   // where it was last drawn (canvas pixels)
    };

    void refresh();   // the cards' status, after a change
    void press(Action a);
    void choose(Action a);
    void take(PickerScene::Mode mode, const std::filesystem::path& path);
    void finishImport();   // the import job is done: take its result
    bool enabled(Action a) const;
    int indexOf(Action a) const;
    // The ROM card's buttons; the first of its choose buttons.
    static bool isRomAction(Action a) {
        return a == Action::ChooseRoms || a == Action::ChooseRomFile || a == Action::ChooseRomFolder || a == Action::ClearRoms;
    }
    int romChooseIndex() const;
    void focusNext(int step);
    int buttonAt(float x, float y) const;   // canvas pixels
    // Lays the screen out for a canvas of `w` x `h` and, with `canvas`, draws
    // it (without, it only measures); the buttons' rectangles are kept. The
    // cards start at `top` (0: right under the header). Returns the cards'
    // bottom.
    float paint(Canvas* canvas, float w, float h, float top);

    const Screen& screen_;
    SetupConfig config_;
    Done done_;
    std::filesystem::path game_, roms_;
    bool gameOk_ = false;
    std::vector<std::string> gameMissing_;   // marker files the game folder lacks
    GameFileCheck gameCheck_;                // the game folder against the known releases (gameOk_ only)
    std::optional<audio::Mt32Roms> romSet_;  // found in roms_ (or, with none chosen, the usual places)
    std::filesystem::path romFoundIn_;       // where (with none chosen)
    std::vector<Button> buttons_;            // in focus order
    int focus_ = 0, hover_ = -1, pressed_ = -1;
    bool focusVisible_ = true;               // the ring (hidden after a click, back on a key)
    bool accepted_ = false, cancelled_ = false;
    std::unique_ptr<PickerScene> picker_;    // the in-game browser, while it's open
    PickerScene::Mode pickerMode_ = PickerScene::Mode::GameFiles;
    std::unique_ptr<ImportJob> job_;         // the import under way
    std::filesystem::path importFrom_;       // what the last import was from
    std::string romError_;                   // why the last ROM import failed (empty: it didn't)
    std::filesystem::path romFrom_;          // what the ROMs were last imported from
    std::string importError_;                // why it failed (empty: it didn't)
    ImportResult lastImport_{};              // the last import that worked
    bool imported_ = false;                  // the game files were imported here
    // Drawing.
    const UiFont* regular_ = nullptr;
    const UiFont* bold_ = nullptr;
    bool dirty_ = true;
    int drawnW_ = 0, drawnH_ = 0;
    float drawnScale_ = 0;
    float layoutScale_ = 1;   // the scale paint lays out at (smaller than the UI scale if the window is too small)
    float footerTop_ = 0;     // (paint: where the footer goes if the content leaves room)
    float neededH_ = 0;       // (paint: the height it all needs)
    long ticks_ = 0;
};

}  // namespace reaction
