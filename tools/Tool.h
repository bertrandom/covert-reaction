// reaction-tool: developer commands for inspecting game resources. Each tools/cmd_*.cpp
// registers its commands with REACTION_COMMAND, so new formats add files, not edits.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/GameData.h"

namespace reaction::tool {

struct Command {
    std::string help;
    // args: everything after the command name. Returns the exit status.
    std::function<int(const GameData&, const std::vector<std::string>& args)> run;
};

std::map<std::string, Command>& registry();

struct Registrar {
    Registrar(const std::string& name, Command c) { registry()[name] = std::move(c); }
};

}  // namespace reaction::tool

#define REACTION_CONCAT2(a, b) a##b
#define REACTION_CONCAT(a, b) REACTION_CONCAT2(a, b)
// REACTION_COMMAND("name", "usage and help", [](const GameData& d, const auto& args) {...})
// (variadic, so lambdas with commas in their capture lists work)
#define REACTION_COMMAND(name, help, ...) \
    static ::reaction::tool::Registrar REACTION_CONCAT(reg_, __LINE__)(name, ::reaction::tool::Command{help, __VA_ARGS__})
