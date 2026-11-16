// GAME, the city loop of a case, as a scene (CityGame runs the original's code;
// see CityGame.h), and the programs it hands over to: the mini-games with the
// case's data (TAC, CHASE, BUG, CODE), after which GAME starts again where it
// left off, and FINAL's debriefing when the case is over (makeEndOfCaseScene,
// app/EndCase.h).
//
// Registered scenes: "city" (a generated case, fixed seed: the mission briefing,
// then the city), "city-street" (the same case with the briefing done off
// screen: straight to the city menu). COVERT_CITY_SEED sets the case's seed,
// COVERT_CITY_LEVEL the difficulty (0-3), COVERT_GAME_SEED GAME's randomize()
// seed (default: the time, or fixed in the test scenes).
#pragma once

#include <memory>

#include "app/App.h"
#include "app/Scene.h"

namespace redux {

// GAME at the start of a case (ENV[1] = 0): the mission briefing, then the city
// loop. app.game holds the case generate_case made (and the career).
std::unique_ptr<Scene> makeCityNewCaseScene(App& app, const Frame& last);

// GAME resumed from a saved game: ENV[1] = 1, last_exit 0, ENV[15] = 0
// (difficulty = ENV+50h, draw_status_bar, advance_time(0)), the city loop in
// here_city.
std::unique_ptr<Scene> makeCityResumeScene(App& app, const Frame& last);

}  // namespace redux
