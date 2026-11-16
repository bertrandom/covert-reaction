// The wiretap / car-tracer puzzle (BUG.EXE: covert-decomp src/BUG/BUG.C, BOARD.C).
//
// A circuit board of 7 x 5 chip sockets carries 10 signal lines from the left edge
// to 10 outputs on the right. Every chip takes two lines in and gives two out
// following its truth table (`kLogic`); between chip columns the lines run
// straight, cross, or swap with the next row. At the start some outputs carry a
// signal (the phone lines, or the car's circuits) and the others don't (the
// alarms). The player moves a cursor over the first six columns and swaps the
// chip in hand with the chip in a socket (Enter/Space), until every phone line
// is dead ("tapped") without lighting an alarm line, before the clock runs out.
// Sockets shown empty hold a hidden chip: swapping with one costs 10 ticks.
// Locked sockets (grey) can't be changed.
//
// This is the program's logic without the drawing: build() and frame() do what
// build_board() and bug_frame() do, in the same order, with the same random()
// calls, and append what the original draws, plays and waits for to an event
// list that the screen (app/BugScene.cpp) plays back. The original blocks while
// it animates the signal (trace_signal waits a retrace per cell) and while a
// text window is open; here those are Wait and Message events, so the logic
// runs to completion and the scene paces it.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Image.h"
#include "game/Rng.h"
#include "game/World.h"

namespace redux {

// What BUG.EXE reads from the env block in main().
struct WiretapSetup {
    int difficulty = 1;       // ENV+3Ah: the game's difficulty, 0-3
    int electronics = 1;      // (ENV+4Eh >> 6) & 3: the agent's electronics skill
    int alertLevel = 0;       // ENV+48h: the alert level (time limit -1 minute per 5)
    bool carTracer = false;   // ENV[0] == 12: a car tracer instead of phone taps
    bool practice = true;     // options (ENV+24h) & 20h: practice, no intercepted info
    bool sound = true;        // options & 4
    std::uint32_t seed = 1;   // randomize(): srand(time(0) & 7FFFh)

    // The board's difficulty: clamp(difficulty - electronics + 1, 0, 3).
    int boardDifficulty() const;
    // Seconds: clamp(10 - alert / 5, 2, 10) minutes, halved for a car tracer.
    int timeLimit() const;

    // From the env block: the World header (load_env) and ENV[0], the code the
    // game starts BUG with (12: car tracer). The seed stays to be set.
    static WiretapSetup fromEnv(const WorldHeader& hdr, int program);
};

// What it writes back before exiting.
struct WiretapResult {
    int progress = 0;         // phones tapped / tracer circuits done (0 after an alarm)
    int alarms = 0;
    bool quitGame = false;    // Alt-Q, Yes: exit() without saving the env (ENV[0] = FFh)
    // The env bytes: ENV[8] = progress * 5, ENV[9] = alarms, ENV[0] = 11h in
    // practice (back to FINAL) else 0 (back to GAME).
    std::uint8_t envProgress() const { return static_cast<std::uint8_t>(progress * 5); }
    std::uint8_t envAlarms() const { return static_cast<std::uint8_t>(alarms); }
    std::uint8_t envProgram(bool practice) const { return practice ? 0x11 : 0; }
};

// The DOS key codes bug_frame() handles (get_key(): extended keys as scan << 8).
namespace bugkeys {
inline constexpr int kNone = -1;
inline constexpr int kEnter = 13, kSpace = ' ', kEsc = 0x1B;
inline constexpr int kUp = 0x4800, kPgUp = 0x4900, kRight = 0x4D00, kPgDn = 0x5100;
inline constexpr int kDown = 0x5000, kEnd = 0x4F00, kLeft = 0x4B00, kHome = 0x4700;
inline constexpr int kAltQ = 0x1000, kAltS = 0x1F00, kAltP = 0x1900;
}  // namespace bugkeys

struct WiretapEvent {
    enum class Kind {
        TraceCell,   // draw_trace_cell: a=x, b=y (8x8 cells), c=mask (100h lit), d=dir
        Chip,        // draw_chip: a=column, b=row, c=chip (-1: empty socket), d=selected
        LockSocket,  // grey a locked socket: a=column, b=row
        Spare,       // the chip in hand: a=chip
        Output,      // an output's icon: a=line, b=x of the icon in bugs.pic (y 20h)
        Timer,       // draw_timer: a=seconds left
        Palette,     // play_tune(a): set_palette(tune_data[a])
        Sound,       // play_sound(a) (only emitted with sound on)
        Wait,        // wait_retrace(a)
        Message,     // text_window(text, 100, 51h) with fillcolor a; waits for a key
        Intercept,   // show_intercepted_info(a): describe_new_info(org, city, a, 1)
        QuitPrompt,  // Alt-Q: "Are you sure you want to Quit?" (Yes ends the game)
        Pause,       // Alt-P: black screen, "PAUSED" (window fillcolor a), any key
    };
    Kind kind;
    int a = 0, b = 0, c = 0, d = 0;
    std::string text;
};

class Wiretap {
public:
    static constexpr int kCellsX = 40, kCellsY = 25;
    static constexpr int kChipCols = 7, kChipRows = 5, kLines = 10;
    // Chip flags in chips(): 40h locked, 80h placed (shown); low bits the chip.
    static constexpr int kLocked = 0x40, kPlaced = 0x80;

    // logic[chip][right * 2 + left] = {left output, right output}.
    static const int kLogic[12][4][2];
    static const int kDirX[8], kDirY[8];
    // tune_data: the palettes play_tune() sets (colours 4, 5, 12, 13 cycle).
    static const std::uint8_t kTunes[5][17];

    explicit Wiretap(const WiretapSetup& setup);

    // main() from randomize() on: build_board() over the board picture (page 0;
    // the traces are read from its pixels).
    void build(const Image& board);
    // bug_frame(): one frame with the key pressed (bugkeys::kNone: none).
    void frame(int key);
    // board_status(update) etc. are private; done() is quit_flag.
    bool done() const { return quit_; }
    WiretapResult result() const;

    // The events appended since the last call.
    std::vector<WiretapEvent> takeEvents();

    // State, for the screen and the tests.
    const WiretapSetup& setup() const { return setup_; }
    int difficulty() const { return difficulty_; }
    int chip(int col, int row) const { return chips_[col][row]; }
    int link(int col, int line) const { return links_[col][line]; }
    int signal(int stage, int line) const { return sig_[stage][line]; }
    int trace(int x, int y) const { return traces_[x][y]; }
    int spare() const { return spare_; }
    int cursorX() const { return curX_; }
    int cursorY() const { return curY_; }
    int ticks() const { return ticks_; }
    int timeLeft() const { return timeLimit_; }
    int tapped() const { return tapped_; }
    int alarms() const { return alarms_; }
    int crossings() const { return crossings_; }
    Rng& rng() { return rng_; }

    // The first part of build_board alone (no random calls): the trace bits
    // read from the board picture.
    void scanTraces(const Image& board);

private:
    void generate();                        // build_board after the trace scan
    void boardStatus(int update);           // board_status
    void traceSignal(int x, int y, int dir, int lit);
    void advanceTimer(int n);
    void drawChip(int col, int row, bool sel);
    void resetSignals();
    int traceAt(int x, int y) const;

    void emit(WiretapEvent::Kind k, int a = 0, int b = 0, int c = 0, int d = 0, std::string text = {});
    void sound(int n);
    void message(const std::string& text, int fill);

    WiretapSetup setup_;
    Rng rng_;
    int difficulty_ = 1, origDifficulty_ = 1;
    int timeLimit_ = 600;
    bool carTracer_ = false;
    bool sound_ = true;
    std::array<std::array<int, kCellsY>, kCellsX> traces_{};
    std::array<std::array<int, kChipRows>, kChipCols> chips_{};
    std::array<std::array<int, kLines>, 6> links_{};
    std::array<std::array<int, kLines>, 15> sig_{};
    int spare_ = 0;
    int curX_ = 3, curY_ = 2;
    int ticks_ = 0;
    int tapped_ = 0, bestTapped_ = 0;
    int alarms_ = 0, progress_ = 0;
    int crossings_ = 0;
    int fill_ = 15;               // g_gfx->fillcolor (the PAUSED window's colour)
    bool quit_ = false;
    std::vector<WiretapEvent> events_;
};

}  // namespace redux
