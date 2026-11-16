// TAC.EXE: the graphics calls, the waits, main() (TAC.C) and BREAKIN.C
// (setup_break_in .. mark_guard_down). See Tac.h.
#include "game/Tac.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>

#include "formats/Pic.h"

namespace redux {

namespace {

constexpr int step_dx[9] = {0, 0, 2, 3, 2, 0, -2, -3, -2};
constexpr int step_dy[9] = {0, -3, -2, 0, 2, 3, 2, 0, -2};
constexpr int dir_dx[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
constexpr int dir_dy[9] = {0, -1, -1, 0, 1, 1, 1, 0, -1};
constexpr const int* door_dx = kDoorDx;
constexpr const int* door_dy = kDoorDy;
constexpr int hit_colors[5] = {0, 8, 3, 7, 15};

// tune_data: the palettes play_tune sets (drv_call_gate is set_palette)
constexpr std::uint8_t tune_data[5][17] = {
    {0, 1, 2, 3, 4, 0, 6, 7, 8, 9, 10, 11, 12, 0, 14, 15, 0},
    {0, 1, 2, 15, 4, 0, 6, 7, 8, 9, 10, 11, 12, 0, 14, 15, 0},
    {0, 1, 2, 3, 4, 0, 6, 12, 8, 9, 10, 11, 12, 0, 14, 15, 0},
    {0, 1, 2, 14, 4, 0, 6, 7, 8, 9, 10, 11, 12, 0, 14, 15, 0},
    {0, 1, 2, 0, 4, 0, 6, 7, 8, 9, 10, 11, 12, 0, 14, 15, 0},
};

int clamp(int v, int lo, int hi) { return clampTo(v, lo, hi); }

}  // namespace

// ---------------------------------------------------------------- TacGfx

void TacGfx::copy(int src, int sx, int sy, int w, int h, int dst, int dx, int dy) {
    if (w <= 0 || h <= 0) return;
    const auto& s = page[static_cast<std::size_t>(src)].pixels();
    auto& d = page[static_cast<std::size_t>(dst)].pixels();
    const bool same = src == dst;
    std::vector<std::uint8_t> tmp;
    if (same) tmp = s;
    const auto& from = same ? tmp : s;
    long so = static_cast<long>(sy) * 320 + sx, dof = static_cast<long>(dy) * 320 + dx;
    for (int r = 0; r < h; ++r, so += 320, dof += 320)
        for (int c = 0; c < w; ++c) {
            const long a = so + c, b = dof + c;
            if (a < 0 || a >= 64000 || b < 0 || b >= 64000) continue;
            d[static_cast<std::size_t>(b)] = from[static_cast<std::size_t>(a)];
        }
}

void TacGfx::fillRect(int pg, int x, int y, int w, int h, int c) {
    Image& img = page[static_cast<std::size_t>(pg)];
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) img.set(i, j, static_cast<std::uint8_t>(c));
}

void TacGfx::line(int x1, int y1, int x2, int y2, int c) {
    Image& img = page[static_cast<std::size_t>(gpage)];
    int dx = std::abs(x2 - x1), dy = -std::abs(y2 - y1);
    const int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        img.set(x1, y1, static_cast<std::uint8_t>(c));
        if (x1 == x2 && y1 == y2) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void TacGfx::rectOutline(int x, int y, int w, int h, int c) {
    line(x, y, x + w, y, c);
    line(x, y + h, x + w, y + h, c);
    line(x + w, y, x + w, y + h, c);
    line(x, y, x, y + h, c);
}

void TacGfx::recolor(int pg, int x, int y, int w, int h, int from, int to) {
    Image& img = page[static_cast<std::size_t>(pg)];
    for (int j = std::max(y, 0); j < std::min(y + h, 200); ++j)
        for (int i = std::max(x, 0); i < std::min(x + w, 320); ++i)
            if (img.at(i, j) == from) img.set(i, j, static_cast<std::uint8_t>(to));
}

void TacGfx::text(const std::string& s, int x, int y, int c) {
    textcolor = c;
    TextAttr a;
    a.textbg = textbg;
    a.color = static_cast<std::uint8_t>(c);
    a.background = static_cast<std::uint8_t>(fillcolor);
    font->draw(page[static_cast<std::size_t>(gpage)], x, y, s, a);
}

void TacGfx::textTransparent(const std::string& s, int x, int y, int c) {
    textbg = 0;
    text(s, x, y, c);
    textbg = 1;
}

void TacGfx::textCentered(const std::string& s, int x, int y, int c) {
    textTransparent(s, x - font->textWidth(s) / 2, y, c);
}

void TacGfx::image(int pg, int x, int y, const Image* img) {
    if (!img || img->empty()) return;
    page[static_cast<std::size_t>(pg)].blit(*img, 0, 0, img->width(), img->height(), x, y, 0);
}

Image TacGfx::grab(int pg, int x, int y, int w, int h) const {
    Image out(w, h);
    out.blit(page[static_cast<std::size_t>(pg)], x, y, w, h, 0, 0);
    return out;
}

// ---------------------------------------------------------------- driving

Tac::Tac(const GameData& data, const FontSet& fonts, TacSetup setup, std::shared_ptr<TacHooks> hooks)
    : data_(data), fonts_(fonts), setup_(std::move(setup)),
      hooks_(hooks ? std::move(hooks) : std::make_shared<TacHooks>()) {
    gfx_.font = &fonts_.font(1);
    persons = setup_.persons;
    for (std::size_t i = 0; i < 17; ++i) palette_[i] = tune_data[0][i];
}

Tac::~Tac() = default;

void Tac::start() {
    main_ = tac_main();
    main_.start();
    if (main_.done()) {
        wait_ = Wait::Done;
        main_.rethrow();
    }
}

void Tac::tick() {
    switch (wait_) {
    case Wait::Retrace:
        if (--count_ > 0) return;
        break;
    case Wait::Key:
        if (keys_.empty()) return;
        break;
    default:
        return;
    }
    wait_ = Wait::None;
    resume_.resume();
    if (main_.done()) {
        wait_ = Wait::Done;
        main_.rethrow();
    }
}

void Tac::pushKey(int code) {
    if (keys_.size() < 15) keys_.push_back(code);
}

int Tac::popKey() {
    if (keys_.empty()) return 0;
    const int k = keys_.front();
    keys_.pop_front();
    return k;
}

int Tac::get_key_now() { return popKey(); }

void Tac::menuDone(int result) {
    if (wait_ != Wait::Menu) return;
    menuResult_ = result;
    wait_ = Wait::None;
    resume_.resume();
    if (main_.done()) {
        wait_ = Wait::Done;
        main_.rethrow();
    }
}

Task<int> Tac::text_window(std::string s, int x, int y) {
    if (!(y & 1)) gfx_.fillcolor = 5;      // draw_text_window: the bevel box
    menu_ = {std::move(s), x, y, gfx_.fillcolor};
    flush_input();
    co_return co_await MenuAwait{this};
}

Task<int> Tac::show_menu(std::string s, int x, int y) {
    const int r = co_await text_window(std::move(s), x, y);
    gfx_.fillcolor = 1;
    co_return r;
}

Task<int> Tac::wait_key_or_button() {
    const int key = co_await get_key_wait();
    flush_input();
    co_return key;
}

void Tac::randomize() {
    const std::uint32_t s = setup_.runSeed ? *setup_.runSeed : static_cast<std::uint32_t>(std::time(nullptr));
    rng_.seed(s & 0x7FFF);
}

TacRoom& Tac::room(int i) {
    static TacRoom scratch;
    if (i < 0 || i >= static_cast<int>(map_.rooms.size())) {
        scratch = TacRoom{};
        return scratch;
    }
    return map_.rooms[static_cast<std::size_t>(i)];
}

std::int8_t& Tac::gpath(int g, int x, int y) {
    const int i = (g * 40 + x) * 32 + y;
    static std::int8_t scratch;
    if (i < 0 || i >= static_cast<int>(guard_paths.size())) {
        scratch = 0;
        return scratch;
    }
    return guard_paths[static_cast<std::size_t>(i)];
}

std::string Tac::person_name(int p) { return hooks_->personName(*this, p); }

// ---------------------------------------------------------------- TAC.C

void Tac::play_sound(int n) {
    if (options & 4) sounds_.push_back(n);
}

void Tac::play_tune(int n) {
    for (std::size_t i = 0; i < 17; ++i) palette_[i] = tune_data[n][i];
}

Task<void> Tac::tac_main() {
    options = (setup_.practice ? 0x20u : 0u) | (setup_.female ? 2u : 0u) | (setup_.sound ? 4u : 0u);
    if (setup_.env0 == 7) tac_mode = 1;
    if (setup_.env0 == 8) tac_mode = 2;
    if (setup_.env0 == 10) tac_mode = 3;
    if (options & 0x20) tac_mode = 0;
    // load_env
    env_flags = setup_.skillLevels;
    difficulty = setup_.difficulty;
    cur_org = setup_.org;
    cur_city = setup_.city;
    clock_base = setup_.clockBase;
    raided_cities = setup_.raidedCities;
    known_plans = setup_.knownPlans;
    n_clues = setup_.nClues;
    const int n_persons = static_cast<int>(persons.size());

    game_speed = (env_flags >> 8) & 3;
    orig_difficulty = difficulty;
    difficulty = difficulty - (env_flags & 3) + 1;
    if (tac_mode == 2) difficulty++;
    if (tac_mode == 0) alert_level = setup_.alertLevel;
    if (orig_difficulty)
        difficulty += clamp(alert_level * orig_difficulty / 50, 0, 1);
    else
        difficulty = 0;
    difficulty = clamp(difficulty, 0, 4);
    target_person = -1;
    if (tac_mode == 0)
        for (int i = 0; i < n_persons; i++)
            if (persons[static_cast<std::size_t>(i)].org == cur_org && persons[static_cast<std::size_t>(i)].city == cur_city)
                target_person = i;
    // (persons[-1] is the work page's RastPort: its status is positive)
    if (target_person >= 0 && persons[static_cast<std::size_t>(target_person)].status < 0 &&
        persons[static_cast<std::size_t>(target_person)].status != -3)
        target_person = -1;
    if (tac_mode == 2) target_person = setup_.rescueTarget;
    hour = static_cast<int>(clock_base / 60 % 24);
    is_night = hour < 8 || hour >= 20;
    real_mission = 1;
    guards_left = difficulty * 4 + 8;
    if (tac_mode == 2) guards_left = orig_difficulty * 2 + 4;
    if (tac_mode == 3) guards_left = orig_difficulty * 2 + 2;
    if (tac_mode == 1) guards_left = difficulty * 3 + 3;
    rng_.seed(tacBuildingSeed(cur_city, cur_org, setup_.gameSeed));
    gfx_.gpage = 0;
    gfx_.fillcolor = 15;
    play_tune(0);
    auto load = [&](const char* name, int pg) {
        const Picture pic = decodePicture(data_.read(name));
        gfx_.page[static_cast<std::size_t>(pg)].blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
    };
    load("camera.pic", 2);
    camera_img = gfx_.grab(2, 0, 0, 196, 112);
    camera_img2 = gfx_.grab(2, 200, 0, 72, 61);
    load_tac_sprites();
    gfx_.copy(2, 0, 160, 320, 40, 1, 0, 160);
    load(tac_mode != 3 ? "guys3.pic" : "guys2.pic", 2);
    gfx_.copy(1, 0, 160, 320, 40, 2, 0, 160);

    TacBuildingParams bp;
    bp.mode = tac_mode;
    bp.targetPerson = target_person;
    bp.targetId = target_person >= 0 && target_person < n_persons ? persons[static_cast<std::size_t>(target_person)].id : 319;
    bp.agents = setup_.agents;
    bp.guardsLeft = guards_left;
    bp.org = cur_org;
    generateBuilding(map_, rng_, bp);
    start_room = map_.startRoom;
    post_room = map_.postRoom;
    cur_room = map_.curRoom;
    if (map_.onAlert) on_alert = 1;
    randomize();    // (generate_building's last call)
    randomize();
    choose_password();
    if ((options & 0x20) || !target_person || (raided_cities & (1u << cur_city))) on_alert = 1;
    if (!target_person && difficulty < 4) difficulty++;
    if (on_alert) guards_left += difficulty * 2;
    for (int i = 0; i <= map_.nRooms; i++)
        if ((std::uint32_t{1} << i) & setup_.roomsSeen) room(i).flags |= 2;
    load((options & 2) ? "equip1.pic" : "equip1m.pic", 1);
    if (tac_mode == 0) gfx_.copy(1, 0, 0, 320, 200, 0, 0, 0);
    flush_input();
    co_await setup_break_in();
    numlock_ = true;
    do {
        co_await update_current_room();
        co_await wait_retrace(game_speed ? (game_speed == 1 ? 4 : 2) : 6);
        update_projectiles(0);
        co_await wait_retrace(game_speed ? (game_speed == 1 ? 4 : 2) : 6);
    } while (!mission_over);
    numlock_ = false;
    co_await mission_end_text();
    result_.env0 = (options & 0x20) ? 0x11 : 0;
    result_.score = result_score;
    result_.violence = violence ? clamp(violence * 10 + 20, 0, guards_left * 10) : 0;
    result_.missionFlags = mission_flags;
    result_.wounds = wounds;
    result_.env13 = (mission_flags & 4) == 0;
    result_.minutes = frame_count / 60;
    result_.gameSpeed = game_speed;
    result_.roomsSeen = setup_.roomsSeen;
    for (int i = 0; i <= map_.nRooms; i++)
        if (room(i).flags & 2) result_.roomsSeen |= std::uint32_t{1} << i;
    result_.photos = photos_taken;
    result_.bugs = bugs_planted;
    result_.persons = persons;
    play_sound(0);
}

// ---------------------------------------------------------------- BREAKIN.C

Task<void> Tac::setup_break_in() {
    int i, py = 0, num, px = 0;
    int exits[8] = {};

    if (tac_mode != 1 && tac_mode != 2 && tac_mode != 3)
        co_await equipment_screen(1);
    else {
        equipment = 0xc1;
        if (tac_mode == 2 || tac_mode == 3) {
            grenades[0] = 4;
            if (tac_mode == 2)
                equipment = 0xc2;
            else
                equipment &= ~0x80u;
        }
        co_await equipment_screen(0);
        draw_equipment_panel();
    }
    room(0).flags |= 2;
    draw_room(180, 100);
    gfx_.fillcolor = 1;
    max_x = 328;
    max_y = 264;
    auto& doors = map_.doors;
    auto D = [&](int k) -> TacDoor& { return doors[static_cast<std::size_t>(k)]; };
    if (tac_mode == 1 || tac_mode == 2 || tac_mode == 3) {
        cur_room = start_room;
        px = room(start_room).x + room(start_room).w / 2;
        py = room(start_room).y + room(start_room).h / 2;
        if (tac_mode == 3) {
            py = room(start_room).y + room(start_room).h - 1;
            goal_room = start_room;
        }
        if (tac_mode == 1) start_room = -1;
        max_dir = 1;
        building(px, py - (tac_mode == 3 ? 1 : 0)) = 14;
        if (tac_mode == 2)
            for (i = 0; i < map_.nDoors; i++) {
                if (D(i).roomA == start_room) room(D(i).roomB).flags |= 2;
                if (D(i).roomB == start_room) room(D(i).roomA).flags |= 2;
            }
    } else {
        cur_room = -1;
        num = 0;
        for (i = 0; i < map_.nDoors; i++) {
            if ((!D(i).roomA && D(i).roomB) || (!D(i).roomB && D(i).roomA)) {
                if (num < 8) exits[num] = i;
                num++;
                gfx_.textTransparent(std::to_string(num), D(i).x * 3 + 180, D(i).y * 3 + 98, mono_display ? 4 : 15);
            }
        }
        if (num > 1) {
            msg_buf = "Which door?  \n ";
            for (i = 0; i < num; i++) msg_buf += "Door #" + std::to_string(i + 1) + "\n ";
            i = co_await show_menu(msg_buf, 180, 64);
            if (i != -1)
                i = exits[std::min(i, 7)];
            else
                i = exits[0];
        } else
            i = exits[0];
        if (!D(i).roomA && D(i).roomB) {
            cur_room = D(i).roomB;
            px = D(i).x + door_dx[D(i).dir];
            py = D(i).y + door_dy[D(i).dir];
            max_dir = D(i).dir * 2 + 1;
        }
        if (!D(i).roomB && D(i).roomA) {
            cur_room = D(i).roomA;
            px = D(i).x;
            py = D(i).y;
            max_dir = ((D(i).dir + 2) & 3) * 2 + 1;
        }
        building(px, py) |= static_cast<std::int8_t>(0x80);
        if (target_person != -1) compute_room_distances(start_room, 0);
    }
    if (tac_mode == 2) compute_room_distances(start_room, 999);
    want_dir = max_dir;
    for (i = 0; i < 16; i++) projectiles[static_cast<std::size_t>(i)].type = -1;
    for (i = 0; i <= 8; i++) {
        actor_x[static_cast<std::size_t>(i)] = actor_y[static_cast<std::size_t>(i)] = 0;
        actor_map_x[static_cast<std::size_t>(i)] = actor_map_y[static_cast<std::size_t>(i)] = 0;
        guards[static_cast<std::size_t>(i)].flags = 0;
    }
    spawn_guard(0);
    guards[0].flags |= 0x80;
    for (i = 1; i <= difficulty + 1; i++)
        if (tac_mode != 2) spawn_guard(i);
    for (i = 0; i < 24; i++) room_noise[static_cast<std::size_t>(i)] = 0;
    max_x = px * 16 + 8;
    max_y = py * 16 + 8;
    room_alert = 0;
    cur_room = -1;
    if (tac_mode == 2 || tac_mode == 3) {
        last_seen_x = max_x / 16;
        last_seen_y = max_y / 16;
        if (tac_mode == 3) {
            compute_room_distances(1, 240);
            building(last_seen_x, last_seen_y) = 2;
        }
    }
}

Task<void> Tac::update_current_room() {
    int i;

    cell_x = max_x / 16;
    cell_y = max_y / 16;
    if (room_map(cell_x, cell_y) != cur_room) {
        if (cur_room != -1) {
            room(cur_room).flags &= static_cast<std::uint16_t>(~8u);
            for (i = 1; i < 8; i++)
                if (guards[static_cast<std::size_t>(i)].flags && guards[static_cast<std::size_t>(i)].stun > 999) {
                    guards[static_cast<std::size_t>(i)].flags = 0;
                    room(cur_room).flags |= 8;
                }
            if (doors_open > 0 || room_alert) room(cur_room).flags |= 8;
        }
        doors_open = 0;
        cur_room = room_map(cell_x, cell_y);
        max_moved = 0;
        max_x = cell_x * 16 - step_dx[max_dir] + 8;
        max_y = cell_y * 16 - step_dy[max_dir] + 8;
        if (cur_room == 0) {
            mission_over = 1;
            gfx_.copy(1, actor_x[0], actor_y[0], 15, 15, gfx_.gpage, actor_x[0], actor_y[0]);
            co_return;
        }
        draw_room_objects(cur_room);
        room(cur_room).flags |= 2;
        draw_room(180, 100);
        if (cur_room == start_room && target_person != -1 && !disguised) compute_room_distances(cur_room, 240);
        room_alert = room(cur_room).flags & 8;
    }
    co_await tac_frame();
    if (mission_over || quit_) co_return;
    for (i = 1; i <= 8; i++) co_await guard_think(i);
    co_await draw_play_view();
    sfx_tempo = 1;
    if (!static_cast<std::int8_t>(frame_count)) sfx_variant = random(3) + 1;
    if (alarm_timer <= 0) {
        if (last_at_goal) {
            if (disguised)
                sfx_tempo = 3;
            else
                sfx_tempo = 2;
        }
    } else {
        if (last_at_goal)
            sfx_tempo = 5;
        else
            sfx_tempo = 4;
    }
    play_sound(44);
    if (++frame_count > 7500) frame_count -= 256;
    draw_disguise_status();
    if (tune_on) {
        play_tune(0);
        tune_on = 0;
    }
    if (mission_flags & 2) disguised = 0;
    if (alarm_timer) {
        disguised = 0;
        if (--alarm_timer == 0) compute_room_distances(start_room, 0);
        if (!(frame_count & 15) && tac_mode != 3) {
            play_tune(2);
            tune_on = 1;
            if (alarm_timer > 200) {
                play_sound(18);
                alarm_heard = 1;
            }
        }
    }
    if (!(frame_count & 31)) {
        numlock_ = true;
        i = random(24);
        if (room(i).flags & 0x10) {
            room(i).flags &= static_cast<std::uint16_t>(~0x10u);
            draw_room(180, 100);
        }
    }
    if (last_disguised != disguised && !mono_display) {
        last_disguised = disguised;
        if (disguised)
            gfx_.recolor(2, 0, 160, 320, 40, 13, alarm_color);
        else
            gfx_.recolor(2, 0, 160, 320, 40, alarm_color, 13);
    }
    if ((static_cast<std::uint8_t>(room(cur_room).flags) & 0x10) != last_dark) {
        last_dark = room(cur_room).flags & 0x10;
        if (last_dark)
            gfx_.recolor(2, 0, 160, 320, 40, 9, 12);
        else
            gfx_.recolor(2, 0, 160, 320, 40, 12, 9);
    }
}

Task<void> Tac::tac_frame() {
    int doorway, mx, my, i, ahead_x, obj, code, ahead_y;
    auto G = [&](int k) -> Guard& { return guards[static_cast<std::size_t>(k)]; };

    if (((equipment & 0x40) ? 3 : 1) < wounds) mission_over = 1;
    if ((room(cur_room).flags & 0x10) && !(equipment & 0x20)) guards[0].stun += 2;
    if (guards[0].stun && --guards[0].stun > 0x40) {
        guards[0].spotted = 0;
        guards[0].flags |= 4;
        sneaking = 1;
        flush_input();
        if (guards[0].stun > 0x100) mission_over = 1;
        co_return;
    }
    made_noise = 0;
    if (ammo == 0 && clips) {
        ammo = 6;
        clips--;
        draw_equipment_panel();
    }
    // (the joystick isn't supported: joykey stays -1)
    if (kbhit() && !dive_count && !throw_delay) {
        code = get_key_now();
        if (code != last_key)
            key_repeat = 4;
        else
            key_repeat = 0;
        switch (last_key = code) {
        case 'f': game_speed = 2; break;
        case 'd': game_speed = 1; break;
        case 's': game_speed = 0; break;
        case ' ':
            if (cur_room == start_room && target_person != -1 && !(mission_flags & 2)) made_noise = 1;
            if (fire_timer < 4) fire_timer += 4;
            if (player_mode != 3) player_mode = 0;
            break;
        case '5':
            player_mode = 0;
            sneaking ^= 1;
            guards[0].flags &= ~4u;
            guards[0].flags |= static_cast<unsigned>(sneaking << 2);
            break;
        case '\r':
        case 0x3B00:
            if (tac_mode != 3) {
                co_await examine_object();
                player_mode = 2;
                sneaking = 0;
            }
            break;
        case 0x3C00:
            made_noise = 1;
            if (equipment & 4) co_await take_photo();
            break;
        case 0x3D00:
            made_noise = 1;
            plant_bug();
            break;
        case 0x3E00:
            made_noise = 1;
            co_await use_object();
            break;
        case 0x3F00:
        case 0x4000:
        case 0x4100:
            made_noise = 1;
            if (grenades[static_cast<std::size_t>(cur_grenade)]-- > 0) {
                co_await grenade_menu(max_x, max_y, max_dir, code == 0x3F00 ? 1 : code == 0x4000 ? 2 : 3, cur_grenade);
                throw_delay = 3;
            }
            break;
        case 0x4200:
            for (i = 0; i < 16; i++)
                if (projectiles[static_cast<std::size_t>(i)].timer == -9999) projectiles[static_cast<std::size_t>(i)].timer = -63;
            break;
        case 0x4300:
            made_noise = 1;
            if (grenades[static_cast<std::size_t>(cur_grenade)] > 0)
                co_await grenade_menu(max_x, max_y, max_dir, -1, cur_grenade);
            break;
        case 0x4400:
            cur_grenade++;
            if (cur_grenade > 2) cur_grenade = 0;
            draw_equipment_panel();
            break;
        case 0x1F00:
            if (options & 4) play_sound(0);
            options ^= 4;
            break;
        case 0x1900:
            present_back_buffer();
            play_sound(0x25);
            message_shown = 1;
            co_await show_menu("PAUSED\n", 0x50, 0x60);
            co_await wait_key_or_button();
            play_sound(0x26);
            save_to_back_buffer();
            break;
        case 0x1000:
            present_back_buffer();
            if (co_await show_menu("Are you sure\nyou want to Quit?\n No\n Yes\n", 0x50, 0x50) == 1) {
                numlock_ = false;   // set_exit_state; exit()
                quit_ = true;
                co_return;
            }
            save_to_back_buffer();
            break;
        case 0:
            player_mode = 0;
            want_dir = max_dir;
            break;
        case 8: case '7': case 0x4737: want_dir = 8; player_mode = 1; break;
        case 1: case '8': case 0x4838: player_mode = 1; want_dir = 1; break;
        case 2: case '9': case 0x4939: want_dir = 2; player_mode = 1; break;
        case 7: case '4': case 0x4B34: want_dir = 7; player_mode = 1; break;
        case 3: case '6': case 0x4D36: want_dir = 3; player_mode = 1; break;
        case 6: case '1': case 0x4F31: want_dir = 6; player_mode = 1; break;
        case 5: case '2': case 0x5032: want_dir = 5; player_mode = 1; break;
        case 4: case '3': case 0x5133: want_dir = 4; player_mode = 1; break;
        case 0xC7: case 0x4700: want_dir = 8; player_mode = 4; break;
        case 0xC8: case 0x4800: player_mode = 4; want_dir = 1; break;
        case 0xC9: case 0x4900: want_dir = 2; player_mode = 4; break;
        case 0xCB: case 0x4B00: want_dir = 7; player_mode = 4; break;
        case 0xCD: case 0x4D00: want_dir = 3; player_mode = 4; break;
        case 0xCF: case 0x4F00: want_dir = 6; player_mode = 4; break;
        case 0xD0: case 0x5000: want_dir = 5; player_mode = 4; break;
        case 0xD1: case 0x5100: want_dir = 4; player_mode = 4; break;
        default: break;
        }
        if (tac_mode == 1 && (player_mode == 1 || player_mode == 4)) {
            compute_room_distances(cur_room, 0xF0);
            tac_mode = 0;
        }
        if (player_mode == 4) {
            made_noise = 1;
            dive_count = 0;
            for (i = 3; i <= 9; i++) {
                mx = (step_dx[want_dir] * i + max_x) / 16;
                my = (step_dy[want_dir] * i + max_y) / 16;
                if (room_map(mx, my) != cur_room) {
                    dive_count = i / 2 + 4;
                    break;
                }
                obj = building(mx, my);
                if (obj != -1) {
                    obj &= 0x3F;
                    if (objFlags(obj) & 0x20) break;
                    if (!(objFlags(obj) & 1)) {
                        dive_count = i / 2 + 6;
                        max_dir = want_dir;
                    }
                } else {
                    dive_count = i / 2 + 6;
                    max_dir = want_dir;
                }
            }
            if (dive_count == 0)
                player_mode = 0;
            else
                sneaking = 1;
        }
    } else
        last_key = -1;
    key_repeat--;
    flush_input();
    if (want_dir != max_dir) {
        if (((want_dir - max_dir) & 7) < 4)
            max_dir++;
        else
            max_dir--;
        max_dir = ((max_dir - 1) & 7) + 1;
    }
    if (player_mode == 1) {
        ahead_x = (step_dx[max_dir] * 2 + max_x) / 16;
        ahead_y = (step_dy[max_dir] * 2 + max_y) / 16;
        doorway = ((building(cell_x, cell_y) & 0x80) && (objFlags(building(cell_x, cell_y) & 0x3F) & 0x10)) ? 1 : 0;
        if (!doorway && ((max_x < room_left + 8 && step_dx[max_dir] < 0) || (max_x > room_right - 8 && step_dx[max_dir] > 0)))
            player_mode = 0;
        if (ahead_x != cell_x && (obj = building(ahead_x, cell_y) & 0x3F) != 0x3F && (objFlags(obj) & 1)) player_mode = 0;
        if (!doorway && ((max_y < room_top + 8 && step_dy[max_dir] < 0) || (max_y > room_bottom - 8 && step_dy[max_dir] > 0)))
            player_mode = 0;
        if (ahead_y != cell_y && (obj = building(cell_x, ahead_y) & 0x3F) != 0x3F && (objFlags(obj) & 1)) player_mode = 0;
        if (ahead_x != cell_x && ahead_y != cell_y && (obj = building(ahead_x, ahead_y) & 0x3F) != 0x3F &&
            (objFlags(obj) & 1))
            player_mode = 0;
    }
    player_moving = 0;
    switch (player_mode) {
    case 4:
        if (dive_count > 6) {
            max_x += step_dx[max_dir] * 2;
            max_y += step_dy[max_dir] * 2;
        }
        if (--dive_count == 0) player_mode = 0;
        break;
    case 1:
        player_moving = 1;
        if (want_dir == max_dir) {
            if (!sneaking || dive_count) {
                max_x += step_dx[max_dir];
                max_y += step_dy[max_dir];
            } else {
                max_x += dir_dx[max_dir];
                max_y += dir_dy[max_dir];
                sneak_steps++;
            }
            if (key_repeat <= 0 && dive_count == 0) player_mode = 0;
        }
        player_anim = (player_anim + 1) & 3;
        break;
    case 2:
        player_anim = 6;
        break;
    case 3:
        player_anim = 7;
        break;
    default:
        player_anim = 0;
        if (max_heard && tac_mode != 1 && disguised == 0) player_anim = 4;
        break;
    }
    if (sneaking) player_anim = 7;
    guards[0].x = max_x;
    guards[0].y = max_y;
    guards[0].dir = max_dir;
    guards[0].firing = fire_timer;
    guards[0].mode = player_mode;
    guards[0].anim = player_anim;
    guards[0].room = cur_room;
    guards[0].flags &= ~4u;
    guards[0].flags += static_cast<unsigned>(sneaking * 4);
    if (fire_timer) fire_timer--;
    if ((i = frame_count % 8 + 1) != 0 && (G(i).flags & 0x80)) {
        if (std::abs(max_x - G(i).x) + std::abs(max_y - G(i).y) < 10 && (G(i).stun > 0x18 || disguised == 0)) {
            if (disguised == 0 && G(i).stun == 0) wounds++;
            if (G(i).stun > 999 && --room_alert == 0) room(cur_room).flags &= static_cast<std::uint16_t>(~8u);
            G(i).flags = 0;
            G(i).stun = 0;
            disguised = 1;
            i = random(3);
            grenades[static_cast<std::size_t>(i)] = clamp(grenades[static_cast<std::size_t>(i)] + 2, 2, 8);
            draw_equipment_panel();
        }
    }
    if (frame_count > 64 && (tac_mode == 3 || tac_mode == 2) && (!guards_near || frame_count > 3600)) {
        cur_room = 0;
        mission_over = 1;
    }
}

Task<void> Tac::draw_play_view() {
    int k, img = 0, range, who, fall, len;
    auto G = [&](int n) -> Guard& { return guards[static_cast<std::size_t>(n)]; };
    auto AX = [&](int n) -> int& { return actor_x[static_cast<std::size_t>(n)]; };
    auto AY = [&](int n) -> int& { return actor_y[static_cast<std::size_t>(n)]; };
    const int gp = gfx_.gpage;

    who = 0;
    do {
        if (AX(who) != -1) gfx_.copy(1, AX(who), AY(who), 15, 15, gp, AX(who), AY(who));
    } while (++who <= 8);
    if (flash_active) gfx_.copy(1, flash_x, flash_y, 15, 15, gp, flash_x, flash_y);
    flash_active = 0;
    for (k = 9; k >= 0; k--) {
        if ((who = k) == 9) who = 0;
        if (G(who).room == cur_room && (G(who).flags & 0x80)) {
            AX(who) = G(who).x - room(cur_room).x * 16 + view_x0 - 8;
            AY(who) = G(who).y - room(cur_room).y * 16 + view_y0 - 8;
            if (k == 9) {
                if (G(who).firing == 0 && ((equipment & 0x40) ? 3 : 1) >= wounds && throw_delay == 0) {
                    img = G(who).anim;
                    if (G(who).spotted) img = 4;
                    if (G(who).flags & 4) img = 7;
                    erase_actor_sprite(((sneaking & sneak_steps & 1) ? 9 : 0) + G(who).dir * 20 + img - 20, AX(who), AY(who));
                }
                continue;
            } else {
                if (G(who).firing == 0) {
                    img = G(who).anim;
                    if (G(who).spotted) img = 4;
                    if (G(who).flags & 4) img = 7;
                } else {
                    if (who == 0) sneak_steps = 0;
                    img = ((G(who).firing-- & 2) == 2) ? 4 : 5;
                    if ((G(who).firing & 3) == 1) {
                        if (!flash_active && !(G(who).flags & 2) && G(who).stun < 99) {
                            flash_who = who;
                            flash_active = 1;
                            flash_x = 6 * dir_dx[G(who).dir] + AX(who);
                            flash_y = 6 * dir_dy[G(who).dir] + AY(who);
                            if (flash_who != 0 || ammo != 0) erase_actor_sprite(G(who).dir * 20 - 6, flash_x, flash_y);
                            if (flash_who == 0 && ammo == 0) play_sound(5);
                        } else
                            G(who).firing++;
                    }
                }
                if (who == 0) {
                    if (throw_delay == 0)
                        gfx_.copy(2, (((player_moving || img) ? img : 9) + (sneaking & sneak_steps & 1)) * 24, 160, 25, 40, gp, 0, 0);
                    else {
                        gfx_.copy(2, (13 - throw_delay) * 24, 160, 25, 40, gp, 0, 0);
                        img = 19 - --throw_delay;
                        sneak_steps = 0;
                    }
                    co_await fire_gun(0, 0);
                    if (guards[0].stun >= 0x42 || (dive_count && dive_count < 6)) {
                        len = clamp((guards[0].stun - 0x40) / 2, dive_count, 0x26);
                        gfx_.fillRect(gp, 1, 0x27 - len, 0x17, len, 7);
                    }
                    if (flash_active && flash_who == 0 && ammo) erase_actor_sprite(0x36, 10, 1);
                }
                if (who == 0 && ((equipment & 0x40) ? 3 : 1) < wounds)
                    erase_actor_sprite(0xA6, AX(who), AY(who));
                else if (who != 0 && G(who).stun > 0x20)
                    erase_actor_sprite(((dir_dy[G(who).dir] == -1) ? 4 : 5) + 0xA0, AX(who), AY(who));
                else if (!(G(who).flags & 8))
                    erase_actor_sprite((who ? 8 : ((sneaking & sneak_steps & 1) ? 9 : 0)) + G(who).dir * 20 + img - 20,
                                       AX(who), AY(who));
            }
        } else
            AX(who) = -1;
    }
    for (who = 8; who >= 0; who--) {
        if ((G(who).flags & 0x80) && G(who).stun < 999) {
            auto& mx = actor_map_x[static_cast<std::size_t>(who)];
            auto& my = actor_map_y[static_cast<std::size_t>(who)];
            gfx_.copy(1, mx, my, 2, 2, gp, mx, my);
            mx = 3 * G(who).x / 16 + 180;
            my = 3 * G(who).y / 16 + 100;
            if (who) {
                range = approx_distance(G(who).x - max_x, G(who).y - max_y);
                if ((((equipment & 0x80) ? 0x100 : 0x60) > range || (room(G(who).room).flags & 4) || tac_mode == 2) &&
                    (!(G(who).flags & 8) || !difficulty))
                    gfx_.fillXY(mx, my, mx + 1, my + 1, (mono_display & frame_count & 1) ? 0 : alarm_color);
            } else if ((frame_count & 1) || who)
                gfx_.fillXY(mx, my, mx + 1, my + 1, 0);
        }
    }
    for (who = 8; who >= 0; who--) {
        if (G(who).room == cur_room && (G(who).flags & 0x80) && (G(who).flags & 2)) {
            G(who).flags &= ~2u;
            if (G(who).firing == 0) {
                img = G(who).anim;
                if (G(who).flags & 4) G(who).anim = 7;
            } else
                img = (((G(who).firing + 1) & 2) == 2) ? 4 : 5;
            for (fall = 0; fall < 4; fall++) {
                gfx_.copy(1, AX(who), AY(who), 15, 15, gp, AX(who), AY(who));
                erase_actor_sprite(fall + 0xA0, AX(who), AY(who));
                erase_actor_sprite(G(who).dir * 20 + (who ? 8 : 0) + img - 20, AX(who), AY(who));
                co_await wait_retrace(2);
            }
            if (who) {
                mark_guard_down(who);
                AX(who) = AY(who) = 0;
            } else {
                wounds++;
                draw_equipment_panel();
            }
        }
    }
    update_projectiles(1);
    if (flash_active) co_await fire_gun(flash_who, 1);
}

Task<void> Tac::fire_gun(int who, int flag) {
    int r, nearest, hitwho, hit, angle, i, dx, dy, color;
    auto G = [&](int n) -> Guard& { return guards[static_cast<std::size_t>(n)]; };

    if (who) {
        hitwho = 0;
        if (!guard_can_see_max(who)) hitwho = -1;
    } else {
        if (ammo == 0) co_return;
        if (flag) {
            ammo--;
            draw_equipment_panel();
            made_noise = 1;
        }
        hitwho = -1;
        nearest = 9999;
        for (i = 1; i <= 8; i++) {
            if (!(G(i).flags & 0x80) || G(i).room != cur_room || G(i).stun > 999) continue;
            dx = G(i).x - max_x;
            dy = G(i).y - max_y;
            angle = direction8(dx, dy);
            if (angle != max_dir && ((max_dir - 2) & 7) + 1 != angle) continue;
            if (guard_can_see_max(i)) {
                r = approx_distance(dx, dy);
                if (r < nearest) {
                    hitwho = i;
                    nearest = r;
                }
            }
        }
    }
    hit = 0;
    if (hitwho != -1) {
        r = approx_distance(G(who).x - G(hitwho).x, G(who).y - G(hitwho).y);
        if (hitwho == 0) {
            violence++;
            compute_room_distances(cur_room, 0xF0);
            hit = G(who).spotted / (5 - difficulty);
            if (player_mode != 1) hit++;
            if (difficulty == 0) hit = 1;
        } else {
            hit = G(hitwho).heard / 2;
            if (equipment & 2) hit++;
            if (flag) G(hitwho).heard = 0;
        }
        hit += 1 - r / 48;
        if (flag) {
            if (hitwho == 0) G(who).spotted = 0;
            if (random(4) < hit) {
                G(hitwho).flags |= 2;
                play_sound(hitwho == 0 ? 3 : (equipment & 2) ? 6 : 4);
            } else {
                play_sound(hitwho == 0 ? 0x21 : (equipment & 2) ? 6 : 0x22);
                compute_room_distances(cur_room, 0xF0);
                last_seen_x = max_x / 16;
                last_seen_y = max_y / 16;
            }
        }
    } else if (flag)
        play_sound(0x22);
    if (flag) {
        play_tune(3);
        co_await wait_retrace(1);
        play_tune(0);
    } else if (hit) {
        color = hit_colors[clamp(hit, 0, 4)];
        // (hit_flash_image is never loaded: the image call draws nothing)
        gfx_.recolor(gfx_.gpage, 4, 1, 16, 16, 1, color);
    }
}

Task<void> Tac::grenade_menu(int x, int y, int dir, int fuse, int type) {
    int slot, d, choice;

    for (slot = 0; slot < 16; slot++) {
        Projectile& p = projectiles[static_cast<std::size_t>(slot)];
        if (p.type == -1) {
            if (!(type & 0x80) && fuse != -1) {
                d = ((dir + 2) & 7) + 1;
                x += step_dx[d];
                y += step_dy[d];
            }
            p.x = x;
            p.y = y;
            p.dir = dir;
            p.type = type;
            if (fuse != -1) {
                p.timer = fuse * 10;
                play_sound(0xC);
            } else {
                present_back_buffer();
                numlock_ = false;
                msg_buf = "Set trap\n Cancel\n Remote Control\n Booby Trap\n";
                choice = co_await show_menu(msg_buf, 0x1E, 0x32);
                numlock_ = true;
                if (choice > 0) {
                    play_sound(0xB);
                    grenades[static_cast<std::size_t>(type)]--;
                }
                switch (choice) {
                case 0: p.type = -1; break;
                case 1: p.timer = -9999; break;
                case 2: p.timer = -9998; break;
                default: break;   // Esc: the slot stays taken with its old timer (sic)
                }
                save_to_back_buffer();
                if (choice == 1 || choice == 2) draw_room(0xB4, 100);
            }
            break;
        }
    }
    if (!(type & 0x80)) draw_equipment_panel();
}

void Tac::update_projectiles(int active) {
    int r, new_y, newx, type, in_room, pass, g, px, i, py, cx, cy, shown, cover;
    const int gp = gfx_.gpage;

    for (i = 0; i < 16; i++) {
        Projectile& p = projectiles[static_cast<std::size_t>(i)];
        if (p.type == -1) continue;
        px = p.x - room(cur_room).x * 16 + view_x0;
        py = p.y - room(cur_room).y * 16 + view_y0;
        type = p.type & 0x7F;
        in_room = room_map(p.x / 16, p.y / 16);
        shown = in_room == cur_room && p.timer >= -63;
        if (throw_delay && !(p.type & 0x80)) shown = 0;
        if (p.timer < 0 && p.timer > -9998 && active) {
            do {
                if (shown) {
                    erase_actor_sprite((p.timer & 7) + 0xA8, px - 8, py - 8);
                    if (type == 0) erase_actor_sprite((p.timer & 3) + 0xA0, px - 8, py - 8);
                    if (type == 2 || p.timer == -63) {
                        play_tune(2);
                        play_tune(1);
                        tune_on = 1;
                    }
                }
                if (p.timer == -63) {
                    p.timer = p.type == 1 ? -16 : -8;
                    draw_room(180, 100);
                    if (type != 1) play_sound(in_room == cur_room ? 7 : 8);
                    if (type != 1 && !(p.type & 0x80)) {
                        compute_room_distances(in_room, 240);
                        violence++;
                    }
                    if (in_room != cur_room) {
                        play_tune(4);
                        play_tune(1);
                        tune_on = 1;
                    }
                    for (g = 0; g <= 8; g++) {
                        Guard& gu = guards[static_cast<std::size_t>(g)];
                        if (gu.room == in_room && (gu.flags & 0x80)) {
                            r = approx_distance(gu.x - p.x, gu.y - p.y);
                            if (g)
                                r = r * (difficulty + 2) / 4;
                            else
                                r = (r << 2) / (difficulty + 2);
                            cover = (gu.flags & 4) ? 2 : 1;
                            cover += 2 - line_of_sight(p.x, p.y, gu.x, gu.y);
                            if (type == 0 && 40 / cover > r) {
                                if (g)
                                    mark_guard_down(g);
                                else {
                                    wounds++;
                                    draw_equipment_panel();
                                }
                            }
                            if (type == 2) gu.stun = clamp(gu.stun, (160 - r) / cover, 999);
                        }
                    }
                }
                if (++p.timer == 0) {
                    if (shown) gfx_.copy(1, px - 8, py - 8, 15, 15, gp, px - 8, py - 8);
                    if (type == 1) {
                        room(in_room).flags |= 0x10;
                        draw_room(180, 100);
                    }
                    p.type = -1;
                }
            } while (p.type == 2 && p.timer > -63);
        }
        if (p.timer > 0) {
            if (shown) gfx_.copy(1, px, py, 1, 1, gp, px, py);
            px = p.x * 3 / 16 + 180;
            py = p.y * 3 / 16 + 100;
            gfx_.copy(1, px, py, 1, 1, gp, px, py);
            newx = step_dx[p.dir] + p.x;
            new_y = step_dy[p.dir] + p.y;
            cx = newx / 16;
            cy = new_y / 16;
            pass = (building(cx, cy) & 0x80) && (objFlags(building(cx, cy) & 0x3F) & 0x10);
            if (room_map(cx, cy) != in_room && !pass) {
                p.timer = 1;
                p.x -= step_dx[p.dir] * 2;
                p.y -= step_dy[p.dir] * 2;
            } else {
                p.x = newx;
                p.y = new_y;
            }
            if (shown) {
                px = p.x - room(cur_room).x * 16 + view_x0;
                py = p.y - room(cur_room).y * 16 + view_y0;
                put_pixel(px, py, 10);
            }
            px = p.x * 3 / 16 + 180;
            py = p.y * 3 / 16 + 100;
            put_pixel(px, py, 10);
            if (--p.timer == 0) p.timer = -63;
        }
    }
}

void Tac::put_pixel(int x, int y, int color) { gfx_.line(x, y, x, y, color); }

void Tac::mark_guard_down(int g) {
    Guard& gu = guards[static_cast<std::size_t>(g)];
    const int sx = gu.x * 3 / 16 + 180, sy = gu.y * 3 / 16 + 100;
    gfx_.copy(1, sx, sy, 2, 2, gfx_.gpage, sx, sy);
    room_alert++;
    room(gu.room).flags |= 8;
    violence++;
    gu.stun = 9999;
}

}  // namespace redux
