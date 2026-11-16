// The wiretap / car-tracer screen (BUG.EXE), over game/Wiretap.
#pragma once

#include <functional>
#include <memory>

#include "app/App.h"
#include "app/Scene.h"
#include "game/Wiretap.h"

namespace redux {

// Called when the puzzle ends (not after Alt-Q / Yes, which quits the game):
// gives the scene to switch to.
using BugDone = std::function<std::unique_ptr<Scene>(App&, const WiretapResult&)>;

std::unique_ptr<Scene> makeBugScene(App& app, const WiretapSetup& setup, BugDone onDone);

}  // namespace redux
