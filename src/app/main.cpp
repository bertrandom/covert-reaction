// Covert Redux: entry point. Finds the game data, opens the window and runs scenes.
//
//   covert-redux [--data DIR] [--scene NAME] [--shot FILE --ticks N] [--input "T:KEY ..."]
//
// --shot/--ticks/--input script a run for testing: KEYs (SDL key names, e.g.
// "Down", "Return") are injected at the given ticks, and after N ticks the frame
// is saved as a BMP and the program exits.
#include <SDL.h>

#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "app/App.h"
#include "app/Scenes.h"
#include "app/Screen.h"
#include "core/GameData.h"

namespace fs = std::filesystem;
using namespace redux;

namespace {

std::vector<fs::path> bundleCandidates() {
    std::vector<fs::path> out;
    if (char* base = SDL_GetBasePath()) {
        fs::path b(base);
        SDL_free(base);
        out.push_back(b / "Covert");                       // next to the executable
        out.push_back(b / "resources" / "Covert");
        out.push_back(b / ".." / "Resources" / "Covert");  // inside a macOS .app
        out.push_back(b / ".." / ".." / ".." / ".." / "resources" / "Covert");  // build/ tree
    }
    if (char* pref = SDL_GetPrefPath("CovertRedux", "CovertRedux")) {
        out.push_back(fs::path(pref) / "Covert");
        SDL_free(pref);
    }
    return out;
}

struct Script {
    std::optional<fs::path> shot;
    long ticks = -1;
    std::multimap<long, SDL_Keycode> keys;
};

Script parseScript(const std::string& input) {
    Script s;
    std::istringstream in(input);
    std::string tok;
    while (in >> tok) {
        auto colon = tok.find(':');
        if (colon == std::string::npos) continue;
        SDL_Keycode k = SDL_GetKeyFromName(tok.substr(colon + 1).c_str());
        if (k != SDLK_UNKNOWN) s.keys.emplace(std::stol(tok.substr(0, colon)), k);
    }
    return s;
}

void pushKey(SDL_Keycode k) {
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = k;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(k);
    SDL_PushEvent(&e);
    e.type = SDL_KEYUP;
    SDL_PushEvent(&e);
}

}  // namespace

int main(int argc, char** argv) {
    std::optional<fs::path> dataDir;
    std::string sceneName = "start", input;
    Script script;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto arg = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--data" || a == "-d") dataDir = arg();
        else if (a == "--scene") sceneName = arg();
        else if (a == "--shot") script.shot = arg();
        else if (a == "--ticks") script.ticks = std::stol(arg());
        else if (a == "--input") input = arg();
    }
    if (!input.empty()) script.keys = parseScript(input).keys;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    auto data = GameData::locate(dataDir, bundleCandidates());
    if (!data) {
        const char* msg =
            "Covert Redux needs the files of the original Covert Action (1990).\n\n"
            "Run it with --data <directory>, set COVERT_DATA, or put the game's files in a "
            "folder named Covert next to the program.";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Covert Redux", msg, nullptr);
        std::fprintf(stderr, "%s\n", msg);
        SDL_Quit();
        return 1;
    }
    std::fprintf(stderr, "game data: %s\n", data->dir().string().c_str());

    int status = 0;
    try {
        Screen screen("Covert Redux", 3);
        App app{*data, screen, FontSet::load(*data)};
        std::unique_ptr<Scene> scene = makeScene(app, sceneName);
        if (!scene) throw std::runtime_error("unknown scene: " + sceneName);
        Frame frame;
        const Uint64 freq = SDL_GetPerformanceFrequency();
        const Uint64 tickLen = freq / kTicksPerSecond;
        Uint64 last = SDL_GetPerformanceCounter(), acc = 0;
        long tick = 0;
        const bool scripted = script.shot.has_value();
        while (scene && !scene->quit) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) scene->quit = true;
                else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RETURN &&
                         (e.key.keysym.mod & KMOD_ALT))
                    screen.toggleFullscreen();
                else
                    scene->handle(e);
            }
            int steps = 0;
            if (scripted) {
                steps = 1;      // deterministic: one tick per loop
            } else {
                Uint64 now = SDL_GetPerformanceCounter();
                acc += now - last;
                last = now;
                for (; acc >= tickLen && steps < 10; acc -= tickLen) ++steps;
                if (acc > tickLen * 10) acc = 0;
            }
            for (int n = 0; n < steps; ++n) {
                auto [lo, hi] = script.keys.equal_range(tick);
                for (auto it = lo; it != hi; ++it) pushKey(it->second);
                scene->tick();
                ++tick;
            }
            scene->draw(frame);
            if (scripted && tick >= script.ticks) {
                writeBmp(*script.shot, frame.image, frame.palette);
                break;
            }
            screen.present(frame.image, frame.palette);
            if (auto nxt = scene->next(); nxt) scene = std::move(nxt);
        }
    } catch (const std::exception& ex) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Covert Redux", ex.what(), nullptr);
        std::fprintf(stderr, "error: %s\n", ex.what());
        status = 1;
    }
    SDL_Quit();
    return status;
}
