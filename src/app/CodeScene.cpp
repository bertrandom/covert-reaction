// The crypto workstation (CODE.EXE): covert-decomp src/CODE/CODE.C and CLOCK.C.
// The puzzle logic is game/CodeBreak; this is the screen, drawn the way the
// original draws it (into one persistent page, piece by piece, with the same
// retrace waits), and its input loop.
//
// Layout (320x200, default EGA colours: CODE.EXE only calls init_mode, which
// loads the driver's default DAC; its drv_set_color_map is the CGA dither map):
//   - left column: per cipher letter A..Z (y = n*7 + 9) the player's guess
//     (x 4, font 2, colour 11), the letter (x 12, font 3, colour 10) and its
//     count (x 20, font 2, colour 10), framed by colour-2 lines;
//   - top bar (x 34.., colour 10): the title (font 2, colour 0) and the clock
//     (x 256, font 2, colour 2 on 10);
//   - the message: 42 cells a row from (44, 16), 6 x 16 pixels; the cipher
//     letter (font 3, colour 10) and below it, at +7, the guess (font 2, 11).
//
// Keys (workstation_input): a..z picks a cipher letter (it blinks), the next
// key is its plain letter (a non-letter clears it); F1 computer assistance
// (one letter, costs 3600 clock ticks); F10 erase all but the computer's
// letters; Esc give up; Alt-S sound, Alt-P pause, Alt-Q quit the game.
// The original reads keys only between its retrace waits (every 10 retraces
// while idle); keys typed meanwhile wait in a 15-key buffer like the BIOS's.
#include <algorithm>
#include <array>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "app/FrontEnd.h"
#include "app/Menu.h"
#include "app/Scenes.h"
#include "formats/McgaPalette.h"
#include "formats/TextWindow.h"
#include "game/CodeBreak.h"

namespace redux {

namespace {

// DOS key codes as the library's get_key returns them (ASCII, or scan << 8).
constexpr int kEsc = 0x1B, kF1 = 0x3B00, kF10 = 0x4400, kAltQ = 0x1000, kAltS = 0x1F00,
              kAltP = 0x1900;

// An SDL key press as the BIOS would have reported it; nullopt for keys that
// don't produce one (Shift, Ctrl, Alt alone, ...).
std::optional<int> codeKey(const SDL_Event& e) {
    if (e.type != SDL_KEYDOWN) return std::nullopt;
    const SDL_Keycode k = e.key.keysym.sym;
    const Uint16 mod = e.key.keysym.mod;
    switch (k) {
    case SDLK_LSHIFT: case SDLK_RSHIFT: case SDLK_LCTRL: case SDLK_RCTRL: case SDLK_LALT:
    case SDLK_RALT: case SDLK_LGUI: case SDLK_RGUI: case SDLK_CAPSLOCK: case SDLK_NUMLOCKCLEAR:
    case SDLK_SCROLLLOCK: case SDLK_MODE:
        return std::nullopt;
    default:
        break;
    }
    if (mod & KMOD_ALT) {
        if (k == SDLK_q) return kAltQ;
        if (k == SDLK_s) return kAltS;
        if (k == SDLK_p) return kAltP;
        return 0xFF00;                       // some other Alt key: not a letter
    }
    if (k >= SDLK_F1 && k <= SDLK_F10) return kF1 + static_cast<int>(k - SDLK_F1) * 0x100;
    switch (k) {
    case SDLK_UP: return 0x4800;
    case SDLK_DOWN: return 0x5000;
    case SDLK_LEFT: return 0x4B00;
    case SDLK_RIGHT: return 0x4D00;
    case SDLK_KP_ENTER: return 0x0D;
    default: break;
    }
    // Letters: SDL key codes are lower case whatever Shift / Caps Lock say. (On
    // DOS a shifted letter came in upper case, which the game ignored; here
    // both cases work.)
    if (k >= SDLK_a && k <= SDLK_z) {
        if (mod & KMOD_CTRL) return static_cast<int>(k - SDLK_a) + 1;   // ^A..^Z
        return static_cast<int>(k);
    }
    if (k > 0 && k < 0x80) return static_cast<int>(k);   // Esc, Enter, Space, digits, ...
    return 0xFF00;
}

class CodeScene : public Scene {
public:
    using OnDone = std::function<std::unique_ptr<Scene>(App&, const CodeOutcome&)>;

    CodeScene(App& app, const CodeSetup& setup, OnDone onDone)
        : app_(app), puzzle_(setup), onDone_(std::move(onDone)), screen_(320, 200, 0),
          palette_(mcgaPalette(palettes::kEga)) {
        outcome_.options = setup.options;
        drawWorkstation();
        state_ = St::Intro;
    }

    void handle(const SDL_Event& e) override {
        if (menu_) {
            menu_->handle(e, app_.mouse(e));
            return;
        }
        auto k = codeKey(e);
        if (!k) return;
        if (state_ == St::Paused) {       // get_key() after the PAUSED window
            resume();
            return;
        }
        if (keys_.size() < 15) keys_.push_back(*k);
    }

    void tick() override {
        if (menu_) {
            menu_->tick();
            if (auto r = menu_->result()) {
                menu_.reset();
                menuClosed(*r);
            }
            return;
        }
        if (wait_ > 0 && --wait_ > 0) return;
        // Run the program until it waits again (or for input).
        for (int guard = 0; wait_ == 0 && !menu_ && guard < 1000; ++guard) {
            if (!step()) break;
        }
    }

    void draw(Frame& f) override {
        f.palette = palette_;
        if (state_ == St::Paused) f.image = pauseScreen_;
        else f.image = screen_;
        if (menu_) menu_->draw(f.image);
    }

    std::unique_ptr<Scene> next() override {
        if (state_ != St::Done) return nullptr;
        if (outcome_.quitGame) {
            quit = true;
            return nullptr;
        }
        if (onDone_) return onDone_(app_, outcome_);
        return makeMainMenuScene(app_);       // practice: back to FINAL's menu
    }

private:
    enum class St { Intro, Main, Blink, Scramble, QuitMenu, Paused, Decoded, Done };
    enum class Blink { Erase, Show, Check };

    // --- drawing primitives over the page, with the original's arguments ---
    void text(int font, std::string_view s, int x, int y, std::uint8_t color, bool opaque = false,
              std::uint8_t bg = 0) {
        TextAttr a;
        a.textbg = opaque ? 1 : 0;
        a.color = color;
        a.background = bg;
        app_.fonts.font(font).draw(screen_, x, y, s, a);
    }
    void letter(int font, unsigned char c, int x, int y, std::uint8_t color, bool opaque = false,
                std::uint8_t bg = 0) {
        const char s[1] = {static_cast<char>(c)};
        text(font, std::string_view(s, 1), x, y, color, opaque, bg);
    }
    void fillXY(int x1, int y1, int x2, int y2, std::uint8_t c) {   // fill_rect_xy
        fillRect(screen_, x1, y1, x2 - x1 + 1, y2 - y1 + 1, c);
    }
    void line(int x1, int y1, int x2, int y2, std::uint8_t c) {     // draw_line (h/v only here)
        fillXY(std::min(x1, x2), std::min(y1, y2), std::max(x1, x2), std::max(y1, y2), c);
    }
    static int cellX(int i) { return (i % CodeBreak::kColumns) * 6 + 44; }
    static int cellY(int i) { return (i / CodeBreak::kColumns) * 16 + 16; }
    static int columnY(int n) { return n * 7 + 9; }

    // draw_workstation up to the typing-out of the message.
    void drawWorkstation() {
        fillXY(0, 0, 320, 200, 0);
        fillXY(34, 0, 320, 8, 10);
        line(1, 0, 31, 0, 2);
        line(1, 199, 31, 199, 2);
        line(0, 1, 0, 198, 2);
        line(32, 1, 32, 198, 2);
        fillRect(screen_, 2, 2, 29, 4, 2);
        fillRect(screen_, 2, 194, 29, 4, 2);
        text(2, puzzle_.title(), 64, 1, 0);
        for (int i = 0; i < CodeBreak::kLetters; ++i) {
            letter(3, static_cast<unsigned char>('A' + i), 12, columnY(i), 10);
            text(2, "0", 20, columnY(i), 10);
        }
    }

    // One cell of the typing-out (with its letter's running count).
    void introCell(int i) {
        const unsigned char plain = puzzle_.plainAt(i);
        const unsigned char ch = puzzle_.cipherAt(i);
        if (plain >= 'A' && plain <= '\\') {
            const int n = ch - 'A';
            fillRect(screen_, 20, columnY(n), 12, 7, 0);
            text(2, std::to_string(++running_[static_cast<std::size_t>(n)]), 20, columnY(n), 10);
        }
        letter(3, ch, cellX(i), cellY(i), 10);
        if ((i + 1) % CodeBreak::kColumns == 0) {
            auto up = [](unsigned char c) { return c >= 'A' && c <= 'Z'; };
            if (up(puzzle_.plainAt(i)) && up(puzzle_.plainAt(i + 1)))
                text(3, "-", CodeBreak::kColumns * 6 + 44, cellY(i), 10);
        }
    }

    // draw_message: the guess column and every cell. `columnFont` is the port's
    // font at the call: 3 right after the typing-out, 2 otherwise.
    void drawMessage(int columnFont = 2) {
        for (int i = 0; i < CodeBreak::kLetters; ++i)
            letter(columnFont, static_cast<unsigned char>(puzzle_.guess(i)), 4, columnY(i), 11, true, 0);
        for (int i = 0; i < puzzle_.length(); ++i) {
            fillRect(screen_, cellX(i), cellY(i), 6, 7, 0);
            letter(3, puzzle_.cipherAt(i), cellX(i), cellY(i), 10);
            letter(2, puzzle_.guessRowAt(i), cellX(i), cellY(i) + 7, 11, true, 0);
        }
    }

    void drawClock() { text(2, clock_.update(), 256, 1, 2, true, 10); }

    void playSound(int /*n*/) {
        // play_sound(n): effect 45 (letter set) / 46 (scramble, decoded) when
        // options & 4. No sound driver in Covert Redux yet.
    }

    // --- the program ---
    // Runs one step; false when it is waiting for input it hasn't got.
    bool step() {
        switch (state_) {
        case St::Intro:
            if (introPos_ < puzzle_.length()) {
                introCell(introPos_++);
                wait_ = 2;
                return true;
            }
            if (puzzle_.guessesShownAtStart()) drawMessage(3);
            enterMain();
            return true;
        case St::Main:
            ++clock_.ticks;
            drawClock();
            if (keys_.empty()) {
                wait_ = 10;
                return true;
            }
            onKey(popKey());
            return true;
        case St::Blink:
            switch (blink_) {
            case Blink::Erase:
                ++clock_.ticks;
                drawClock();
                fillRect(screen_, 12, columnY(blinkLetter_), 6, 6, 0);
                blink_ = Blink::Show;
                wait_ = 5;
                return true;
            case Blink::Show:
                letter(3, static_cast<unsigned char>('A' + blinkLetter_), 12, columnY(blinkLetter_), 10);
                blink_ = Blink::Check;
                wait_ = 5;
                return true;
            case Blink::Check:
                if (keys_.empty()) {
                    blink_ = Blink::Erase;
                    return true;
                }
                {
                    int j = popKey() - 'a';
                    puzzle_.setGuess(blinkLetter_, j);
                }
                playSound(45);
                drawMessage();
                afterCommand();
                return true;
            }
            return false;
        case St::Scramble:
            if (scrambleErase_) {
                scramblePos_ = puzzle_.scramblePos();
                fillRect(screen_, cellX(scramblePos_), cellY(scramblePos_), 6, 6, 0);
                scrambleErase_ = false;
                wait_ = 2;
                return true;
            }
            letter(3, puzzle_.cipherAt(scramblePos_), cellX(scramblePos_), cellY(scramblePos_), 10);
            scrambleErase_ = true;
            if (++scrambleCount_ < 200) return true;
            puzzle_.computerAssist();
            drawMessage();
            clock_.ticks += 3600;
            afterCommand();
            return true;
        case St::QuitMenu:
        case St::Paused:
        case St::Decoded:
        case St::Done:
            return false;
        }
        return false;
    }

    int popKey() {
        int k = keys_.front();
        keys_.pop_front();
        return k;
    }

    void enterMain() {
        state_ = St::Main;
        wait_ = 10;
    }

    void onKey(int k) {
        const int ltr = k - 'a';
        switch (k) {
        case kEsc:
            finish();
            return;
        case kF1:                     // computer assistance
            playSound(46);
            state_ = St::Scramble;
            scrambleCount_ = 0;
            scrambleErase_ = true;
            return;
        case kF10:                    // erase all guesses
            puzzle_.refreshGuesses();
            drawMessage();
            enterMain();              // returns without the solved test
            return;
        case kAltQ:
            keys_.clear();
            state_ = St::QuitMenu;
            menu_.emplace(app_.fonts.font(2), "Are you sure\nyou want to Quit?\n No\n Yes\n", 80, 80);
            return;
        case kAltS:
            outcome_.options ^= 4;
            afterCommand();
            return;
        case kAltP: {
            keys_.clear();
            pauseScreen_ = Image(320, 200, 0);
            TextWindowOptions opt;
            opt.fillColor = 10;       // the port's fillcolor, last set by draw_clock
            drawTextWindow(pauseScreen_, app_.fonts.font(2), "PAUSED\n", 128, 81, opt);
            state_ = St::Paused;
            return;
        }
        default:
            if (ltr < 0 || ltr >= CodeBreak::kLetters) {
                enterMain();          // `continue`: back to waiting
                return;
            }
            state_ = St::Blink;
            blink_ = Blink::Erase;
            blinkLetter_ = ltr;
            return;
        }
    }

    void resume() {                   // after the pause's key: screen restored
        afterCommand();
    }

    // The end of workstation_input's switch: the solved test.
    void afterCommand() {
        if (puzzle_.decoded()) {
            outcome_.result = 1;
            puzzle_.solve();
            playSound(46);
            drawMessage();
            keys_.clear();
            state_ = St::Decoded;
            menu_.emplace(app_.fonts.font(2), "MESSAGE DECODED\n", 100, 2);
            return;
        }
        enterMain();
    }

    void menuClosed(int r) {
        if (state_ == St::QuitMenu) {
            if (r == 1) {
                outcome_.quitGame = true;
                finish();
                return;
            }
            afterCommand();
        } else if (state_ == St::Decoded) {
            finish();
        }
    }

    void finish() {
        outcome_.elapsed = clock_.elapsed;
        state_ = St::Done;
    }

    App& app_;
    CodeBreak puzzle_;
    OnDone onDone_;
    CodeOutcome outcome_;
    CodeClock clock_;
    Image screen_, pauseScreen_;
    Palette palette_;
    std::optional<Menu> menu_;
    std::deque<int> keys_;
    St state_ = St::Intro;
    int wait_ = 0;
    int introPos_ = 0;
    std::array<int, 28> running_{};
    Blink blink_ = Blink::Erase;
    int blinkLetter_ = 0;
    int scramblePos_ = 0, scrambleCount_ = 0;
    bool scrambleErase_ = true;
};

int envInt(const char* name, int fallback) {
    if (const char* v = std::getenv(name); v && *v) return std::atoi(v);
    return fallback;
}

// Practice: what FINAL's start_practice leaves in the env block. Development
// settings: COVERT_CODE_LEVEL (practice level 0..3, default 0 = Local
// Disturbance, the menu's first item), COVERT_CODE_DIFFICULTY (the workstation's
// own -3..5, overriding the level), COVERT_CODE_SEED (random seed).
std::unique_ptr<Scene> makePracticeCodeScene(App& app) {
    CodeSetup setup;
    setup.seed = static_cast<std::uint32_t>(
        envInt("COVERT_CODE_SEED", static_cast<int>(std::time(nullptr) & 0x7FFF)));
    setup.gameDifficulty = clampTo(envInt("COVERT_CODE_LEVEL", 0), 0, 3);
    setup.skillLevels = 0x555;
    setup.options = 0x24;
    if (const char* d = std::getenv("COVERT_CODE_DIFFICULTY"); d && *d) setup.difficultyOverride = std::atoi(d);
    Rng finalRng(setup.seed);
    setup.message = composePracticeIntercept(app.data, finalRng);
    return std::make_unique<CodeScene>(app, setup, nullptr);
}

}  // namespace

REDUX_SCENE("code", makePracticeCodeScene);

}  // namespace redux
