// The scene registry: makeScene(app, "start") gives the first scene; other names
// open a scene directly (for development and the scripted tests). Each scene file
// registers its scenes with REDUX_SCENE, so new screens add files, not edits.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "app/App.h"
#include "app/Scene.h"

namespace redux {

using SceneFactory = std::function<std::unique_ptr<Scene>(App&)>;

std::map<std::string, SceneFactory>& sceneRegistry();
std::unique_ptr<Scene> makeScene(App& app, const std::string& name);

struct SceneRegistrar {
    SceneRegistrar(const std::string& name, SceneFactory f) { sceneRegistry()[name] = std::move(f); }
};

}  // namespace redux

#define REDUX_SCENE_CONCAT2(a, b) a##b
#define REDUX_SCENE_CONCAT(a, b) REDUX_SCENE_CONCAT2(a, b)
// REDUX_SCENE("name", [](App& app) { return std::make_unique<MyScene>(app); })
#define REDUX_SCENE(name, ...) \
    static ::redux::SceneRegistrar REDUX_SCENE_CONCAT(scene_reg_, __LINE__)(name, __VA_ARGS__)
