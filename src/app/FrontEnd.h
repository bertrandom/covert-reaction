// Title sequence and main menu (FINAL.EXE main()).
#pragma once

#include <memory>

#include "app/App.h"
#include "app/Scene.h"

namespace reaction {

std::unique_ptr<Scene> makeTitleScene(App& app);
std::unique_ptr<Scene> makeMainMenuScene(App& app);
// The main menu drawn over `background` (what the screen showed before).
std::unique_ptr<Scene> makeMainMenuSceneOver(App& app, Image background);

}  // namespace reaction
