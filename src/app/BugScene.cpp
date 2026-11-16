// The wiretap / car-tracer screen (BUG.EXE main(), BOARD.C's drawing): board.pic
// on screen, bugs.pic as the sprite page (trace pieces, chips, sockets, output
// icons), and the events of game/Wiretap played back at the original's pace:
// a frame is 5 retraces, a step of the signal along a trace 1.
//
// Registered scenes (practice setup, a new board each time):
//   bug       phone taps          bug-car   car tracer
// For testing, REACTION_BUG_SEED fixes the srand() seed (default: the time, as
// randomize()), and REACTION_BUG_DIFFICULTY the board difficulty (0-3, default 1).
#include "app/BugScene.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <optional>
#include <string>

#include "app/Keypad.h"
#include "app/Menu.h"
#include "app/Scenes.h"
#include "formats/McgaPalette.h"
#include "formats/Pic.h"
#include "formats/TextWindow.h"

namespace reaction {

namespace {

using Ev = WiretapEvent::Kind;

// SDL keys to the codes get_key() gives bug_frame. The keypad digits act as
// the arrows they are with Num Lock off; so do the number row's digits, the
// keypad on the letters (7 8 9 / U I O / J K L), for keyboards without a
// keypad (app/Keypad.h).
int dosKey(const SDL_Keysym& k, bool keypadKeys) {
    using namespace bugkeys;
    if (k.mod & KMOD_ALT) {
        switch (k.sym) {
        case SDLK_q: return kAltQ;
        case SDLK_s: return kAltS;
        case SDLK_p: return kAltP;
        default: return kNone;
        }
    }
    switch (!keypadKeys || (k.mod & (KMOD_CTRL | KMOD_GUI)) ? k.sym : letterKeypad(digitAsKeypad(k.sym))) {
    case SDLK_UP: case SDLK_KP_8: return kUp;
    case SDLK_PAGEUP: case SDLK_KP_9: return kPgUp;
    case SDLK_RIGHT: case SDLK_KP_6: return kRight;
    case SDLK_PAGEDOWN: case SDLK_KP_3: return kPgDn;
    case SDLK_DOWN: case SDLK_KP_2: return kDown;
    case SDLK_END: case SDLK_KP_1: return kEnd;
    case SDLK_LEFT: case SDLK_KP_4: return kLeft;
    case SDLK_HOME: case SDLK_KP_7: return kHome;
    case SDLK_RETURN: case SDLK_KP_ENTER: return kEnter;
    case SDLK_SPACE: return kSpace;
    case SDLK_ESCAPE: return kEsc;
    default: return kNone;
    }
}

// Any key that puts something in the BIOS buffer (not a lone modifier).
bool isCharKey(const SDL_Keysym& k) {
    switch (k.sym) {
    case SDLK_LSHIFT: case SDLK_RSHIFT: case SDLK_LCTRL: case SDLK_RCTRL:
    case SDLK_LALT: case SDLK_RALT: case SDLK_LGUI: case SDLK_RGUI:
    case SDLK_CAPSLOCK: case SDLK_NUMLOCKCLEAR: case SDLK_SCROLLLOCK:
        return false;
    default:
        return true;
    }
}

class BugScene : public Scene {
public:
    BugScene(App& app, const WiretapSetup& setup, BugDone onDone, std::shared_ptr<BugCaseHooks> hooks)
        : app_(app), onDone_(std::move(onDone)), logic_(setup), palette_(mcgaPalette(palettes::kGame)),
          hooks_(std::move(hooks)) {
        // During a case, show_intercepted_info runs describe_new_info on the
        // case's tables (game/BugCase.h); the scene replays what it showed.
        if (hooks_) logic_.intercept = [this](int amount) { return hooks_->intercept(logic_, amount); };
        // main(): camera.pic (the intercept popup's frame) is grabbed first; then
        // load_bug_sprites leaves bugs.pic on page 2 (chip_page), and board.pic
        // goes on screen.
        sprites_ = decodePicture(app.data.read("bugs.pic")).image;
        screen_ = decodePicture(app.data.read("board.pic")).image;
        logic_.build(screen_);
        play(logic_.takeEvents());
        keys_.clear();   // flush_input()
    }

    void handle(const SDL_Event& e) override {
        if (menu_) {
            menu_->handle(e, app_.mouse(e));
            return;
        }
        if (e.type != SDL_KEYDOWN) return;
        if (scriptKey_) {                    // wait_key_or_button: any key
            if (isCharKey(e.key.keysym)) scriptKey_ = false;
            return;
        }
        if (paused_) {                       // get_key(): any key resumes
            if (isCharKey(e.key.keysym)) {
                paused_ = false;
                screen_ = saved_;
            }
            return;
        }
        const int k = dosKey(e.key.keysym, app_.settings.keypadKeys);
        if (k != bugkeys::kNone && keys_.size() < 15) keys_.push_back(k);   // the BIOS buffer
    }

    void tick() override {
        if (menu_) {
            menu_->tick();
            if (auto r = menu_->result()) closeMenu(*r);
            return;
        }
        if (paused_ || finished_) return;
        if (runScript()) return;
        if (wait_ > 0 && --wait_ > 0) return;
        for (;;) {
            if (pending_.empty()) {
                if (logic_.done()) {
                    finished_ = true;
                    const WiretapResult r = logic_.result();
                    std::fprintf(stderr, "bug: progress %d, alarms %d (env bytes 8=%d 9=%d 0=%02Xh)\n", r.progress,
                                 r.alarms, r.envProgress(), r.envAlarms(), r.envProgram(logic_.setup().practice));
                    return;
                }
                int key = bugkeys::kNone;
                if (!keys_.empty()) {
                    key = keys_.front();
                    keys_.pop_front();
                }
                logic_.frame(key);
                for (auto& ev : logic_.takeEvents()) pending_.push_back(std::move(ev));
                if (pending_.empty()) return;
            }
            WiretapEvent ev = std::move(pending_.front());
            pending_.pop_front();
            apply(ev);
            if (ev.kind == Ev::Wait && ev.a > 0) {
                wait_ = ev.a;
                return;
            }
            if (menu_ || paused_ || !script_.empty()) return;
        }
    }

    void draw(Frame& f) override {
        f.palette = palette_;
        f.image = screen_;
        if (menu_) menu_->draw(f.image);
    }

    std::unique_ptr<Scene> next() override {
        if (!finished_) return nullptr;
        finished_ = false;
        quit = !onDone_;
        return onDone_ ? onDone_(app_, logic_.result()) : nullptr;
    }

private:
    enum class MenuKind { Message, Quit };

    // The build events at once (nothing waits while ticks is 0).
    void play(std::vector<WiretapEvent> evs) {
        for (auto& ev : evs) apply(ev);
    }

    // draw_sprite: bugs.pic sprites, colour 0 transparent. 0-31: 8x8 trace
    // pieces from (col * 8, row * 8 + 16); 32-43: 16x16 chips from (n * 16, 0).
    void drawSprite(int n, int x, int y) {
        if (n < 32) screen_.blit(sprites_, (n % 16) << 3, (n / 16) * 8 + 16, 8, 8, x, y, 0);
        else screen_.blit(sprites_, (n - 32) << 4, 0, 16, 16, x, y, 0);
    }

    void vline(int x, int y0, int y1, std::uint8_t c) {
        for (int y = y0; y <= y1; ++y) screen_.set(x, y, c);
    }

    void apply(const WiretapEvent& ev) {
        switch (ev.kind) {
        case Ev::TraceCell:   // draw_trace_cell
            for (int i = 0; i < 8; ++i) {
                if (!(ev.c & (1 << i))) continue;
                if (ev.c & 0x100) drawSprite((((ev.d + 4) & 7) == i ? 16 : 24) + i, ev.a << 3, ev.b << 3);
                else drawSprite(i, ev.a << 3, ev.b << 3);
            }
            break;
        case Ev::Chip: {      // draw_chip: the socket, then the chip
            const int x = ev.a * 32 + 0x28, y = ev.b * 32 + 8;
            screen_.blit(sprites_, ev.d ? 17 : 1, 55, 14, 19, x + 1, y - 1);
            if (ev.c != -1) drawSprite(ev.c + 32, x, y);
            break;
        }
        case Ev::LockSocket: {
            const int x = (ev.a << 5) + 0x27, y = (ev.b << 5) + 5;
            recolorRect(screen_, x, y, 0x12, 0x17, 2, 8);
            recolorRect(screen_, x, y, 0x12, 0x17, 3, 8);
            vline((ev.a << 5) + 0x28, y, (ev.b << 5) + 0x18, 8);
            vline((ev.a << 5) + 0x37, y, (ev.b << 5) + 0x18, 8);
            break;
        }
        case Ev::Spare:
            fillRect(screen_, 0x9A, 0xB1, 12, 16, 8);
            drawSprite(ev.a + 0x20, 0x98, 0xB1);
            break;
        case Ev::Output:
            screen_.blit(sprites_, ev.b, 0x20, 12, 16, ((ev.a & 1) ? 0x18 : 0) + 0x108 + 2, ev.a << 4);
            break;
        case Ev::Timer:
            drawTimer(ev.a);
            break;
        case Ev::Palette: {
            PaletteMap m;
            for (int i = 0; i < 17; ++i) m[i] = Wiretap::kTunes[ev.a][i];
            palette_ = mcgaPalette(m);
            break;
        }
        case Ev::Sound:       // play_sound(n) (emitted only with sound on)
            app_.driverSound(ev.a);
            break;
        case Ev::Wait:        // (handled by tick)
            break;
        case Ev::Intercept:
            // show_intercepted_info: the screen to the back page, what
            // describe_new_info(org, city, amount, 1) drew and waited for, the
            // screen back (practice runs never get here)
            if (hooks_ && ev.b >= 0) {
                back_ = screen_;
                for (const LibStep& st : hooks_->script(ev.b)) script_.push_back(st);
                script_.push_back(LibStep{LibStep::Kind::Retrace, {}, {}, 0, 0, -1});   // (end: restore)
            }
            break;
        case Ev::Message: {
            TextWindowOptions opt;
            opt.fillColor = static_cast<std::uint8_t>(ev.a);
            openMenu(MenuKind::Message, ev.text, 100, 0x51, opt);
            break;
        }
        case Ev::QuitPrompt: {
            saved_ = screen_;
            TextWindowOptions opt;
            opt.fillColor = 15;
            openMenu(MenuKind::Quit, "Are you sure\nyou want to Quit?\n No\n Yes\n", 0x50, 0x51, opt);
            break;
        }
        case Ev::Pause: {
            // text_window with `paused` set draws and returns at once; get_key().
            saved_ = screen_;
            screen_.fill(0);
            TextWindowOptions opt;
            opt.fillColor = static_cast<std::uint8_t>(ev.a);
            drawTextWindow(screen_, app_.fonts.font(1), "PAUSED\n", 0x80, 0x51, opt);
            keys_.clear();
            paused_ = true;
            break;
        }
        }
    }

    // draw_timer: "HH:MM:SS" in font 1, colour 7 on 0, with C's / and %.
    void drawTimer(int t) {
        char s[9] = "00:00:00";
        int rest = t;
        const int hrs = rest / 3600;
        rest -= 3600 * hrs;
        const int min = rest / 60;
        rest -= 60 * min;
        const int sec = rest;
        s[0] = static_cast<char>(hrs / 10 + '0');
        s[1] = static_cast<char>(hrs % 10 + '0');
        s[3] = static_cast<char>(min / 10 + '0');
        s[4] = static_cast<char>(min % 10 + '0');
        s[6] = static_cast<char>(sec / 10 + '0');
        s[7] = static_cast<char>(sec % 10 + '0');
        fillRect(screen_, 0x100, 0xB8, 0x30, 9, 0);
        TextAttr attr;
        attr.textbg = 1;
        attr.color = 7;
        attr.background = 0;
        app_.fonts.font(1).draw(screen_, 0x104, 0xB9, s, attr);
    }

    void openMenu(MenuKind kind, std::string text, int x, int y, TextWindowOptions opt) {
        keys_.clear();   // text_window: flush_input()
        menuKind_ = kind;
        menu_.emplace(app_.fonts.font(1), std::move(text), x, y, opt);
    }

    // Plays the intercept's steps until one waits; true while it does.
    bool runScript() {
        while (!script_.empty()) {
            if (scriptKey_ || menu_) return true;
            const LibStep st = std::move(script_.front());
            script_.pop_front();
            switch (st.kind) {
            case LibStep::Kind::Draw: {
                LibCanvas c{screen_, app_.fonts.font(1), hooks_->faces(), libFill_};
                st.draw(c);
                break;
            }
            case LibStep::Kind::Key:   // wait_key_or_button (then flush_input)
                keys_.clear();
                scriptKey_ = true;
                return true;
            case LibStep::Kind::Menu: {
                TextWindowOptions opt;
                if (!(st.y & 1)) libFill_ = 5;
                opt.fillColor = static_cast<std::uint8_t>(libFill_);
                openMenu(MenuKind::Message, st.text, st.x, st.y, opt);
                return true;
            }
            case LibStep::Kind::Retrace:
                if (st.n < 0) {
                    screen_ = back_;   // drv_copy_rect(back_page -> g_gfx)
                    keys_.clear();
                    break;
                }
                wait_ = st.n;
                return true;
            case LibStep::Kind::Popup: break;   // (BUG's popup is recorded as drawing)
            }
        }
        return false;
    }

    void closeMenu(int result) {
        if (menuKind_ == MenuKind::Quit) {
            menu_.reset();
            if (result == 1) {   // exit() to DOS: the game ends, env not saved
                quit = true;
                return;
            }
            screen_ = saved_;
            return;
        }
        menu_->draw(screen_);    // the window stays on the screen
        menu_.reset();
    }

    App& app_;
    BugDone onDone_;
    Wiretap logic_;
    Image sprites_, screen_, saved_;
    Palette palette_;
    std::deque<WiretapEvent> pending_;
    std::deque<int> keys_;
    std::optional<Menu> menu_;
    std::shared_ptr<BugCaseHooks> hooks_;
    std::deque<LibStep> script_;       // an intercept being shown
    Image back_;                       // back_page meanwhile
    int libFill_ = 15;                 // g_gfx->fillcolor for the library's drawing
    bool scriptKey_ = false;
    MenuKind menuKind_ = MenuKind::Message;
    int wait_ = 0;
    bool paused_ = false, finished_ = false;
};

WiretapSetup practiceSetup(bool car) {
    WiretapSetup s;
    s.carTracer = car;
    s.practice = true;
    if (const char* d = std::getenv("REACTION_BUG_DIFFICULTY")) s.difficulty = std::atoi(d);
    if (const char* seed = std::getenv("REACTION_BUG_SEED"))
        s.seed = static_cast<std::uint32_t>(std::strtoul(seed, nullptr, 10));
    else
        s.seed = static_cast<std::uint32_t>(std::time(nullptr)) & 0x7FFF;
    return s;
}

std::unique_ptr<Scene> backToMenu(App& app, const WiretapResult&) { return makeScene(app, "main-menu"); }

}  // namespace

std::unique_ptr<Scene> makeBugScene(App& app, const WiretapSetup& setup, BugDone onDone,
                                    std::shared_ptr<BugCaseHooks> hooks) {
    return std::make_unique<BugScene>(app, setup, std::move(onDone), std::move(hooks));
}

REACTION_SCENE("bug", [](App& app) {
    WiretapSetup s = practiceSetup(false);
    if (app.practiceLevel >= 0) s.difficulty = app.practiceLevel;
    return makeBugScene(app, s, backToMenu);
});
REACTION_SCENE("bug-car", [](App& app) { return makeBugScene(app, practiceSetup(true), backToMenu); });

}  // namespace reaction
