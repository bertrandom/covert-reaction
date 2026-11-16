// Covert Redux: entry point. Finds the game data, opens the window and runs scenes.
#include <SDL.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/Scene.h"
#include "app/Screen.h"
#include "core/GameData.h"

namespace fs = std::filesystem;
using namespace redux;

namespace {

// Placeholder first scene until the title sequence exists: the default palette.
class PaletteScene : public Scene {
public:
    void handle(const SDL_Event& e) override {
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) quit = true;
    }
    void draw(Frame& f) override {
        for (int i = 0; i < 256; ++i) {
            const auto v = static_cast<std::uint8_t>(i);
            f.palette.colors[static_cast<std::size_t>(i)] = {v, static_cast<std::uint8_t>(255 - v),
                                                             static_cast<std::uint8_t>(v * 7)};
        }
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 320; ++x)
                f.image.set(x, y, static_cast<std::uint8_t>(x / 20 + y / 13 * 16));
    }
};

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

}  // namespace

int main(int argc, char** argv) {
    std::optional<fs::path> dataDir;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "--data" || a == "-d") && i + 1 < argc) dataDir = argv[++i];
    }
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
        std::unique_ptr<Scene> scene = std::make_unique<PaletteScene>();
        Frame frame;
        const Uint64 freq = SDL_GetPerformanceFrequency();
        const Uint64 tickLen = freq / kTicksPerSecond;
        Uint64 last = SDL_GetPerformanceCounter(), acc = 0;
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
            Uint64 now = SDL_GetPerformanceCounter();
            acc += now - last;
            last = now;
            for (int n = 0; acc >= tickLen && n < 10; ++n, acc -= tickLen) scene->tick();
            if (acc > tickLen * 10) acc = 0;
            scene->draw(frame);
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
