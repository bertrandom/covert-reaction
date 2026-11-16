// FINAL at the end of a case (ENDCASE.C end_of_case) and what its main does next
// (FINAL.C: plot_briefing when needed, case_intro_story, generate_case), as
// scenes. The logic is src/game/EndCase.*.
#pragma once

#include <memory>

#include "app/App.h"
#include "app/Scene.h"

namespace redux {

// The debriefing of the case in app.game, as GAME left it when the simulation
// ended the case (Sim::caseOver): FINAL's start (reseed, options |= 8), the
// chronology, the efficiency report, the plot's story, the clue about Mr. Big,
// the vacation, the Hall of Fame, the career, the promotion, the retirement or
// the continue/save/end menu, then the next case or the main menu.
std::unique_ptr<Scene> makeEndOfCaseScene(App& app);

// FINAL main's "resume": plot_briefing if the MasterMind was arrested (or on the
// first plot), case_intro_story, generate_case and its line-up, then the new case.
// Also where a game saved between cases goes once loaded. `last`: the screen.
std::unique_ptr<Scene> makeNextCaseScene(App& app, const Frame& last);

}  // namespace redux
