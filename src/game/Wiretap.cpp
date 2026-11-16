#include "game/Wiretap.h"

#include <utility>

namespace reaction {

// BUG.C: the chip truth tables, [chip][right * 2 + left][0 left out, 1 right out].
const int Wiretap::kLogic[12][4][2] = {
    {{0, 0}, {1, 0}, {1, 0}, {1, 0}},  //  0: OR, out on the left
    {{0, 0}, {0, 1}, {0, 1}, {0, 1}},  //  1: OR, out on the right
    {{0, 0}, {1, 1}, {0, 0}, {1, 1}},  //  2: split the left input
    {{0, 0}, {0, 0}, {1, 1}, {1, 1}},  //  3: split the right input
    {{0, 0}, {1, 0}, {0, 1}, {1, 1}},  //  4: straight through
    {{0, 0}, {0, 1}, {1, 0}, {1, 1}},  //  5: crossover
    {{1, 0}, {0, 0}, {1, 1}, {0, 1}},  //  6: straight, left inverted
    {{0, 1}, {1, 1}, {0, 0}, {1, 0}},  //  7: straight, right inverted
    {{1, 1}, {0, 1}, {1, 0}, {0, 0}},  //  8: straight, both inverted
    {{0, 1}, {0, 0}, {1, 1}, {1, 0}},  //  9: crossover, left input inverted
    {{1, 0}, {1, 1}, {0, 0}, {0, 1}},  // 10: crossover, right input inverted
    {{1, 1}, {1, 0}, {0, 1}, {0, 0}},  // 11: crossover, both inverted
};

// dir_dx / dir_dy: 0 up, then clockwise.
const int Wiretap::kDirX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int Wiretap::kDirY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// BOARD.C tune_data: 16 colours + border, the argument of set_palette.
const std::uint8_t Wiretap::kTunes[5][17] = {
    {0, 1, 2, 3, 15, 12, 6, 7, 8, 9, 10, 11, 12, 15, 14, 15, 0},
    {0, 1, 2, 3, 15, 12, 6, 7, 8, 9, 10, 11, 15, 12, 14, 15, 0},
    {0, 1, 2, 3, 12, 15, 6, 7, 8, 9, 10, 11, 15, 12, 14, 15, 0},
    {0, 1, 2, 3, 12, 15, 6, 7, 8, 9, 10, 11, 12, 15, 14, 15, 0},
    {0, 1, 2, 3, 4, 0, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0},
};

int WiretapSetup::boardDifficulty() const { return clampTo(difficulty - electronics + 1, 0, 3); }

int WiretapSetup::timeLimit() const {
    int t = clampTo(10 - alertLevel / 5, 2, 10) * 60;
    return carTracer ? t / 2 : t;
}

WiretapSetup WiretapSetup::fromEnv(const WorldHeader& hdr, int program) {
    WiretapSetup s;
    s.difficulty = hdr.difficulty;
    s.electronics = (hdr.skillLevels >> 6) & 3;
    s.alertLevel = hdr.var48;
    s.carTracer = program == 12;
    s.practice = (hdr.options & 0x20) != 0;
    s.sound = (hdr.options & 4) != 0;
    return s;
}

Wiretap::Wiretap(const WiretapSetup& setup) : setup_(setup), rng_(setup.seed) {
    // main(): orig_difficulty keeps the game's difficulty for the intercepted
    // information; the board uses it adjusted by the electronics skill.
    origDifficulty_ = setup.difficulty;
    difficulty_ = setup.boardDifficulty();
    timeLimit_ = setup.timeLimit();
    carTracer_ = setup.carTracer;
    sound_ = setup.sound;
}

std::vector<WiretapEvent> Wiretap::takeEvents() { return std::exchange(events_, {}); }

void Wiretap::emit(WiretapEvent::Kind k, int a, int b, int c, int d, std::string text) {
    events_.push_back(WiretapEvent{k, a, b, c, d, std::move(text)});
}

void Wiretap::sound(int n) {
    if (sound_) emit(WiretapEvent::Kind::Sound, n);
}

void Wiretap::message(const std::string& text, int fill) {
    emit(WiretapEvent::Kind::Message, fill, 0, 0, 0, text);
}

WiretapResult Wiretap::result() const {
    WiretapResult r;
    r.progress = progress_;
    r.alarms = alarms_;
    return r;
}

int Wiretap::traceAt(int x, int y) const {
    // The original indexes traces[40][25] unchecked; the board's traces never
    // lead off it, so reads outside count as no trace.
    if (x < 0 || y < 0 || x >= kCellsX || y >= kCellsY) return 0;
    return traces_[x][y];
}

void Wiretap::build(const Image& board) {
    scanTraces(board);
    generate();
}

// build_board, first part: a cell's bit d is set when the board picture has
// trace colour 10 two pixels from the cell's centre in direction d
// (board_variant, never set by BUG, would pick colour 3).
void Wiretap::scanTraces(const Image& board) {
    for (int i1 = 0; i1 < kCellsX; ++i1)
        for (int row = 0; row < kCellsY; ++row) {
            const int px = (i1 << 3) + 4, py = (row << 3) + 4;
            int bits = 0;
            for (int d = 0; d < 8; ++d) {
                const int x = kDirX[d] * 2 + px, y = kDirY[d] * 2 + py;
                if (x < board.width() && y < board.height() && board.at(x, y) == 10) bits += 1 << d;
            }
            traces_[i1][row] = bits;
        }
}

void Wiretap::resetSignals() {
    for (int i1 = 0; i1 < kLines; ++i1) {
        for (int row = 0; row < 14; ++row) sig_[row][i1] = -1;
        sig_[0][i1] = (!(i1 & 3) || (i1 & 3) == 3) ? 1 : 0;
    }
}

void Wiretap::generate() {
    const int d = difficulty_;
    // The wiring between chip columns: straight (2 in 3) or crossed; two straight
    // rows in a row swap their facing lines. Between 4d and 4d + 4 such swaps.
    do {
        crossings_ = 0;
        for (int i1 = 0; i1 < 6; ++i1) {
            bool pair = false;
            for (int row = 0; row < 5; ++row) {
                const int px = i1 * 4 + 7, py = row * 4 + 1;
                switch (rng_.below(3)) {
                case 0:
                case 1:
                    traces_[px][py] = 0x44;
                    traces_[px + 1][py] = 0x44;
                    traces_[px][py + 1] = 0x44;
                    traces_[px + 1][py + 1] = 0x44;
                    links_[i1][row * 2] = row * 2;
                    links_[i1][row * 2 + 1] = row * 2 + 1;
                    if (pair) {
                        ++crossings_;
                        traces_[px][py] = 0x41;
                        traces_[px + 1][py] = 5;
                        traces_[px][py - 1] = 0x12;
                        traces_[px + 1][py - 1] = 0x90;
                        traces_[px][py - 2] = 9;
                        traces_[px + 1][py - 2] = 0x21;
                        traces_[px][py - 3] = 0x50;
                        traces_[px + 1][py - 3] = 0x14;
                        links_[i1][row * 2] = row * 2 - 1;
                        links_[i1][row * 2 - 1] = row * 2;
                    }
                    pair = true;
                    break;
                case 2:
                    traces_[px][py] = 0x48;
                    traces_[px + 1][py] = 0x24;
                    traces_[px][py + 1] = 0x42;
                    traces_[px + 1][py + 1] = 0x84;
                    links_[i1][row * 2] = row * 2 + 1;
                    links_[i1][row * 2 + 1] = row * 2;
                    pair = false;
                    break;
                }
            }
        }
    } while (crossings_ < d * 4 || crossings_ > d * 4 + 4);
    resetSignals();
    for (int i1 = 1; i1 < 6; ++i1) {
        const int px = i1 * 4 + 7, py = 17;
        traces_[px][py + 1] |= 0x10;
        traces_[px + 1][py + 1] |= 0x10;
    }
    // The chips: 6 - d/2 lit outputs, and not too many lit pairs.
    int sum = 0, pairs = 0;
    do {
        for (int i1 = 0; i1 < kChipCols; ++i1)
            for (int row = 0; row < kChipRows; ++row) {
                int n;
                do
                    n = chips_[i1][row] = clampTo(rng_.below(d + 6) + d, 0, 11);
                while (i1 == 6 && n < 4);
            }
        boardStatus(0);
        sum = 0;
        pairs = 0;
        for (int i1 = 0; i1 < kLines; ++i1) {
            sum += sig_[13][i1];
            sig_[14][i1] = !sig_[13][i1];
            if (sig_[13][i1 & 0xE] && sig_[13][(i1 & 0xE) + 1]) ++pairs;
        }
    } while (sum != 6 - d / 2 || (pairs && pairs / 2 > 4 - d));
    // Which chips are shown.
    for (int i1 = 0; i1 < kChipCols; ++i1)
        for (int row = 0; row < kChipRows; ++row)
            if (rng_.below(3) + 1 >= d || i1 == 6) chips_[i1][row] |= kPlaced;
    // 2d locked sockets (not OR/split chips, not the sixth column). The test is on
    // the whole value, flags included, as the original's.
    for (int n = 0; n < d * 2;) {
        const int px = rng_.below(6), py = rng_.below(5);
        if (chips_[px][py] > 3 && !(chips_[px][py] & kLocked) && (px != 5 || d == 4)) {
            chips_[px][py] |= kLocked;
            if (d != 4) chips_[px][py] |= kPlaced;
            ++n;
        }
    }
    for (int i1 = 0; i1 < kChipCols; ++i1)
        for (int row = 0; row < kChipRows; ++row) {
            if (chips_[i1][row] & kLocked) emit(WiretapEvent::Kind::LockSocket, i1, row);
            if (chips_[i1][row] & kPlaced) emit(WiretapEvent::Kind::Chip, i1, row, chips_[i1][row] & 0x3F, 0);
        }
    spare_ = clampTo(rng_.below(d + 6) + d, 0, 11);
    resetSignals();
    boardStatus(1);
    curX_ = 3;
    curY_ = 2;
    drawChip(curX_, curY_, true);
}

// draw_chip for the socket under (col, row): its chip if shown, else empty.
void Wiretap::drawChip(int col, int row, bool sel) {
    const int c = chips_[col][row];
    emit(WiretapEvent::Kind::Chip, col, row, (c & kPlaced) ? (c & 0x3F) : -1, sel);
}

void Wiretap::boardStatus(int update) {
    emit(WiretapEvent::Kind::Spare, spare_ & 0x3F);
    if (update && !ticks_) traceSignal(7, 20, 0, 1);
    for (int i2 = 0; i2 < kChipCols; ++i2)
        for (int fx = 0; fx < kChipRows; ++fx) {
            const int row = i2 * 2, bx = fx * 2;
            const int in = sig_[row][bx + 1] * 2 + sig_[row][bx];
            const int chip = chips_[i2][fx] & 0x3F;
            for (int k = 0; k < 2; ++k) {
                const int old = sig_[row + 1][bx + k];
                sig_[row + 1][bx + k] = kLogic[chip][in][k];
                if (update && sig_[row + 1][bx + k] != old)
                    traceSignal(i2 * 4 + 7, fx * 4 + 1 + k, 2, sig_[row + 1][bx + k]);
                if (i2 < 6) sig_[row + 2][links_[i2][bx + k]] = sig_[row + 1][bx + k];
            }
        }
    if (update) {
        tapped_ = 0;
        for (int i2 = 0; i2 < kLines; ++i2) {
            const int old = sig_[13][i2];
            if (sig_[14][i2]) {        // an alarm line
                emit(WiretapEvent::Kind::Output, i2, old ? 0x12 : 2);
                if (old) ++alarms_;
            } else {                   // a phone line / tracer circuit
                if (tapped_ == bestTapped_ && !old) sound(0x1D);
                if (!carTracer_) {
                    emit(WiretapEvent::Kind::Output, i2, old ? 0x22 : 0x32);
                    if (!old && ++tapped_ > bestTapped_) {
                        bestTapped_ = tapped_;
                        if (!setup_.practice) {
                            // show_intercepted_info(8)
                            emit(WiretapEvent::Kind::Palette, 4);
                            emit(WiretapEvent::Kind::Intercept, 8, intercept ? intercept(8) : -1);
                            emit(WiretapEvent::Kind::Palette, ticks_ & 3);
                        }
                    }
                } else {
                    emit(WiretapEvent::Kind::Output, i2, old ? 0x42 : 0x52);
                    // The original also formats "Car tracer N% complete" here,
                    // between saving and restoring the screen: nothing is shown.
                    if (!old && ++tapped_ > bestTapped_) bestTapped_ = tapped_;
                }
            }
        }
    }
    fill_ = 1;   // g_gfx->fillcolor = 1: the alarm window is blue
    if (alarms_) {
        emit(WiretapEvent::Kind::Palette, 0);
        sound(0x1E);
        message("Alarm!!!\n", 1);
        progress_ = 0;
        quit_ = true;
    }
    if ((!carTracer_ ? 6 - difficulty_ / 2 : 5) <= tapped_ || timeLimit_ == 0) {
        std::string msg;
        if (timeLimit_) {
            if (!carTracer_) {
                if (!setup_.practice) {
                    emit(WiretapEvent::Kind::Palette, 4);
                    emit(WiretapEvent::Kind::Intercept, origDifficulty_ << 4,
                         intercept ? intercept(origDifficulty_ << 4) : -1);
                    emit(WiretapEvent::Kind::Palette, ticks_ & 3);
                }
                msg = "Phone taps completed.\n";
            } else {
                msg = "Car tracer installed.\n";
            }
            sound(0x2E);
        } else {
            msg = "Time expired.\n";
            if (!carTracer_) msg += std::to_string(tapped_) + " phones tapped.\n";
        }
        msg += alarms_ ? std::to_string(alarms_) : std::string("No");
        msg += " Alarms\n";
        fill_ = 15;
        message(msg, 15);
        progress_ = tapped_;
        quit_ = true;
    }
}

// Follows the signal from cell (x, y), entered going `dir`, along the traces,
// marking and drawing each cell lit or not; branches recurse. After the first
// frame every cell takes a tick and a retrace (while 2 seconds or more are left).
void Wiretap::traceSignal(int x, int y, int dir, int lit) {
    int mask;
    while ((mask = traceAt(x, y)) != 0 && dir != -1) {
        traces_[x][y] = (mask & 0xFF) + (lit ? 0x100 : 0);
        emit(WiretapEvent::Kind::TraceCell, x, y, traces_[x][y], dir);
        int first = -1;
        for (int i = 0; i < 8; ++i) {
            if (((dir + 4) & 7) == i) continue;
            if (!(mask & (1 << i))) continue;
            if (first == -1) first = i;
            else traceSignal(x + kDirX[i], y + kDirY[i], i, lit);
        }
        dir = first;
        if (dir != -1) {   // (the original adds dir_dx[-1], then stops)
            x += kDirX[dir];
            y += kDirY[dir];
        }
        if (ticks_ && timeLimit_ > 1) {
            advanceTimer(1);
            emit(WiretapEvent::Kind::Wait, 1);
        }
    }
}

// A second every 8 ticks; the palette cycles the lit traces' colours.
void Wiretap::advanceTimer(int n) {
    for (int i = 0; i < n; ++i) {
        ++ticks_;
        if (!(ticks_ & 7)) {
            --timeLimit_;
            emit(WiretapEvent::Kind::Timer, timeLimit_);
            fill_ = 0;   // draw_timer sets g_gfx->fillcolor = 0
            // board_status() is called without an argument: its `update` is the
            // caller's loop counter, which sits in that stack slot.
            if (timeLimit_ == 0) boardStatus(i);
        }
        emit(WiretapEvent::Kind::Palette, ticks_ & 3);
    }
}

void Wiretap::frame(int key) {
    using namespace bugkeys;
    advanceTimer(1);
    int dx = 0, dy = 0;
    bool move = false;
    switch (key) {
    case kEsc:
        progress_ = tapped_;
        quit_ = true;
        break;
    case kEnter:
    case kSpace: {
        const int x = curX_, y = curY_;
        if (chips_[x][y] & kLocked) break;
        if (!(chips_[x][y] & kPlaced)) {   // reveal the hidden chip: 10 ticks
            emit(WiretapEvent::Kind::Chip, x, y, chips_[x][y] & 0x3F, 1);
            advanceTimer(10);
        }
        const int saved = spare_;
        spare_ = chips_[x][y] & 0x3F;
        chips_[x][y] = saved + kPlaced;
        drawChip(x, y, true);
        sound(0x1C);
        boardStatus(1);
        break;
    }
    case kUp: dx = 0, dy = -1, move = true; break;
    case kPgUp: dx = 1, dy = -1, move = true; break;
    case kRight: dx = 1, dy = 0, move = true; break;
    case kPgDn: dx = 1, dy = 1, move = true; break;
    case kDown: dx = 0, dy = 1, move = true; break;
    case kEnd: dx = -1, dy = 1, move = true; break;
    case kLeft: dx = -1, dy = 0, move = true; break;
    case kHome: dx = -1, dy = -1, move = true; break;
    case kAltQ:
        fill_ = 15;
        emit(WiretapEvent::Kind::QuitPrompt);
        break;
    case kAltS:
        if (sound_) emit(WiretapEvent::Kind::Sound, 0);
        sound_ = !sound_;
        break;
    case kAltP: emit(WiretapEvent::Kind::Pause, fill_); break;
    default: break;
    }
    if (move) {
        drawChip(curX_, curY_, false);
        curX_ = clampTo(dx + curX_, 0, 5);
        curY_ = clampTo(dy + curY_, 0, 4);
        drawChip(curX_, curY_, true);
    }
    emit(WiretapEvent::Kind::Wait, 5);
    if (quit_) {   // main() after the loop
        sound(0);
        emit(WiretapEvent::Kind::Palette, 4);
    }
}

}  // namespace reaction
