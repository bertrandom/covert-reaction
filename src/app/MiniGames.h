// The mini-game scenes with a given setup and what to do when they end (the
// city loop starts them with the case's data and goes back to GAME after).
// BugScene.h has the wiretap's.
#pragma once

#include <functional>
#include <memory>

#include "app/App.h"
#include "app/Scene.h"
#include "game/Chase.h"
#include "game/CodeBreak.h"
#include "game/Tac.h"

namespace reaction {

using ChaseDone = std::function<std::unique_ptr<Scene>(App&, const ChaseResult&)>;
std::unique_ptr<Scene> makeChaseScene(App& app, const ChaseSetup& setup, ChaseDone onDone);

using CodeDone = std::function<std::unique_ptr<Scene>(App&, const CodeOutcome&)>;
std::unique_ptr<Scene> makeCodeScene(App& app, const CodeSetup& setup, CodeDone onDone);

using TacDone = std::function<std::unique_ptr<Scene>(App&, const TacResult&)>;
std::unique_ptr<Scene> makeTacScene(App& app, TacSetup setup, std::shared_ptr<TacHooks> hooks, TacDone onDone);

}  // namespace reaction
