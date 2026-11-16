// reaction-tool [--data DIR] COMMAND [ARGS...]
#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "Tool.h"

namespace reaction::tool {
std::map<std::string, Command>& registry() {
    static std::map<std::string, Command> r;
    return r;
}
}  // namespace reaction::tool

using namespace reaction;

REACTION_COMMAND("ls", "ls: list the game directory's files with their sizes",
              [](const GameData& d, const std::vector<std::string>&) {
                  for (const auto& e : std::filesystem::directory_iterator(d.dir()))
                      if (e.is_regular_file())
                          std::printf("%8ju  %s\n", static_cast<std::uintmax_t>(e.file_size()),
                                      e.path().filename().string().c_str());
                  return 0;
              });

int main(int argc, char** argv) {
    std::optional<std::filesystem::path> dir;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "--data" || a == "-d") && i + 1 < argc) dir = argv[++i];
        else args.push_back(a);
    }
    auto& reg = tool::registry();
    if (args.empty() || !reg.count(args[0])) {
        std::fprintf(stderr, "usage: reaction-tool [--data DIR] COMMAND [ARGS...]\n\ncommands:\n");
        for (const auto& [name, c] : reg) std::fprintf(stderr, "  %s\n", c.help.c_str());
        return 2;
    }
    if (!reg[args[0]].needsData) {
        try {
            return reg[args[0]].run(GameData(std::filesystem::path()), std::vector<std::string>(args.begin() + 1, args.end()));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 1;
        }
    }
    auto data = GameData::locate(dir, {});
    if (!data) {
        std::fprintf(stderr, "game data not found: use --data DIR or COVERT_DATA\n");
        return 2;
    }
    try {
        return reg[args[0]].run(*data, std::vector<std::string>(args.begin() + 1, args.end()));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
