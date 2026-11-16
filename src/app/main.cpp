// Covert Redux: entry point. Finds the game data, opens the window and runs scenes.
//
//   covert-redux [--data DIR] [--scene NAME] [--shot FILE --ticks N] [--input "T:KEY ..."]
//
// --shot/--ticks/--input script a run for testing: KEYs (SDL key names, e.g.
// "Down", "Return") are injected at the given ticks, and after N ticks the frame
// is saved as a BMP and the program exits.
//
// One frame of work is Runner::step(). The desktop calls it in a loop; in a browser
// (Emscripten), which can't run a blocking loop, emscripten_set_main_loop calls it
// once per animation frame. The timing is the same: fixed 70 ticks per second.
#include <SDL.h>

#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// The page (web/shell.html) shows errors and the end of the run.
EM_JS(void, redux_js_error, (const char* msg), {
    if (Module.reduxError) Module.reduxError(UTF8ToString(msg));
});
EM_JS(void, redux_js_exit, (int status), {
    if (Module.reduxExit) Module.reduxExit(status);
});
#endif

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

// Shows a fatal error: a message box on the desktop, the page in a browser (the
// web shell defines Module.reduxError), and stderr everywhere.
void reportError(const char* msg, bool headless) {
    std::fprintf(stderr, "error: %s\n", msg);
#ifdef __EMSCRIPTEN__
    (void)headless;
    redux_js_error(msg);
#else
    if (!headless) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Covert Redux", msg, nullptr);
#endif
}

struct ScriptKey {
    SDL_Keycode key;
    Uint16 mod;
};

struct Script {
    std::optional<fs::path> shot;
    long ticks = -1;
    std::multimap<long, ScriptKey> keys;
};

// "T:KEY" tokens; KEY may carry modifiers: "40:Alt+Q", "50:Shift+Tab", "60:Ctrl+C".
Script parseScript(const std::string& input) {
    Script s;
    std::istringstream in(input);
    std::string tok;
    while (in >> tok) {
        auto colon = tok.find(':');
        if (colon == std::string::npos) continue;
        std::string name = tok.substr(colon + 1);
        Uint16 mod = 0;
        for (;;) {
            auto plus = name.find('+');
            if (plus == std::string::npos || plus + 1 >= name.size()) break;
            std::string m = name.substr(0, plus);
            if (m == "Alt") mod |= KMOD_LALT;
            else if (m == "Shift") mod |= KMOD_LSHIFT;
            else if (m == "Ctrl") mod |= KMOD_LCTRL;
            else break;
            name = name.substr(plus + 1);
        }
        SDL_Keycode k = SDL_GetKeyFromName(name.c_str());
        if (k != SDLK_UNKNOWN) s.keys.emplace(std::stol(tok.substr(0, colon)), ScriptKey{k, mod});
    }
    return s;
}

void pushKey(ScriptKey sk) {
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = sk.key;
    e.key.keysym.mod = sk.mod;
    e.key.keysym.scancode = SDL_GetScancodeFromKey(sk.key);
    SDL_PushEvent(&e);
    // (No SDL_TEXTINPUT: pushing one crashes inside sdl2-compat. Scenes treat key
    // presses without a window id, like these, as typed text.)
    e.type = SDL_KEYUP;
    SDL_PushEvent(&e);
}

// Everything a run owns, so a frame can be one call (the browser's main loop
// can't keep these as locals of a loop that never returns).
class Runner {
public:
    Runner(GameData data, const std::string& sceneName, Script script, bool headless)
        : data_(std::move(data)),
          script_(std::move(script)),
          scripted_(script_.shot.has_value()),
          screen_("Covert Redux", 3, headless),
          app_{data_, screen_, FontSet::load(data_)} {
        scene_ = makeScene(app_, sceneName);
        if (!scene_) throw std::runtime_error("unknown scene: " + sceneName);
        freq_ = SDL_GetPerformanceFrequency();
        tickLen_ = freq_ / kTicksPerSecond;
        last_ = SDL_GetPerformanceCounter();
    }

    // One frame: events, the ticks that are due, draw, present, scene switch.
    // False when the run is over (quit, or the scripted shot is saved).
    bool step() {
        if (!scene_ || scene_->quit) return false;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) scene_->quit = true;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_RETURN &&
                     (e.key.keysym.mod & KMOD_ALT))
                screen_.toggleFullscreen();
            else
                scene_->handle(e);
        }
        int steps = 0;
        if (scripted_) {
            steps = 1;      // deterministic: one tick per frame
        } else {
            Uint64 now = SDL_GetPerformanceCounter();
            acc_ += now - last_;
            last_ = now;
            for (; acc_ >= tickLen_ && steps < 10; acc_ -= tickLen_) ++steps;
            if (acc_ > tickLen_ * 10) acc_ = 0;
        }
        for (int n = 0; n < steps; ++n) {
            auto [lo, hi] = script_.keys.equal_range(tick_);
            for (auto it = lo; it != hi; ++it) pushKey(it->second);
            scene_->tick();
            ++tick_;
        }
        scene_->draw(frame_);
        if (scripted_ && tick_ >= script_.ticks) {
            writeBmp(*script_.shot, frame_.image, frame_.palette);
            return false;
        }
        screen_.present(frame_.image, frame_.palette);
        if (auto nxt = scene_->next(); nxt) scene_ = std::move(nxt);
        return scene_ && !scene_->quit;
    }

private:
    GameData data_;
    Script script_;
    bool scripted_;
    Screen screen_;
    App app_;
    std::unique_ptr<Scene> scene_;
    Frame frame_;
    Uint64 freq_ = 0, tickLen_ = 0, last_ = 0, acc_ = 0;
    long tick_ = 0;
};

#ifdef __EMSCRIPTEN__
std::unique_ptr<Runner> gRunner;
bool gHeadless = false;

// The page learns the run ended (quit, scripted shot, error) through Module.reduxExit.
void finish(int status) {
    emscripten_cancel_main_loop();
    gRunner.reset();
    SDL_Quit();
    redux_js_exit(status);
}

void webFrame() {
    try {
        if (!gRunner->step()) finish(0);
    } catch (const std::exception& ex) {
        reportError(ex.what(), gHeadless);
        finish(1);
    }
}
#endif

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
    // Scripted runs are headless: no video or audio at all, just the event queue,
    // so tests never open windows or dialogs on the desktop.
    const bool headless = script.shot.has_value();
    if (SDL_Init(headless ? SDL_INIT_EVENTS : SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        reportError((std::string("SDL_Init: ") + SDL_GetError()).c_str(), headless);
        return 1;
    }
    // Audio is optional: no device (or a browser that holds audio until the first
    // click or key) must not stop the game.
    if (!headless && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        std::fprintf(stderr, "no audio: %s\n", SDL_GetError());
    auto data = GameData::locate(dataDir, bundleCandidates());
    if (!data) {
        const char* msg =
            "Covert Redux needs the files of the original Covert Action (1990).\n\n"
            "Run it with --data <directory>, set COVERT_DATA, or put the game's files in a "
            "folder named Covert next to the program.";
        reportError(msg, headless);
        SDL_Quit();
        return 1;
    }
    std::fprintf(stderr, "game data: %s\n", data->dir().string().c_str());

#ifdef __EMSCRIPTEN__
    gHeadless = headless;
    try {
        gRunner = std::make_unique<Runner>(std::move(*data), sceneName, std::move(script), headless);
    } catch (const std::exception& ex) {
        reportError(ex.what(), headless);
        finish(1);
        return 1;
    }
    // fps 0: the browser's animation frame (the 70 Hz ticks come from the clock).
    // Never returns (simulate_infinite_loop), so nothing after it runs.
    emscripten_set_main_loop(webFrame, 0, true);
    return 0;
#else
    int status = 0;
    try {
        Runner runner(std::move(*data), sceneName, std::move(script), headless);
        while (runner.step()) {}
    } catch (const std::exception& ex) {
        reportError(ex.what(), headless);
        status = 1;
    }
    SDL_Quit();
    return status;
#endif
}
