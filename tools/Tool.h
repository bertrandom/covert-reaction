// redux-tool: developer commands for inspecting game resources. Each tools/cmd_*.cpp
// registers its commands with REDUX_COMMAND, so new formats add files, not edits.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/GameData.h"

namespace redux::tool {

struct Command {
    std::string help;
    // args: everything after the command name. Returns the exit status.
    std::function<int(const GameData&, const std::vector<std::string>& args)> run;
};

std::map<std::string, Command>& registry();

struct Registrar {
    Registrar(const std::string& name, Command c) { registry()[name] = std::move(c); }
};

}  // namespace redux::tool

#define REDUX_CONCAT2(a, b) a##b
#define REDUX_CONCAT(a, b) REDUX_CONCAT2(a, b)
// REDUX_COMMAND("name", "usage and help", [](const GameData& d, const auto& args) {...})
// (variadic, so lambdas with commas in their capture lists work)
#define REDUX_COMMAND(name, help, ...) \
    static ::redux::tool::Registrar REDUX_CONCAT(reg_, __LINE__)(name, ::redux::tool::Command{help, __VA_ARGS__})
