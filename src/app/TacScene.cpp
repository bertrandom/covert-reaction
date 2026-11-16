// The building break-in (TAC.EXE) over game/Tac: the scene ticks the program
// once per retrace, turns SDL keys into the codes get_key() returns (with the
// BIOS's typematic repeat and 15-key buffer), and runs the text windows TAC asks
// for with the Menu widget.
//
// Keys (TAC forces NumLock on while Max moves):
//   keypad 1-9 (and the digit keys)            walk; with Shift: dive
//   arrows, Home End PgUp PgDn                 dive (as in the original: the grey
//                                              keys give no digit)
//   keypad 5 or '5'                            sneak on / off
//   7 8 9 / U I O / J K L                      the keypad on the letters, for
//                                              keyboards without one: walk,
//                                              with Shift dive, I sneak
//   Space fire          Enter or F1 examine (open / close, take)
//   F2 photograph       F3 plant a bug        F4 use (computer terminal)
//   F5 F6 F7 throw a grenade (short / medium / long fuse)
//   F8 detonate remote-controlled traps       F9 set a trap   F10 next grenade kind
//   s d f  slow / normal / fast   Alt-S sound   Alt-P pause   Alt-Q quit
//
// Registered scene: "tac", a practice break-in. For testing, REACTION_TAC_CITY,
// REACTION_TAC_ORG, REACTION_TAC_SEED (the game seed), REACTION_TAC_DIFFICULTY (0-3),
// REACTION_TAC_RUNSEED (the srand seed after the building, default the time),
// REACTION_TAC_MODE (ENV[0]: 7 kidnap, 8 rescue, 10 bomb; not practice then),
// REACTION_TAC_FEMALE=1 and REACTION_TAC_HOUR set the setup.
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <optional>

#include "app/Keypad.h"
#include "app/Menu.h"
#include "app/MiniGames.h"
#include "app/Scenes.h"
#include "game/Tac.h"
#include "game/World.h"

namespace reaction {

namespace {

bool isModifier(SDL_Keycode k) {
    switch (k) {
    case SDLK_LSHIFT: case SDLK_RSHIFT: case SDLK_LCTRL: case SDLK_RCTRL: case SDLK_LALT: case SDLK_RALT:
    case SDLK_LGUI: case SDLK_RGUI: case SDLK_CAPSLOCK: case SDLK_NUMLOCKCLEAR: case SDLK_SCROLLLOCK: case SDLK_MODE:
        return true;
    default:
        return false;
    }
}

// The get_key() code of a key press; -1 for none.
int tacKey(const SDL_Keysym& k, bool numlock) {
    const bool shift = k.mod & KMOD_SHIFT;
    if (k.mod & KMOD_ALT) {
        switch (k.sym) {
        case SDLK_s: return 0x1F00;
        case SDLK_p: return 0x1900;
        case SDLK_q: return 0x1000;
        default: return -1;
        }
    }
    if (k.sym >= SDLK_F1 && k.sym <= SDLK_F10) return 0x3B00 + static_cast<int>(k.sym - SDLK_F1) * 0x100;
    struct Dir {
        SDL_Keycode a, b;
        int scan;
        char digit;
    };
    static constexpr Dir dirs[] = {
        {SDLK_UP, SDLK_KP_8, 0x48, '8'},   {SDLK_PAGEUP, SDLK_KP_9, 0x49, '9'}, {SDLK_RIGHT, SDLK_KP_6, 0x4D, '6'},
        {SDLK_PAGEDOWN, SDLK_KP_3, 0x51, '3'}, {SDLK_DOWN, SDLK_KP_2, 0x50, '2'}, {SDLK_END, SDLK_KP_1, 0x4F, '1'},
        {SDLK_LEFT, SDLK_KP_4, 0x4B, '4'}, {SDLK_HOME, SDLK_KP_7, 0x47, '7'},
    };
    for (const Dir& d : dirs) {
        if (k.sym == d.a) return d.scan << 8;   // grey keys: no digit, so Max dives
        if (k.sym == d.b) return numlock && !shift ? (d.scan << 8 | d.digit) : d.scan << 8;
    }
    switch (k.sym) {
    case SDLK_KP_5: return numlock && !shift ? '5' : -1;
    case SDLK_RETURN: case SDLK_KP_ENTER: return 13;
    case SDLK_ESCAPE: return 27;
    case SDLK_BACKSPACE: return 8;
    case SDLK_INSERT: case SDLK_KP_0: return 0x5200;
    case SDLK_DELETE: case SDLK_KP_PERIOD: return 0x5300;
    default: break;
    }
    if (k.sym >= 32 && k.sym < 127) {
        int c = static_cast<int>(k.sym);
        if (shift || (k.mod & KMOD_CAPS)) {
            if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        }
        return c;
    }
    return -1;
}

class TacScene : public Scene {
public:
    TacScene(App& app, TacSetup setup, std::shared_ptr<TacHooks> hooks = {}, TacDone onDone = {})
        : app_(app), tac_(app.data, app.fonts, std::move(setup), std::move(hooks)), onDone_(std::move(onDone)) {
        tac_.start();
    }

    void handle(const SDL_Event& e) override {
        if (menu_) {
            menu_->handle(e, app_.mouse(e));
            return;
        }
        if (e.type == SDL_KEYUP) {
            if (held_ && e.key.keysym.sym == heldKey_) held_.reset();
            return;
        }
        if (e.type != SDL_KEYDOWN || e.key.repeat || isModifier(e.key.keysym.sym)) return;
        SDL_Keysym key = e.key.keysym;
        // While Max moves (TAC's NumLock on), the number row and 7 8 9 / U I O /
        // J K L are the keypad: walk, with Shift dive, I (5) sneak. Not while
        // typing a message (NumLock off), nor with Ctrl / Alt / Cmd.
        if (app_.settings.keypadKeys && tac_.numLock() && !(key.mod & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)))
            key.sym = letterKeypad(digitAsKeypad(key.sym));
        const int code = tacKey(key, tac_.numLock());
        if (code < 0) return;
        tac_.pushKey(code);
        held_ = key;
        heldKey_ = e.key.keysym.sym;
        nextRepeat_ = static_cast<double>(tick_) + 17.5;   // typematic delay 250 ms
    }

    void tick() override {
        ++tick_;
        if (menu_) {
            menu_->tick();
            for (const auto& s : tac_.takeSounds()) app_.driverSound(s[0], s[1], s[2]);   // Alt-A/S/J's
            if (auto r = menu_->result()) {
                menu_->draw(tac_.screen());      // text_window leaves the window on the screen
                menu_.reset();
                tac_.menuDone(*r);
            }
            return;
        }
        if (held_ && static_cast<double>(tick_) >= nextRepeat_) {   // 15 keys a second
            const int code = tacKey(*held_, tac_.numLock());
            if (code >= 0) tac_.pushKey(code);
            nextRepeat_ += 70.0 / 15.0;
        }
        tac_.tick();
        for (const auto& s : tac_.takeSounds()) app_.driverSound(s[0], s[1], s[2]);
        if (tac_.waiting() == Tac::Wait::Menu && !menu_) {
            const auto& req = tac_.menuRequest();
            TextWindowOptions opt;
            opt.fillColor = static_cast<std::uint8_t>(req.fill);
            menu_.emplace(app_.fonts.font(1), req.text, req.x, req.y, opt).setHotkeys(hotkeys());
            held_.reset();
        }
        if (tac_.quitRequested()) quit = true;
        if (tac_.finished() && !reported_) {
            reported_ = true;
            const TacResult r = tac_.result();
            std::fprintf(stderr,
                         "tac: score %d, violence %d, flags %d, wounds %d, minutes %d, photos %d, bugs %d, rooms seen %08x\n",
                         r.score, r.violence, r.missionFlags, r.wounds, r.minutes, r.photos, r.bugs,
                         static_cast<unsigned>(r.roomsSeen));
        }
    }

    void draw(Frame& f) override {
        f.palette = tac_.palette();
        f.image = tac_.screen();
        if (menu_) menu_->draw(f.image);
    }

    std::unique_ptr<Scene> next() override {
        if (!tac_.finished() || !reported_ || left_) return nullptr;
        left_ = true;
        if (onDone_) return onDone_(app_, tac_.result());
        // practice: FINAL reloads the env.sve TAC saved (load_env), options &= ~20h
        app_.game.world.hdr.options = static_cast<std::int16_t>(tac_.result().options & ~0x20u);
        auto s = makeScene(app_, "main-menu");
        if (!s) quit = true;
        return s;
    }

    // text_window's Alt keys in TAC's windows: TAC's options word and play_sound.
    TextWindowHotkeys hotkeys() {
        TextWindowHotkeys h;
        h.options = [this] { return static_cast<int>(static_cast<std::int16_t>(tac_.optionsWord())); };
        h.setOptions = [this](int v) {
            saveAnimationsBit(app_, static_cast<int>(tac_.optionsWord()), v);
            tac_.setOptionsWord(static_cast<unsigned>(v));
        };
        h.sound = [this](int n) { tac_.playSound(n); };
        h.quit = [this] { quit = true; };   // ENV[0] = -1, set_exit_state, exit
        return h;
    }

private:
    App& app_;
    Tac tac_;
    TacDone onDone_;
    std::optional<Menu> menu_;
    std::optional<SDL_Keysym> held_;
    SDL_Keycode heldKey_ = SDLK_UNKNOWN;   // the physical key behind held_
    double nextRepeat_ = 0;
    long tick_ = 0;
    bool reported_ = false, left_ = false;
};

int envInt(const char* name, int def) {
    const char* v = std::getenv(name);
    return v ? std::atoi(v) : def;
}

// FINAL's start_practice(0): its options word | 20h, here_org = random(12) + 4,
// here_city = random(16), load_world_file(random(3)) (the names the terminal's
// file search looks up), FINAL's clock_base (0 on a fresh start: night) and
// rand_seed (FINAL's srand seed, part of the building's seed). The draws are
// FINAL's random() (app.game.rng).
TacSetup practiceSetup(App& app) {
    TacSetup s;
    const auto& h = app.game.world.hdr;
    Rng& r = app.game.rng;
    s.org = envInt("REACTION_TAC_ORG", r.below(12) + 4);
    s.city = envInt("REACTION_TAC_CITY", r.below(16));
    {
        World world;
        world.loadWorld(app.data, r.below(3));
        for (std::size_t i = 0; i < 16; ++i) {
            s.cityNames[i] = world.cities[i].name.str();
            s.orgNames[i] = world.orgs[i].name.str();
        }
    }
    s.gameSeed = static_cast<unsigned>(envInt("REACTION_TAC_SEED", static_cast<std::uint16_t>(h.randSeed)));
    s.difficulty = envInt("REACTION_TAC_DIFFICULTY", 1);
    const int clock = static_cast<std::uint16_t>(h.clockBase);
    s.clockBase = static_cast<unsigned>(std::getenv("REACTION_TAC_HOUR") ? envInt("REACTION_TAC_HOUR", 12) * 60 : clock);
    s.female = envInt("REACTION_TAC_FEMALE", (h.options & 2) ? 1 : 0) != 0;
    s.sound = (h.options & 4) != 0;
    s.otherOptions = static_cast<std::uint16_t>(h.options);
    if (const char* rs = std::getenv("REACTION_TAC_RUNSEED")) s.runSeed = static_cast<std::uint32_t>(std::strtoul(rs, nullptr, 10));
    if (const char* m = std::getenv("REACTION_TAC_MODE")) {
        s.env0 = std::atoi(m);
        s.practice = false;
    }
    return s;
}

}  // namespace

std::unique_ptr<Scene> makeTacScene(App& app, TacSetup setup, std::shared_ptr<TacHooks> hooks, TacDone onDone) {
    return std::make_unique<TacScene>(app, std::move(setup), std::move(hooks), std::move(onDone));
}

REACTION_SCENE("tac", [](App& app) {
    TacSetup s = practiceSetup(app);
    if (app.practiceLevel >= 0) s.difficulty = app.practiceLevel;
    return std::make_unique<TacScene>(app, s);
});

}  // namespace reaction
