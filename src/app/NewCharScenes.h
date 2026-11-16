// The new character, the plot briefing, the leader-face line-up and the Hall of
// Fame (FINAL: NEWCHAR.C, FAME.C). Registered scenes: "new-character",
// "hall-of-fame", "leader-faces" (a line-up drawn on the spot), "fame-demo"
// (sample entries, nothing saved).
#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "app/App.h"
#include "app/Scene.h"
#include "core/Image.h"
#include "game/CaseGen.h"

namespace reaction {

// What comes after a scene: given the last screen and palette, the next scene.
using SceneThen = std::function<std::unique_ptr<Scene>(const Frame& last)>;

// create_new_character (gender, code name, difficulty, training), then the plot
// briefing and the episode (plot_briefing, case_intro_story). Ends with the
// career, difficulty and skills in app.game, ready for generate_case.
std::unique_ptr<Scene> makeNewCharacterScene(App& app, SceneThen then);

// show_hall_of_fame(-1) after load_hall_of_fame; with `insertPoints`,
// hall_of_fame_insert(points) from app.game's career and save_hall_of_fame.
std::unique_ptr<Scene> makeHallOfFameScene(App& app, std::optional<int> insertPoints, SceneThen then);

// identify_leader_faces with a line-up drawn by drawLeaderLineup (CaseGen.h).
// `then` gets whether the player may go on: the right face, or any answer with
// app.game.copyProtection off (the GOG release's patched check).
std::unique_ptr<Scene> makeLeaderFacesScene(App& app, LeaderLineup lineup,
                                            std::function<std::unique_ptr<Scene>(bool passed, const Frame& last)> then);

// A wrong face in the line-up (with copy protection on): case_briefing's
// "not quite up to speed" over briefing.pan, then FINAL main's answer to it:
// difficulty 0, training_screen(1) (which practice) and start_practice: the
// practice mini-game, which ends in the main menu. The case is dropped.
std::unique_ptr<Scene> makeLineupFailedScene(App& app, const Frame& last);

}  // namespace reaction
