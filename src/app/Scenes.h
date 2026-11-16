// The scene registry: makeScene(app, "start") gives the first scene; other names
// open a scene directly (for development and the scripted tests).
#pragma once

#include <memory>
#include <string>

#include "app/App.h"
#include "app/Scene.h"

namespace redux {

std::unique_ptr<Scene> makeScene(App& app, const std::string& name);

}  // namespace redux
