// The scene registry: makeScene(app, "start") gives the first scene; other names
// open a scene directly (for development and the scripted tests). Each scene file
// registers its scenes with REACTION_SCENE, so new screens add files, not edits.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "app/App.h"
#include "app/Scene.h"

namespace reaction {

using SceneFactory = std::function<std::unique_ptr<Scene>(App&)>;

std::map<std::string, SceneFactory>& sceneRegistry();
std::unique_ptr<Scene> makeScene(App& app, const std::string& name);

struct SceneRegistrar {
    SceneRegistrar(const std::string& name, SceneFactory f) { sceneRegistry()[name] = std::move(f); }
};

}  // namespace reaction

#define REACTION_SCENE_CONCAT2(a, b) a##b
#define REACTION_SCENE_CONCAT(a, b) REACTION_SCENE_CONCAT2(a, b)
// REACTION_SCENE("name", [](App& app) { return std::make_unique<MyScene>(app); })
#define REACTION_SCENE(name, ...) \
    static ::reaction::SceneRegistrar REACTION_SCENE_CONCAT(scene_reg_, __LINE__)(name, __VA_ARGS__)
