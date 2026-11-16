// TAC.EXE, the building break-in (covert-decomp src/TAC: TAC.C, BREAKIN.C,
// ACTION.C, ROOM.C, ROOMVIEW.C, GUARD.C), ported function for function.
//
// The original draws as it goes into four 320x200 pages (0 the screen, 1 the
// back page, 2 the sprite page, 3 the work page) and blocks inside its
// functions (wait_retrace, get_key, text_window). This port keeps both: the
// pages are Images here (TacGfx), and the blocking functions are coroutines
// (game/TacTask.h) that suspend on a retrace count, a key or a menu. The scene
// (app/TacScene.cpp) calls tick() once per retrace, feeds keys into the
// 15-key BIOS-like buffer and runs the menus TAC asks for (text_window). No SDL.
//
// Names inside follow the original's (snake_case globals and functions), so the
// code can be read side by side with covert-decomp.
//
// Setup: TacSetup holds what main() reads from the env block (see Tac.h fields)
// with defaults for a practice break-in; the case-related library routines
// (GAMELIB: dossiers, clue reveals, master plans, organization presence) go
// through TacHooks, which by default do nothing (as in practice mode).
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/GameData.h"
#include "core/Image.h"
#include "formats/Font.h"
#include "formats/McgaPalette.h"
#include "game/Rng.h"
#include "game/TacBuilding.h"
#include "game/TacTask.h"

namespace redux {

// What TAC's main() takes from the env block (load_env, ENV[]); defaults: a
// practice break-in.
struct TacSetup {
    int env0 = 13;              // ENV[0]: 7 kidnap, 8 rescue, 10 bomb; others a break-in
    bool practice = true;       // options & 20h (forces tac_mode 0 and the building on alert)
    bool female = false;        // options & 2: spritesf.pic and equip1.pic (else sprites.pic, equip1m.pic)
    bool sound = true;          // options & 4
    int difficulty = 1;         // env +3Ah: the case's difficulty (orig_difficulty), 0-3
    int skillLevels = 0x0555;   // env +4Eh env_flags: bits 0-1 the skill, 8-9 the game speed
    int alertLevel = 0;         // env +48h (break-ins and practice); for the others the game's value
    int org = 4, city = 0;      // env +22h / +20h: the building's organization and city
    unsigned gameSeed = 0;      // env +3Ch rand_seed: part of the building's seed
    unsigned clockBase = 720;   // env +30h: minutes (the hour decides night)
    unsigned raidedCities = 0;  // env +4Ah: bit per city whose CIA agent is a double agent
    unsigned knownPlans = 0;    // env +4Ch: bit per organization whose plan is known
    int nClues = 0;             // env +3Eh
    std::uint32_t roomsSeen = 0;  // the site's rooms_seen (bit per room already explored)
    // The plot's people (persons[], 64 slots; only what TAC reads).
    struct Person {
        int org = 0, id = 0, city = 0;
        unsigned known = 0;
        int clues = 0, found = 0, points = 0, status = 0;
    };
    std::vector<Person> persons;    // n_persons
    int rescueTarget = -1;          // ENV[12] (rescue: the captured agent's person)
    std::array<TacAgent, 4> agents{};
    std::array<std::string, 16> cityNames{};   // for the terminal and the file search
    std::array<std::string, 16> orgNames{};
    // srand() seed for randomize() (the original uses the time); nullopt: the time.
    std::optional<std::uint32_t> runSeed;
};

// What main() writes back to the env block at the end.
struct TacResult {
    int env0 = 0;               // ENV[0]: 11h back to FINAL (practice), else 0 (GAME)
    int score = 0;              // ENV[8] result_score (-1: caught)
    int violence = 0;           // ENV[9]
    int missionFlags = 0;       // ENV[10] 1 evidence taken, 2 arrested, 4 double agent
    int wounds = 0;             // ENV[11]
    int env13 = 0;              // ENV[13] (mission_flags & 4) == 0
    int minutes = 0;            // ENV[15] frame_count / 60
    int gameSpeed = 1;          // back into env_flags bits 8-9
    std::uint32_t roomsSeen = 0;
    int photos = 0, bugs = 0;
    std::vector<TacSetup::Person> persons;   // persons[] as TAC left them (known/found bits)
};

class Tac;

// The case-related library routines (GAMELIB) TAC calls. The defaults do what a
// practice break-in needs (nothing).
struct TacHooks {
    virtual ~TacHooks() = default;
    virtual void suspectDossier(Tac&, int /*person*/) {}
    virtual std::string personName(Tac&, int /*person*/) { return "the suspect"; }
    virtual void describeNewInfo(Tac&, int /*org*/, int /*city*/, int /*amount*/, int /*worth*/) {}
    virtual void showMasterPlan(Tac&, int /*org*/) {}
    virtual int orgAlertLevel(Tac&, int /*city*/, int /*org*/) { return 0; }
    virtual std::string buildingType(Tac&, int /*city*/, int /*org*/) { return " building"; }
    virtual int setLocationKnown(Tac&, int /*org*/, int /*city*/, int /*flags*/) { return -1; }
    virtual int clueCount(Tac&) { return 0; }
    virtual std::string clueText(Tac&, int /*clue*/) { return {}; }
    virtual bool revealRandomClue(Tac&, int /*clue*/, int /*org*/, int /*city*/) { return false; }
};

// The pages and the graphics driver calls TAC makes (MPSLIB GFX.C, UI.C and the
// MCGA driver).
struct TacGfx {
    std::array<Image, 4> page{Image(320, 200), Image(320, 200), Image(320, 200), Image(320, 200)};
    // g_gfx, the screen port; draw_room points it at page 1 for a while
    int gpage = 0;
    int textbg = 1, textcolor = 15, fillcolor = 4;
    const Font* font = nullptr;

    // drv_copy_rect: unclipped, row-linear like the driver (x past 319 runs into the next row)
    void copy(int src, int sx, int sy, int w, int h, int dst, int dx, int dy);
    void fillRect(int pg, int x, int y, int w, int h, int c);     // gfx_fill_rect
    void fillXY(int x1, int y1, int x2, int y2, int c) { fillRect(gpage, x1, y1, x2 - x1 + 1, y2 - y1 + 1, c); }
    void line(int x1, int y1, int x2, int y2, int c);             // draw_line
    void rectOutline(int x, int y, int w, int h, int c);          // draw_rect_outline
    void recolor(int pg, int x, int y, int w, int h, int from, int to);
    void text(const std::string& s, int x, int y, int c);         // draw_text
    void textTransparent(const std::string& s, int x, int y, int c);
    void textCentered(const std::string& s, int x, int y, int c); // draw_text_centered
    void image(int pg, int x, int y, const Image* img);           // draw_image_noclip (0 transparent)
    Image grab(int pg, int x, int y, int w, int h) const;         // drv_get_image
};

class Tac {
public:
    Tac(const GameData& data, const FontSet& fonts, TacSetup setup, std::shared_ptr<TacHooks> hooks = {});
    ~Tac();

    // What the program is waiting for.
    enum class Wait { None, Retrace, Key, Menu, Done };

    void start();               // runs main() up to its first wait
    void tick();                // one retrace
    Wait waiting() const { return wait_; }
    bool finished() const { return wait_ == Wait::Done; }
    bool quitRequested() const { return quit_; }   // Alt-Q, Yes: the game ends

    // The keyboard (get_key codes: ASCII, or scan code << 8).
    void pushKey(int code);
    // NumLock as TAC forces it (KBD_FLAGS 20h): keypad digits walk, Shift+keypad dives.
    bool numLock() const { return numlock_; }
    bool keyBufferEmpty() const { return keys_.empty(); }

    // A text_window TAC is waiting on: draw it over the screen (fill colour
    // `fill` for odd y), then call menuDone with the item (-1 none/Esc) once
    // the window has been drawn into screen() the way text_window leaves it.
    struct MenuRequest {
        std::string text;
        int x = 0, y = 0;
        int fill = 1;
    };
    const MenuRequest& menuRequest() const { return menu_; }
    void menuDone(int result);

    Image& screen() { return gfx_.page[0]; }
    const Image& screen() const { return gfx_.page[0]; }
    Palette palette() const { return mcgaPalette(palette_); }
    TacResult result() const { return result_; }
    const TacMap& map() const { return map_; }
    const TacSetup& setup() const { return setup_; }
    TacGfx& gfx() { return gfx_; }
    // the sound effects asked for since the last call (play_sound numbers; no driver yet)
    std::vector<int> takeSounds() { return std::exchange(sounds_, {}); }

    // mutable state the hooks may need
    std::string msg_buf;
    std::vector<TacSetup::Person> persons;

private:
    // ---- waits ----
    struct RetraceAwait {
        Tac* t;
        int n;
        bool await_ready() const noexcept { return n <= 0; }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            t->wait_ = Wait::Retrace;
            t->count_ = n;
            t->resume_ = h;
        }
        void await_resume() const noexcept {}
    };
    struct KeyAwait {
        Tac* t;
        bool await_ready() const noexcept { return !t->keys_.empty(); }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            t->wait_ = Wait::Key;
            t->resume_ = h;
        }
        int await_resume() { return t->popKey(); }
    };
    struct MenuAwait {
        Tac* t;
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            t->wait_ = Wait::Menu;
            t->resume_ = h;
        }
        int await_resume() const noexcept { return t->menuResult_; }
    };
    RetraceAwait wait_retrace(int n) { return {this, n}; }
    KeyAwait get_key_wait() { return {this}; }
    int popKey();
    bool kbhit() const { return !keys_.empty(); }   // (drv_kbhit's sense inverted: true = a key waits)
    int get_key_now();                              // get_key when a key waits
    void flush_input() { keys_.clear(); }
    Task<int> text_window(std::string s, int x, int y);
    Task<int> show_menu(std::string s, int x, int y);
    Task<int> wait_key_or_button();

    // ---- TAC.C ----
    Task<void> tac_main();
    void play_sound(int n);
    void play_tune(int n);
    // ---- BREAKIN.C ----
    Task<void> setup_break_in();
    Task<void> update_current_room();
    Task<void> tac_frame();
    Task<void> draw_play_view();
    Task<void> fire_gun(int who, int flag);
    Task<void> grenade_menu(int x, int y, int dir, int fuse, int type);
    void update_projectiles(int active);
    void put_pixel(int x, int y, int color);
    void mark_guard_down(int g);
    // ---- ROOM.C / ROOMVIEW.C ----
    void draw_room(int x, int y);
    void draw_room_objects(int room);
    void reveal_rooms(int x, int y);
    static int approx_distance(int dx, int dy);
    void erase_actor_sprite(int n, int x, int y);
    void load_tac_sprites();
    // ---- ACTION.C ----
    Task<void> examine_object();
    void object_in_front();
    Task<void> take_photo();
    void plant_bug();
    Task<void> use_object();
    void draw_disguise_status();
    void show_status_line(const std::string& s);
    Task<void> mission_end_text();
    Task<void> search_files();
    void choose_password();
    void reveal_password_letter();
    Task<void> computer_terminal();
    void draw_terminal_frame(const std::string& title);
    Task<void> terminal_search();
    Task<int> terminal_input(int x, int y, int n);
    void draw_input_field(int x, int y, int n);
    void draw_intercept_popup();
    void present_back_buffer();
    void save_to_back_buffer();
    std::string format_clock_hms(int frames) const;
    // ---- GUARD.C ----
    Task<void> guard_think(int n);
    int guard_choose_goal(int room, int guard);
    int guard_next_room(int room, int mode);
    void compute_room_distances(int room, int noise);
    int find_door(int a, int b, int* px, int* py);
    int guard_plan_path(int g, int room, int tx, int ty, int sx, int sy);
    void spawn_guard(int n);
    int guard_sees_max(int n);
    int guard_can_see_max(int n);
    int line_of_sight(int x1, int y1, int x2, int y2);
    int guard_reserve_check(int n);
    static int direction8(int dx, int dy);
    static int direction8_coarse(int dx, int dy);
    Task<void> equipment_screen(int interactive);
    void draw_film_count();
    void draw_equipment_panel();

    // ---- helpers ----
    int random(int n) { return rng_.below(n); }
    void randomize();
    TacRoom& room(int i);
    std::int8_t& building(int x, int y) { return map_.b(x, y); }
    int room_map(int x, int y) { return map_.room(x, y); }
    const TacObjType& obj(int t) const { return map_.obj[static_cast<std::size_t>(t & 0x3F)]; }
    unsigned objFlags(int t) const { return obj(t).flags; }
    std::string person_name(int p);

    const GameData& data_;
    const FontSet& fonts_;
    TacSetup setup_;
    std::shared_ptr<TacHooks> hooks_;
    TacGfx gfx_;
    TacMap map_;
    Rng rng_;
    PaletteMap palette_{};
    std::vector<int> sounds_;

    Task<void> main_;
    std::coroutine_handle<> resume_;
    Wait wait_ = Wait::None;
    int count_ = 0;
    MenuRequest menu_;
    int menuResult_ = -1;
    std::deque<int> keys_;
    bool numlock_ = false;
    bool quit_ = false;
    TacResult result_;

public:
    // ---- the original's globals (TAC.H) ----
    struct Guard {
        unsigned flags = 0;
        int x = 0, y = 0, tx = 0, ty = 0, anim = 0, dir = 0, mode = 0, firing = 0, room = 0, wait = 0,
            spotted = 0, heard = 0, stun = 0;
    };
    struct Projectile {
        int x = 0, y = 0, dir = 0, timer = 0, type = 0;
    };

private:
    unsigned options = 0;
    int tac_mode = 0, env_flags = 0, game_speed = 1, difficulty = 0, orig_difficulty = 0, alert_level = 0;
    int target_person = -1, n_clues = 0;
    unsigned known_plans = 0, raided_cities = 0, clock_base = 0;
    int cur_org = 0, cur_city = 0, hour = 0, is_night = 0, real_mission = 1;
    int on_alert = 0, mission_over = 0, mission_flags = 0, result_score = 0, violence = 0, guards_left = 0;
    int wounds = 0, frame_count = 0, message_shown = 0, photos_taken = 0, photo_score = 0, bugs_planted = 0,
        bug_score = 0;
    std::string password, password_shown;
    int sfx_tempo = 0, sfx_variant = 0, tune_on = 0, mono_display = 0;
    std::array<Image, 160 + 6 + 1 + 8> sprite_imgs;   // sprite_imgs[160], icon_imgs[6], cursor_img, icon2_imgs[8]
    Image camera_img, camera_img2;
    std::array<Image, 15> equip_images;
    int alarm_color = 14;
    int flash_active = 0, flash_who = 0, flash_x = 0, flash_y = 0;
    int start_room = 0, post_room = 0, cur_room = 0, goal_room = 0;
    std::array<int, 24> room_noise{}, room_dist{};
    int room_alert = 0, doors_open = 0;
    int max_x = 0, max_y = 0, cell_x = 0, cell_y = 0, front_x = 0, front_y = 0;
    int max_dir = 4, want_dir = 4, max_moved = 0;
    unsigned equipment = 0;
    int ammo = 0, clips = 0, bugs_left = 0;
    std::array<int, 3> grenades{};
    int cur_grenade = 0, player_mode = 0, player_moving = 0, player_anim = 0, sneaking = 0, sneak_steps = 0;
    int disguised = 0, fire_timer = 0, dive_count = 0, throw_delay = 0, made_noise = 0, last_key = 0,
        key_repeat = 0;
    std::array<Guard, 9> guards{};
    std::vector<std::int8_t> guard_paths = std::vector<std::int8_t>(9 * 40 * 32);
    std::int8_t& gpath(int g, int x, int y);
    int last_at_goal = 0, guards_at_goal = 0, guard_in_room = 0, guards_near = 0, guards_active = 0;
    int max_heard = 0, guard_saw_max = 0, see_bits = 0, last_seen_x = 0, last_seen_y = 0;
    int alarm_timer = 0, alarm_heard = 0, last_disguised = 0, last_dark = 0, last_grenade = 0;
    std::array<Projectile, 16> projectiles{};
    std::array<std::uint8_t, 256> queue_x{}, queue_y{};
    int view_x0 = 0, view_y0 = 0, room_left = 0, room_top = 0, room_right = 0, room_bottom = 0;
    std::array<int, 9> actor_x{}, actor_y{}, actor_map_x{}, actor_map_y{};
    int popup_x = 0, popup_y = 0, popup_face = 0, cursor_pos = 0, cursor_w = 0, cursor_x = 0;
    struct Pos {
        int x, y;
    };
    std::array<Pos, 13> panel_icons{{{170, 48}, {166, 46}, {216, 73}, {229, 7}, {252, 70}, {247, 0}, {247, 31},
                                     {266, 0}, {172, 3}, {172, 17}, {172, 31}, {172, 71}, {172, 80}}};
};

}  // namespace redux
