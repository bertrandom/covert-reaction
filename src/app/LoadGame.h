// The saved-game menus (covert-decomp FINAL/LOADGAME.C, GAME/OFFICE.C
// load_game_menu, save_game_menu): four slots, cv0.sve .. cv3.sve, kept in the
// per-user folder (UserFiles; an original save in the game directory loads too).
// The logic and the file format are src/game/SaveGame.*. The original first asks
// for the drive with the save disk (select_drive); Covert Reaction has no drives to
// ask about and goes straight to the slots.
#pragma once

#include <functional>
#include <memory>

#include "app/App.h"
#include "app/Scene.h"

namespace reaction {

// load_game_menu: the slots, then load_env of the chosen one into app.game
// (options &= ~80h). `then(loaded, last)`; loaded is false on Esc, an empty slot
// or a file that can't be read. GAME's hotel uses it as is (and then keeps its
// joystick option and runs advance_time(0)).
std::unique_ptr<Scene> makeLoadGameScene(App& app, const Frame& last,
                                         std::function<std::unique_ptr<Scene>(bool loaded, const Frame&)> then);

// save_game_menu: the slots (`defaultSlot` highlighted: default_sel = save_slot),
// save_env of app.game into the chosen one, "Game has been saved." / "Game NOT
// saved.", then the screen cleared to colour 5 and `then`. The caller does what
// the original does first: EndOfCase::beforeSave between cases, or
// savegame::beforeCitySave at the hotel.
std::unique_ptr<Scene> makeSaveGameScene(App& app, int defaultSlot, const Frame& last,
                                         std::function<std::unique_ptr<Scene>(const Frame&)> then);

// FINAL's main menu, "Load a Saved Game": the load menu; back to the main menu if
// nothing was loaded; a game saved between cases goes on to the next case's
// briefing (makeNextCaseScene), any other back into the city loop.
std::unique_ptr<Scene> makeFinalLoadGameScene(App& app, const Frame& last);

}  // namespace reaction
