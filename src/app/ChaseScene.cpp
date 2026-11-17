// The car chase (CHASE.EXE): covert-decomp src/CHASE/CHASE.C, DRIVE.C, CITY.C.
// The logic is game/Chase; this is the screen, drawn the way the original draws
// it (into persistent pages, piece by piece, with the same retrace waits), and
// its input loop.
//
// Pages, as in the original: 0 the screen, 1 the back page (cars.pic, then the
// street view's tiles and composition), 2 street.pic (the windshield scenes,
// 2 columns x 4 rows of 116x44), 3 chase.pic (car icons, the 16 street tiles,
// the 16 sizes of the car seen through the windshield, the dashboard).
//
// Layout (chase_palette: colour 0 shows as yellow):
//   - car selection (car_select_screen, not when you are chased): cars.pic's
//     four rows of 50 pixels, each a car offer recoloured by its visibility,
//     its specs at x 256; the row under the cursor blinks (colour 1 <-> 9);
//   - left 200x200: the city map, blocks of 8 pixels (map position / 4);
//   - top right 120x100: the dashboard (chase.pic 200,101): the windshield at
//     (201, 1) 116x44, the speed at (230, 75), the clock at (290, 75), the
//     follow / tracker / heat lights;
//   - bottom right 120x100: the street view at (200, 100), scrolling over the
//     tiles around the car you drive.
//
// Keys (chase_loop, read once per loop after the first 32 loops): arrows ask
// the next turn (the opposite way: a U-turn, for cars with excellent handling),
// '=' faster, '-' slower, Space switches between Max's two cars, F10 follow
// the suspect, F1 / 'a' arrest (when right behind him), F2 / 'e' into the
// building (when at one), 'f' / 'd' / 's' fast / normal / slow, Esc give up
// (not when chased), Alt-Q quit, Alt-S sound, Alt-P pause. Keys typed meanwhile
// wait in a 15-key buffer like the BIOS's.
//
// Timing: one chase_loop, then wait_retrace(8 slow / 4 normal / 0 fast). With
// 0 the original runs as fast as the PC draws; here that is one retrace.
#include <SDL.h>

#include <array>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/DosKey.h"
#include "app/FrontEnd.h"
#include "app/Menu.h"
#include "app/Scenes.h"
#include "formats/McgaPalette.h"
#include "formats/Pic.h"
#include "formats/TextWindow.h"
#include "app/MiniGames.h"
#include "game/Chase.h"
#include "game/World.h"

namespace reaction {

namespace {

constexpr int kF1 = 0x3B00, kAltQ = 0x1000, kAltS = 0x1F00, kAltP = 0x1900;

// The get_key() code of an SDL key press; -1 for none. DosKey.h's codes plus
// what the chase reads beyond them: F2..F10 and Alt-Q / S / P.
int chaseKey(const SDL_Event& e) {
    if (e.type != SDL_KEYDOWN) return -1;
    const SDL_Keycode k = e.key.keysym.sym;
    if (e.key.keysym.mod & KMOD_ALT) {
        if (k == SDLK_q) return kAltQ;
        if (k == SDLK_s) return kAltS;
        if (k == SDLK_p) return kAltP;
        if (k == SDLK_RETURN) return -1;     // Alt+Enter: full screen (Screen)
    }
    if (k >= SDLK_F1 && k <= SDLK_F10) return kF1 + static_cast<int>(k - SDLK_F1) * 0x100;
    if (k == SDLK_KP_MINUS) return '-';
    if (k == SDLK_KP_PLUS) return '+';       // as on DOS: not '=', so not "faster"
    return dosKey(e);
}

// The rectangle of a car shape on chase.pic (car_shape_rect), 16 sizes.
constexpr int kCarShapeRect[16][4] = {
    {5, 84, 53, 116},   {8, 117, 50, 145},  {10, 146, 48, 171}, {12, 172, 46, 194},
    {71, 84, 102, 104}, {74, 106, 100, 123}, {75, 126, 99, 142}, {77, 143, 97, 156},
    {78, 157, 96, 168}, {80, 169, 94, 178}, {81, 179, 94, 187}, {81, 188, 92, 195},
    {60, 169, 68, 175}, {61, 176, 68, 180}, {62, 181, 67, 184}, {63, 185, 66, 187},
};
// edge_xw, edge_yh: the strips drawn over a tile where a block side has no road.
constexpr int kEdgeXW[4][2] = {{2, 28}, {30, 2}, {2, 28}, {0, 2}};
constexpr int kEdgeYH[4][2] = {{0, 2}, {2, 28}, {30, 2}, {2, 28}};
// grade_colors: the paint of a car offer by visibility (colours 11 and 3).
constexpr int kGradeColors[4][2] = {{7, 8}, {10, 2}, {15, 7}, {12, 4}};

// drv_copy_rect on 320x200 pages: rows addressed linearly (y * 320 + x), as the
// driver does, so a negative x reads the end of the row above.
void copyRect(const Image& src, int sx, int sy, int w, int h, Image& dst, int dx, int dy) {
    std::vector<std::uint8_t> tmp(static_cast<std::size_t>(std::max(0, w * h)));
    const auto& sp = src.pixels();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const long o = static_cast<long>(sy + y) * 320 + sx + x;
            tmp[static_cast<std::size_t>(y * w + x)] =
                o >= 0 && o < static_cast<long>(sp.size()) ? sp[static_cast<std::size_t>(o)] : 0;
        }
    auto& dp = dst.pixels();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const long o = static_cast<long>(dy + y) * 320 + dx + x;
            if (o >= 0 && o < static_cast<long>(dp.size()))
                dp[static_cast<std::size_t>(o)] = tmp[static_cast<std::size_t>(y * w + x)];
        }
}

class ChaseScene : public Scene {
public:
    using OnDone = std::function<std::unique_ptr<Scene>(App&, const ChaseResult&)>;

    ChaseScene(App& app, const ChaseSetup& setup, OnDone onDone)
        : app_(app), game_(setup), onDone_(std::move(onDone)), screen_(320, 200, 0), back_(320, 200, 0),
          street_(320, 200, 0), sprites_(320, 200, 0) {
        auto load = [&](const char* name, Image& page) {
            auto pic = decodePicture(app.data.read(name));
            page.blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
        };
        load("chase.pic", sprites_);
        for (int i = 0; i < 16; ++i) {
            const auto& r = kCarShapeRect[i];
            Image img(r[2] - r[0] + 1, r[3] - r[1] + 1, 0);
            img.blit(sprites_, r[0], r[1], img.width(), img.height(), 0, 0);
            shapes_[static_cast<std::size_t>(i)] = std::move(img);
        }
        load("street.pic", street_);
        setPalette(palettes::kChase);
        if (game_.mode() != 2) startSelect();
        else startChase();
    }

    void handle(const SDL_Event& e) override {
        if (menu_) {
            menu_->handle(e);
            return;
        }
        const int k = chaseKey(e);
        if (k < 0) return;
        if (keys_.size() < 15) keys_.push_back(k);
    }

    void tick() override {
        if (menu_) {
            menu_->tick();
            if (auto r = menu_->result()) {
                menu_->draw(screen_);   // text_window leaves the window on the screen
                menu_.reset();
                menuClosed(*r);
            }
            return;
        }
        if (wait_ > 0 && --wait_ > 0) return;
        for (int guard = 0; wait_ == 0 && !menu_ && guard < 1000; ++guard)
            if (!step()) break;
        playSounds();
    }

    // The play_sound calls of this tick, to the driver (with sound_param).
    void playSounds() {
        for (const auto& s : game_.takeSounds()) app_.driverSound(s[0], s[1]);
    }

    void draw(Frame& f) override {
        f.palette = palette_;
        f.image = screen_;
        if (menu_) menu_->draw(f.image);
    }

    std::unique_ptr<Scene> next() override {
        if (state_ != St::Done) return nullptr;
        if (result_.quitGame) {
            quit = true;
            return nullptr;
        }
        if (onDone_) return onDone_(app_, result_);
        return makeMainMenuScene(app_);       // practice: back to FINAL's menu
    }

private:
    enum class St { Select, SelectBlink, Loop, Turn, QuitMenu, Paused, Message, Done };

    void setPalette(const PaletteMap& m) { palette_ = mcgaPalette(m); }

    // --- drawing primitives with the original's arguments ---
    void text(int font, std::string_view s, int x, int y, int color, bool opaque = false, int bg = 5,
              Image* page = nullptr) {
        TextAttr a;
        a.textbg = opaque ? 1 : 0;
        a.color = static_cast<std::uint8_t>(color);
        a.background = static_cast<std::uint8_t>(bg);
        app_.fonts.font(font).draw(page ? *page : screen_, x, y, s, a);
    }
    static void line(Image& img, int x1, int y1, int x2, int y2, int c) {   // h/v lines and points
        fillRect(img, std::min(x1, x2), std::min(y1, y2), std::abs(x2 - x1) + 1, std::abs(y2 - y1) + 1,
                 static_cast<std::uint8_t>(c));
    }
    void fillXY(int x1, int y1, int x2, int y2, int c) {                   // fill_rect_xy
        fillRect(screen_, x1, y1, x2 - x1 + 1, y2 - y1 + 1, static_cast<std::uint8_t>(c));
    }
    void mapDot(int x, int y, int c) {                                      // draw_map_dot
        const int px = x * 8 / 32, py = y * 8 / 32;
        line(screen_, px, py, px, py, c);
        line(screen_, px + 1, py, px + 1, py, c);
        line(screen_, px, py + 1, px, py + 1, c);
        line(screen_, px + 1, py + 1, px + 1, py + 1, c);
    }
    void recolor(int x, int y, int w, int h, int from, int to) {
        recolorRect(screen_, x, y, w, h, static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to));
    }

    // --- car_select_screen ---
    void startSelect() {
        game_.playSound(0x27);                             // the car select tune
        // load_picture_swapped("cars.pic", 1)
        auto pic = decodePicture(app_.data.read("cars.pic"));
        back_.blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
        copyRect(back_, 0x9A, 0, 0xA6, 200, screen_, 0x9A, 0);
        offers_ = game_.rollOffers();
        std::array<bool, 4> used{};
        for (int i = 0; i < 4; ++i) {
            const CarOffer& o = offers_[static_cast<std::size_t>(i)];
            const int c = o.bits;
            copyRect(back_, 0, o.sprite * 50, 0x9A, 50, screen_, 0, i * 50);
            if (!used[static_cast<std::size_t>(o.paint)] && !game_.cga()) {
                recolor(0, i * 50, 0x9A, 50, 11, kGradeColors[o.paint][0]);
                recolor(0, i * 50, 0x9A, 50, 3, kGradeColors[o.paint][1]);
            }
            used[static_cast<std::size_t>(o.paint)] = true;
            int y = i * 50 + 11;
            if (c & 4) text(1, "(Tracking)", 256, y, 15);
            y += 9;
            text(1, std::to_string((c & 3) * 20 + 40) + " mph", 256, y, 15);
            y += 9;
            text(1, (c & 8) ? "Excellent" : "Fair", 256, y, 15);
            y += 9;
            static const char* const kVis[4] = {"Low", "Moderate", "High", "Extreme"};
            text(1, kVis[(c & 0x30) >> 4], 256, y, 15);
        }
        keys_.clear();                        // flush_input
        picked_ = -1;
        pick_ = 1;
        startPick();
    }
    void highlight(bool on) {
        if (game_.cga()) {
            drawRectOutline(screen_, 0, slot_ * 50 + 9, 319, 37, on ? 15 : 0);
            return;
        }
        if (on) recolor(0, slot_ * 50, 320, 50, 1, 9);
        else recolor(0, slot_ * 50, 320, 50, 9, 1);
    }
    void startPick() {
        slot_ = !picked_ ? 1 : 0;
        highlight(true);
        state_ = St::Select;
    }
    bool stepSelect() {
        if (state_ == St::SelectBlink) {      // the second half of the blink
            highlight(true);
            wait_ = 10;
            state_ = St::Select;
            return true;
        }
        if (keys_.empty()) {
            highlight(false);
            wait_ = 10;
            state_ = St::SelectBlink;
            return true;
        }
        highlight(false);
        const int k = popKey();
        bool done = false;
        switch (k) {
        case 0x5000: if (slot_ < 3) ++slot_; break;
        case 0x4800: if (slot_ > 0) --slot_; break;
        case 13: case ' ': done = true; break;
        default: break;
        }
        if (slot_ == picked_) {
            if ((k == 0x5000 && slot_ < 3) || slot_ == 0) ++slot_;
            else --slot_;
        }
        highlight(true);
        if (!done) return true;
        game_.setTeamCar(pick_, offers_[static_cast<std::size_t>(slot_)].bits);
        picked_ = slot_;
        text(1, "Car #" + std::to_string(pick_), 0x9A, slot_ * 50 + 9, 15);
        if (++pick_ <= 2) {
            startPick();
            return true;
        }
        game_.playSound(1);
        startChase();
        return true;
    }

    // --- generate_city_map's drawing, init_cars ---
    void startChase() {
        game_.generateMap();
        const int color = game_.mapColor();
        fillXY(0, 0, 199, 199, 1);
        fillXY(1, 1, 198, 198, color);
        fillXY(200, 0, 319, 99, 1);
        copyRect(sprites_, 200, 101, 120, 100, screen_, 200, 0);
        line(screen_, 200, 0, 319, 0, 15);
        for (int i = 8; i < 200; i += 8) {
            line(screen_, i, 1, i, 198, 5);
            line(screen_, i + 1, 1, i + 1, 198, 5);
        }
        for (int j = 8; j < 200; j += 8) {
            line(screen_, 1, j, 198, j, 5);
            line(screen_, 1, j + 1, 198, j + 1, 5);
        }
        for (const auto& w : game_.walls()) {
            const int s = w.side, o = s & 1;
            line(screen_, Chase::kWallX[s][0] + w.x * 8, Chase::kWallY[s][0] + w.y * 8,
                 Chase::kWallX[s][1] + w.x * 8, Chase::kWallY[s][1] + w.y * 8, color);
            line(screen_, Chase::kWallX[s][0] + o + w.x * 8, Chase::kWallY[s][0] + w.y * 8 - o + 1,
                 Chase::kWallX[s][1] + o + w.x * 8, Chase::kWallY[s][1] + w.y * 8 - o + 1, color);
        }
        for (const auto& l : game_.labels()) {
            mapDot((l.building & 1 ? 8 : -8) + l.x, l.y, 15);
            text(2, game_.labelText(l.building), l.x * 8 / 32 + 4, l.y * 8 / 32 - 3, game_.labelColor());
        }
        game_.initCars();
        state_ = St::Loop;
    }

    // --- the main loop ---
    bool step() {
        switch (state_) {
        case St::Select:
        case St::SelectBlink:
            return stepSelect();
        case St::Loop: {
            if (game_.loopStart()) keys_.clear();          // flush_input at chase_clock 32
            if (!keys_.empty() && game_.readsKeys()) {
                switch (game_.key(popKey())) {
                case Chase::KeyAction::Return:
                    endLoop();
                    return true;
                case Chase::KeyAction::QuitPrompt:
                    keys_.clear();
                    state_ = St::QuitMenu;
                    menu_.emplace(app_.fonts.font(2), "Are you sure\nyou want to Quit?\n No\n Yes\n", 0xCA, 4).setHotkeys(hotkeys());
                    return true;
                case Chase::KeyAction::Pause:
                    game_.playSound(0x25);                 // get_key()
                    state_ = St::Paused;
                    return true;
                case Chase::KeyAction::None:
                    break;
                }
                game_.afterKey();
            }
            moveCars();
            return true;
        }
        case St::Paused:
            if (keys_.empty()) return false;
            popKey();
            game_.playSound(0x26);
            game_.afterKey();
            moveCars();
            return true;
        case St::Turn:
            return stepTurn();
        case St::QuitMenu:
        case St::Message:
        case St::Done:
            return false;
        }
        return false;
    }

    void moveCars() {
        game_.moveCars();
        for (const auto& d : game_.dots()) mapDot(d.x, d.y, d.color);
        if (!game_.turns().empty()) {
            turnIndex_ = 0;
            startTurn();
            return;
        }
        finishLoop();
    }

    // turn_animation: the windshield scene slides sideways, in the text palette.
    void startTurn() {
        const auto& t = game_.turns()[static_cast<std::size_t>(turnIndex_)];
        turnFrames_.clear();
        const int step = 32 / (game_.gameSpeed() + 2);
        if (((t.dir - 1) & 3) == t.newDir) {
            int x = 8;
            do {
                turnFrames_.push_back(x);
                x += step;
            } while (x < 116);
        }
        if (((t.dir + 1) & 3) == t.newDir) {
            int x = 108;
            do {
                turnFrames_.push_back(x);
                x -= step;
            } while (x > 8);
        }
        turnFrame_ = 0;
        setPalette(palettes::kGame);                       // text_palette
        if (!turnFrames_.empty()) game_.playSound(23);
        state_ = St::Turn;
    }
    bool stepTurn() {
        const auto& turns = game_.turns();
        if (turnFrame_ < static_cast<int>(turnFrames_.size())) {
            const int sc = turns[static_cast<std::size_t>(turnIndex_)].scene;
            const int x = turnFrames_[static_cast<std::size_t>(turnFrame_++)];
            copyRect(street_, sc / 4 * 125, sc % 4 * 47, 116 - x, 44, screen_, x + 201, 1);
            copyRect(street_, sc / 4 * 125 - x + 116, sc % 4 * 47, x, 44, screen_, 201, 1);
            wait_ = 2;
            return true;
        }
        setPalette(palettes::kChase);
        if (++turnIndex_ < static_cast<int>(turns.size())) {
            startTurn();
            return true;
        }
        state_ = St::Loop;
        finishLoop();
        return true;
    }

    void finishLoop() {
        drawStreetView(game_.curCar(), 200, 100);
        drawDashboard();
        endLoop();
    }

    void endLoop() {
        game_.endLoop();
        state_ = St::Loop;
        if (game_.chaseOver()) {
            finishChase(false);
            return;
        }
        wait_ = std::max(1, game_.loopWait());
    }

    // text_window's Alt keys in CHASE's windows: its own options word (CHASE
    // doesn't save env.sve: the toggles last for the chase).
    TextWindowHotkeys hotkeys() {
        TextWindowHotkeys h;
        h.options = [this] { return static_cast<int>(static_cast<std::int16_t>(game_.options())); };
        h.setOptions = [this](int v) { game_.setOptions(static_cast<std::uint16_t>(v)); };
        h.sound = [this](int n) {
            game_.playSound(n);
            playSounds();
        };
        h.quit = [this] { quit = true; };
        return h;
    }

    void finishChase(bool quitGame) {
        result_ = game_.result(quitGame);
        if (quitGame) {
            state_ = St::Done;
            return;
        }
        game_.playSound(0);                                // main, after the loop
        playSounds();
        // drv_set_palette(text_palette); text_window(message, 100, 80)
        setPalette(palettes::kGame);
        keys_.clear();
        state_ = St::Message;
        menu_.emplace(app_.fonts.font(1), result_.message, 100, 80).setHotkeys(hotkeys());
    }

    void menuClosed(int r) {
        if (state_ == St::QuitMenu) {
            if (r == 1) {
                finishChase(true);
                return;
            }
            state_ = St::Loop;
            game_.afterKey();
            moveCars();
            // keep running the program this tick, like the original after the window
            for (int guard = 0; wait_ == 0 && !menu_ && guard < 1000; ++guard)
                if (!step()) break;
            return;
        }
        if (state_ == St::Message) state_ = St::Done;
    }

    int popKey() {
        const int k = keys_.front();
        keys_.pop_front();
        return k;
    }

    // --- draw_street_view ---
    void drawCarIcon(int x, int y, int flag, int dir) {
        if (game_.mode() == 2) flag = !flag;
        if (dir & 1) copyRect(sprites_, dir * 8 + 1, flag ? 10 : 2, 6, 4, back_, x - 3, y - 2);
        else copyRect(sprites_, dir * 8 + 2, flag ? 9 : 1, 4, 6, back_, x - 2, y - 3);
    }

    void drawStreetView(int n, int px, int py) {
        const ChaseCar& me = game_.car(n);
        const int tx = clampTo(me.x - 60, 0, 648);
        const int ty = clampTo(me.y - 50, 0, 668);
        if (viewX_ == 0) {
            viewX_ = tx;
            viewY_ = ty;
        }
        const int heatLimit = game_.heatLimit(), clock = game_.clock();
        do {
            viewX_ += clampTo(tx - viewX_, -4, 4);
            viewY_ += clampTo(ty - viewY_, -4, 4);
            const int bx = viewX_ / 32, by = viewY_ / 32;
            if (bx != tileX_ || by != tileY_) {
                for (int x = 0; x < 5; ++x)
                    for (int y = 0; y < 5; ++y) {
                        // city_map[bx + x][by + y]: at the east / south edge the
                        // index runs past the column (into the next one) or the
                        // array (unknown memory: no roads, tile 0 here).
                        const int lin = (bx + x) * Chase::kBlocks + by + y;
                        const int m = lin < Chase::kBlocks * Chase::kBlocks
                                          ? game_.map(lin / Chase::kBlocks, lin % Chase::kBlocks) : 0;
                        int sx = (m >> 4) * 32, sy = 17;
                        while (sx >= 320) {
                            sx -= 320;
                            sy += 32;
                        }
                        copyRect(sprites_, sx, sy, 32, 32, back_, x * 32, y * 32);
                        for (int k = 0; k < 4; ++k)
                            if (!((1 << k) & m))
                                fillRect(back_, x * 32 + kEdgeXW[k][0], y * 32 + kEdgeYH[k][0], kEdgeXW[k][1],
                                         kEdgeYH[k][1], 7);
                    }
                tileX_ = bx;
                tileY_ = by;
            }
            copyRect(back_, viewX_ - bx * 32, viewY_ - by * 32, 120, 100, back_, px, py);
            for (int k = 0; k < 3; ++k) {
                const ChaseCar& c = game_.car(k);
                if (k != 0 && c.heat > heatLimit * 2 && (clock & 2)) continue;
                const bool leader = game_.mode() == 2 ? k > 0 : k == 0;
                int sx = 0, sy = 0, frame = 0;
                if (!leader) {
                    sx = c.x - viewX_;
                    sy = c.y - viewY_;
                    frame = (c.heat > heatLimit ? 0 : k == game_.curCar() && !(clock & 7) ? 8 : 4) + c.dir;
                    if ((clock & 4) && c.req != -1) {
                        if (((c.dir + 1) & 3) == c.req) frame += 12;
                        if (((c.dir - 1) & 3) == c.req) frame += 24;
                    }
                } else {
                    sx = c.tx - viewX_;
                    sy = c.ty - viewY_;
                    frame = (c.follow ? 0 : 4) + c.req;
                }
                if (sx > 4 && sx < 116 && sy > 4 && sy < 96) drawCarIcon(sx + px, sy + py, k, frame);
            }
            line(back_, px, py, px + 119, py, 15);
            line(back_, px + 119, py + 99, px + 119, py, 15);
            line(back_, px + 119, py + 99, px, py + 99, 15);
            line(back_, px, py, px, py + 99, 15);
            copyRect(back_, px, py, 120, 100, screen_, px, py);
        } while (tx != viewX_ || ty != viewY_);
    }

    // --- draw_dashboard ---
    void drawScene(int sc) {
        copyRect(street_, sc / 4 * 125, sc % 4 * 47, 116, 44, screen_, 201, 1);
        copyRect(sprites_, 202, 145, 116, 24, screen_, 202, 45);
    }
    // gate_thunk_b (draw_image) on the windshield port: relative to (201, 1),
    // colour 0 transparent, clipped at the port's right (318) and bottom (75).
    void drawShape(int x, int y, const Image& img) {
        for (int r = 0; r < img.height(); ++r)
            for (int c = 0; c < img.width(); ++c) {
                const int rx = x + c, ry = y + r;
                if (rx < 0 || ry < 0 || rx > 318 || ry > 75) continue;
                const std::uint8_t px = img.at(c, r);
                if (px) screen_.set(201 + rx, 1 + ry, px);
            }
    }

    void drawDashboard() {
        int sc = game_.scene();
        if (sc > 7) game_.setScene(sc = 0);
        if (sc != lastScene_) drawScene(sc);
        const ChaseCar& me = game_.car(game_.curCar());
        int dist = game_.carInView(game_.curCar());
        if (dist && game_.carAhead()) {
            dist += dist / 2;
            const int size = clampTo(dist / 4 - 2, 0, 15);
            const int nearv = 63 - clampTo(dist, 0, 63);
            const int dy = ((nearv * nearv >> 2) * 20 >> 10) + 19;
            int dx = 61;
            if (me.dir != game_.car(0).dir) dx -= (nearv * nearv >> 2) * 20 >> 7;
            if (dy != lastDashY_ || sc != lastScene_) {
                if (sc == lastScene_) drawScene(sc);
                drawShape(dx - (kCarShapeRect[size][2] - kCarShapeRect[size][0]) / 2, dy,
                          shapes_[static_cast<std::size_t>(size)]);
            }
            lastDashY_ = dy;
        } else {
            if (lastDashY_ != -1 && sc == lastScene_) drawScene(sc);
            lastDashY_ = -1;
        }
        lastScene_ = sc;
        const int clock = game_.clock();
        text(2, std::to_string(me.speed * 20), 230, 75, 7, true, 5);
        const int heat = clampTo(me.heat * 5 / game_.heatLimit(), 0, 10) * 3;
        if (!(clock & 3)) {
            copyRect(sprites_, 288, 186, 30, 2, screen_, 288, 85);
            if (heat && (!(clock & 7) || heat < 30)) recolor(288, 84, heat, 2, 7, 14);
        }
        text(2, game_.clockText(), 290, 75, 9, true, 5);
        if (!(clock & 3)) {
            copyRect(sprites_, 275, 185, 10, 10, screen_, 275, 84);
            if (!(clock & 4) && me.follow == 1) recolor(276, 85, 6, 7, 1, game_.cga() ? 15 : 7);
        }
        if (!(clock & 3)) {
            copyRect(sprites_, 273, 175, 14, 10, screen_, 273, 74);
            if (me.bits & 4) {                // the tracker: which quarter the suspect is in
                if (clock & 4) {
                    const int ddx = game_.car(0).x - me.x, ddy = game_.car(0).y - me.y;
                    if (ddx >= 0 && ddy <= 0) recolor(280, 74, 4, 4, 5, 11);
                    if (ddx >= 0 && ddy >= 0) recolor(280, 79, 4, 4, 5, 11);
                    if (ddx <= 0 && ddy <= 0) recolor(273, 74, 4, 4, 5, 11);
                    if (ddx <= 0 && ddy >= 0) recolor(273, 79, 4, 4, 5, 11);
                } else {
                    const int px = Chase::kDx[static_cast<std::size_t>(me.dir)] + 278;
                    const int py = Chase::kDy[static_cast<std::size_t>(me.dir)] + 78;
                    line(screen_, px, py, px, py, 9);
                }
            }
        }
    }

    App& app_;
    Chase game_;
    OnDone onDone_;
    ChaseResult result_;
    Image screen_, back_, street_, sprites_;
    std::array<Image, 16> shapes_;
    Palette palette_;
    std::optional<Menu> menu_;
    std::deque<int> keys_;
    St state_ = St::Loop;
    int wait_ = 0;
    // car selection
    std::array<CarOffer, 4> offers_{};
    int picked_ = -1, pick_ = 1, slot_ = 0;
    // turn animation
    std::vector<int> turnFrames_;
    int turnFrame_ = 0, turnIndex_ = 0;
    // street view and dashboard state (view_x/y, tile_x/y, last_scene, last_dash_y)
    int viewX_ = 0, viewY_ = 0, tileX_ = 0, tileY_ = 0;
    int lastScene_ = -1, lastDashY_ = -1;
};

int envInt(const char* name, int fallback) {
    if (const char* v = std::getenv(name); v && *v) return std::atoi(v);
    return fallback;
}

// Practice: what FINAL's start_practice(1) leaves in the env block: ENV[0] = 14,
// options |= 20h, skill levels 555h (driving 1: no difficulty change; normal
// speed), here_org = random(12) + 4 (the suspect's destination), here_city =
// random(16), load_world_file(random(3)) (the city's known buildings and the
// organizations' names); FINAL's options and clock_base (0 on a fresh start, so
// the practice chase is at night). Development settings: COVERT_CHASE_SEED
// (FINAL's and CHASE's random seed), COVERT_CHASE_LEVEL (difficulty 0-3),
// COVERT_CHASE_CITY (0-15), COVERT_CHASE_TIME (clock, minutes),
// COVERT_CHASE_MODE (ENV[0]: 3/14 follow, 6 you are chased), COVERT_CHASE_ORGMASK
// (the city's known buildings, e.g. 65535: all labelled).
std::unique_ptr<Scene> makePracticeChaseScene(App& app, int mode) {
    ChaseSetup setup;
    const auto seed = static_cast<std::uint32_t>(
        envInt("COVERT_CHASE_SEED", static_cast<int>(std::time(nullptr) & 0x7FFF)));
    Rng finalRng(seed);
    setup.mode = envInt("COVERT_CHASE_MODE", mode);
    setup.targetBuilding = finalRng.below(12) + 4;
    setup.cityId = clampTo(envInt("COVERT_CHASE_CITY", finalRng.below(16)), 0, 15);
    World world;
    world.loadWorld(app.data, finalRng.below(3));
    // (worldN.dta's cities know no buildings: no labels, nothing to escape into)
    setup.orgMask = static_cast<std::uint16_t>(
        envInt("COVERT_CHASE_ORGMASK", world.cities[static_cast<std::size_t>(setup.cityId)].orgsKnown));
    for (std::size_t i = 0; i < 16; ++i) setup.orgAbbrev[i] = world.orgs[i].abbrev.str();
    setup.difficulty = clampTo(envInt("COVERT_CHASE_LEVEL", app.practiceLevel >= 0 ? app.practiceLevel : 0), 0, 3);
    setup.gameTime = static_cast<std::uint16_t>(
        envInt("COVERT_CHASE_TIME", static_cast<std::uint16_t>(app.game.world.hdr.clockBase)));   // FINAL's clock_base
    setup.options = static_cast<std::uint16_t>(app.game.world.hdr.options | 0x20);   // FINAL's options | 20h
    setup.envFlags = 0x555;
    setup.seed = seed;
    return std::make_unique<ChaseScene>(app, setup, nullptr);
}

}  // namespace

std::unique_ptr<Scene> makeChaseScene(App& app, const ChaseSetup& setup, ChaseDone onDone) {
    return std::make_unique<ChaseScene>(app, setup, std::move(onDone));
}

REACTION_SCENE("chase", [](App& app) { return makePracticeChaseScene(app, 14); });
REACTION_SCENE("chase-escape", [](App& app) { return makePracticeChaseScene(app, 6); });

}  // namespace reaction
