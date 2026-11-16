// The saved-game menus (FINAL/LOADGAME.C, GAME/OFFICE.C load_game_menu,
// list_save_files, save_game_menu, write_save_file) and FINAL main's "Load a Saved
// Game". The files and their format are src/game/SaveGame.*.
#include <memory>

#include "app/CityLoop.h"
#include "app/EndCase.h"
#include "app/FrontEnd.h"
#include "app/LoadGame.h"
#include "app/Scenes.h"
#include "app/ScriptScene.h"
#include "app/UserFiles.h"
#include "game/SaveGame.h"

namespace reaction {

namespace {

using Slots = std::array<std::optional<std::vector<std::uint8_t>>, savegame::kSlots>;
using Then = std::function<std::unique_ptr<Scene>(const Frame&)>;

std::shared_ptr<Slots> readSlots(const App& app) {
    return std::make_shared<Slots>(savegame::readSlots(userDataDir(), app.data.dir()));
}

// GAME resumed from a saved game (FINAL main: ENV[0] = 0, ENV[1] = 1, ENV[7] = 0,
// ENV[15] = 0, save_env, then GAME's main with ENV[1] != 0: difficulty =
// ENV+50h, last_exit = 0, draw_status_bar, advance_time(0), the city loop).
std::unique_ptr<Scene> resumeCityLoop(App& app, const Frame& last) { return makeCityResumeScene(app, last); }

}  // namespace

std::unique_ptr<Scene> makeLoadGameScene(App& app, const Frame& last,
                                         std::function<std::unique_ptr<Scene>(bool, const Frame&)> then) {
    auto done = std::make_shared<std::function<std::unique_ptr<Scene>(bool, const Frame&)>>(std::move(then));
    return std::make_unique<ScriptScene>(app, last, [done](ScriptScene& s) {
        // (select_drive leaves the screen cleared to 0)
        s.screen().fill(0);
        auto slots = readSlots(s.app());
        auto end = [done](bool loaded) {
            return ScriptScene::finishStep([done, loaded](const Frame& f) { return (*done)(loaded, f); });
        };
        s.insert({ScriptScene::menuStep(savegame::slotMenu(true, *slots), 4, 16, [&s, slots, end](int r) {
            const int slot = r >= 0 ? r / 3 : -1;
            if (slot < 0 || slot >= savegame::kSlots || !(*slots)[static_cast<std::size_t>(slot)]) {
                s.insert({end(false)});
                return;
            }
            if (!savegame::loadBytes(s.app().game, *(*slots)[static_cast<std::size_t>(slot)])) {
                // load_env: "File Error: name"
                s.insert({ScriptScene::menuStep("File Error: " + savegame::slotFileName(slot) + "\n", 100, 80),
                          end(false)});
                return;
            }
            s.insert({end(true)});
        })});
    });
}

std::unique_ptr<Scene> makeSaveGameScene(App& app, int defaultSlot, const Frame& last, Then then) {
    auto done = std::make_shared<Then>(std::move(then));
    return std::make_unique<ScriptScene>(app, last, [done, defaultSlot](ScriptScene& s) {
        s.screen().fill(0);
        auto slots = readSlots(s.app());
        auto end = ScriptScene::Step([done](ScriptScene& s2) {
            s2.screen().fill(5);   // gfx_fill_rect(g_gfx, 0, 0, 320, 200, 5)
            s2.insert({ScriptScene::finishStep([done](const Frame& f) { return (*done)(f); })});
        });
        s.insert({ScriptScene::menuStep(
            savegame::slotMenu(false, *slots), 4, 16,
            [&s, end](int r) {
                const int slot = r >= 0 ? r / 3 : -1;
                if (slot < 0 || slot >= savegame::kSlots) {
                    s.insert({end});
                    return;
                }
                const std::string name = savegame::slotFileName(slot);
                const auto bytes = savegame::saveBytes(s.app().game);
                // write_save_file: the file is opened first ("File Error" if it
                // can't be), then "... save in progress." while save_env writes it.
                const int err = savegame::writeSlot(userDataDir(), slot, bytes);
                if (err == -1) {
                    s.insert({ScriptScene::menuStep("File Error: " + name + "\n", 100, 80), end});
                    return;
                }
                std::string t = err == 0 ? "Game has been saved.\n" : "Game NOT saved.\n";
                if (err == 13) t += "Write access denied.\n";
                if (err == 28) t += "Disk Full.\n";
                t += "Press key to continue.\n";
                s.insert({ScriptScene::pausedStep(name + "\n ... save in progress.\n", 4, 100),
                          ScriptScene::menuStep(t, 4, 128), end});
            },
            false, defaultSlot)});
    });
}

std::unique_ptr<Scene> makeFinalLoadGameScene(App& app, const Frame& last) {
    const auto saved = app.game.world.hdr.options;
    return makeLoadGameScene(app, last, [&app, saved](bool loaded, const Frame& f) -> std::unique_ptr<Scene> {
        if (!loaded) return makeMainMenuSceneOver(app, f.image);
        auto& h = app.game.world.hdr;
        h.options = static_cast<std::int16_t>((saved & 0x10) | (h.options & ~0x10));
        app.game.briefed = false;
        if (savegame::savedBetweenCases(app.game)) return makeNextCaseScene(app, f);
        return resumeCityLoop(app, f);
    });
}

REACTION_SCENE("load-game", [](App& app) -> std::unique_ptr<Scene> {
    Frame f;
    f.image = Image(320, 200, 0);
    return makeFinalLoadGameScene(app, f);
});

// The save menu on its own, saving a case played by the simulation (a save
// between cases, as the debriefing's "Save Game" makes).
REACTION_SCENE("save-game", [](App& app) -> std::unique_ptr<Scene> {
    makeDemoFinishedCase(app.game, app.data, 1990);
    app.game.world.hdr.options = static_cast<std::int16_t>(app.game.world.hdr.options | 0x40);
    Frame f;
    f.image = Image(320, 200, 0);
    return makeSaveGameScene(app, 0, f, [&app](const Frame& last) { return makeMainMenuSceneOver(app, last.image); });
});

}  // namespace reaction
