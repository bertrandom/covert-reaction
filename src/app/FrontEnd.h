// Title sequence and main menu (FINAL.EXE main()).
#pragma once

#include <memory>

#include "app/App.h"
#include "app/Scene.h"

namespace redux {

std::unique_ptr<Scene> makeTitleScene(App& app);
std::unique_ptr<Scene> makeMainMenuScene(App& app);

}  // namespace redux
