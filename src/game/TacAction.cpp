// TAC.EXE: drawing the building map and the room (ROOM.C draw_room, ROOMVIEW.C)
// and ACTION.C: examining, photographing, bugging, the status line, the end of
// the mission, searching files, the computer terminal and its password.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "formats/Pic.h"
#include "formats/TextWindow.h"
#include "game/Tac.h"

namespace reaction {

namespace {

constexpr int step_dx[9] = {0, 0, 2, 3, 2, 0, -2, -3, -2};
constexpr int step_dy[9] = {0, -3, -2, 0, 2, 3, 2, 0, -2};
constexpr const int* door_dx = kDoorDx;
constexpr const int* door_dy = kDoorDy;

int clamp(int v, int lo, int hi) { return clampTo(v, lo, hi); }

const char* const item_names[16] = {
    "Pistol", "Uzi", "Camera", "Bugs", "Safecracking Tools", "Gas Mask", "Kevlar Vest", "Motion Detector",
    "Fragmentation Grenades", "Gas Grenades", "Stun Grenades", "Assorted Grenades", "", "", "", "Floor Plan"};
constexpr int file_items[8] = {2, 5, 6, 7, 8, 9, 10, 15};

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool equalNoCase(const std::string& a, const std::string& b) { return upper(a) == upper(b); }

// `1 << i` for an int i as MSC computes it (16 bits, the count taken mod 32)
int intShift(int i) {
    const int n = i & 31;
    return n < 16 ? static_cast<int>(static_cast<std::int16_t>(static_cast<std::uint16_t>(1u << n))) : 0;
}

}  // namespace

// ---------------------------------------------------------------- ROOM.C / ROOMVIEW.C

void Tac::draw_room(int x, int y) {
    int px, i, py, j, sx, k, sy, tile;

    gfx_.gpage = 1;
    gfx_.fillXY(x, y, x + 119, y + 95, 3);
    if (tac_mode != 3) {
        gfx_.rectOutline(x - 1, y - 1, 121, 97, 7);
        for (i = 0; i < 40; i++)
            for (k = 0; k < 32; k++) {
                if ((room(room_map(i, k)).flags & 2) || (equipment & 0x8000)) {
                    px = i * 3 + x;
                    py = k * 3 + y;
                    tile = building(i, k);
                    if (tile != -1) {
                        tile &= 0x3f;
                        sx = obj(tile).sx / 16 * 3;
                        sy = obj(tile).sy / 16 * 3 + 128;
                        gfx_.copy(2, sx, sy, 3, 3, gfx_.gpage, px, py);
                    } else {
                        gfx_.fillXY(px, py, px + 2, py + 2, room_map(i, k) ? 3 : 0);
                        if (room(room_map(i, k)).flags & 0x10) gfx_.fillXY(px, py, px + 2, py + 2, 12);
                    }
                }
            }
    }
    for (j = 0; j < 16; j++) {
        const Projectile& p = projectiles[static_cast<std::size_t>(j)];
        if ((p.type != -1 && p.timer == -9998) || p.timer == -9999) {
            sx = p.x * 3 / 16 + x;
            sy = p.y * 3 / 16 + y;
            gfx_.line(sx - 1, sy - 1, sx + 1, sy + 1, 15);
            gfx_.line(sx + 1, sy - 1, sx - 1, sy + 1, 15);
        }
    }
    gfx_.gpage = 0;
    gfx_.copy(1, x - 1, y - 1, 122, 98, 0, x - 1, y - 1);
}

void Tac::draw_room_objects(int rm) {
    int i, j, sy, tile;
    const TacRoom& r = room(rm);
    const int gp = gfx_.gpage;

    gfx_.fillXY(0, 0, 172, 200, 1);
    for (j = 0; j < 200; j += 2) gfx_.line(0, j, 172, j, 5);
    gfx_.line(171, 0, 171, 199, 15);
    gfx_.line(172, 0, 172, 199, 5);
    view_x0 = -(r.w * 16 / 2 - 88);
    view_y0 = -(r.h * 16 / 2 - 108);
    for (i = 0; i < r.w; i++)
        for (j = 0; j < r.h; j++) gfx_.copy(2, 0, 112, 16, 16, gp, i * 16 + view_x0, j * 16 + view_y0);
    room_left = r.x * 16;
    room_top = r.y * 16;
    room_right = r.w * 16 + room_left - 1;
    room_bottom = r.h * 16 + room_top - 1;
    for (j = 0; j < r.h; j++)
        for (i = 0; i < r.w; i++) {
            tile = building(r.x + i, r.y + j);
            if (tile != -1) {
                sy = obj(tile & 0x3f).sy;
                if (tile & 0x80) sy += 16;
                tile &= 0x3f;
                gfx_.copy(2, obj(tile).sx, sy, 16, 16, gp, i * 16 + view_x0, j * 16 + view_y0);
            }
        }
    gfx_.rectOutline(view_x0 - 1, view_y0 - 1, r.w * 16 + 1, r.h * 16 + 1, 11);
    gfx_.copy(gp, 0, 0, 172, 200, 1, 0, 0);
    gfx_.rectOutline(26, 0, 143, 18, 7);
    gfx_.fillRect(gp, 27, 1, 141, 16, 1);
    gfx_.textTransparent(tac_mode == 3 ? "Street" : kTacRoomTypes[static_cast<std::size_t>(room(cur_room).type) % 10].name, 32,
                         2, 15);
    draw_equipment_panel();
}

void Tac::reveal_rooms(int x, int y) {
    int nx, ny, sx, sy, spr_y, tile, n;

    tile = building(x, y);
    if (tile != -1 && room_map(x, y) == cur_room) {
        spr_y = obj(tile & 0x3f).sy;
        if (tile & 0x80) spr_y += 16;
        tile &= 0x3f;
        sx = (x - room(cur_room).x) * 16 + view_x0;
        sy = (y - room(cur_room).y) * 16 + view_y0;
        gfx_.copy(2, obj(tile).sx, spr_y, 16, 16, gfx_.gpage, sx, sy);
        gfx_.copy(gfx_.gpage, sx, sy, 16, 16, 1, sx, sy);
    }
    if (objFlags(tile & 0x3f) & 0x10) {
        n = tile & 3;
        nx = door_dx[n] + x;
        ny = door_dy[n] + y;
        building(nx, ny) &= 0x7f;
        building(nx, ny) = static_cast<std::int8_t>(building(nx, ny) + (building(x, y) & 0x80));
        if (room_map(nx, ny) == cur_room) {
            play_sound(building(nx, ny) & 0x80 ? 13 : 14);
            reveal_rooms(nx, ny);
        }
    }
}

int Tac::approx_distance(int dx, int dy) {
    dx = std::abs(dx);
    dy = std::abs(dy);
    if (dx > dy) return dy / 2 + dx;
    return dx / 2 + dy;
}

void Tac::erase_actor_sprite(int n, int x, int y) {
    // sprite_imgs[160] is followed by icon_imgs[6], cursor_img and icon2_imgs[8];
    // past those (175: target_person) the original draws garbage: nothing here.
    if (n < 0 || n >= static_cast<int>(sprite_imgs.size())) return;
    gfx_.image(gfx_.gpage, x, y, &sprite_imgs[static_cast<std::size_t>(n)]);
}

void Tac::load_tac_sprites() {
    int i, j;

    // draw_face: the portrait parts (draw_sprite_a's icons), for the case screens
    if (real_mission) faces_ = FaceSprites::load(data_);
    {
        const Picture pic = decodePicture(data_.read((options & 2) ? "spritesf.pic" : "sprites.pic"));
        gfx_.page[2].blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
    }
    switch (cur_org % 3) {
    case 0: alarm_color = 14; break;
    case 1: alarm_color = 11; break;
    default: alarm_color = 15; break;
    }
    if (mono_display)
        alarm_color = 14;
    else
        gfx_.recolor(2, 0, 0, 320, 200, 14, alarm_color);
    for (i = 0; i < 8; i++)
        for (j = 0; j < 20; j++) sprite_imgs[static_cast<std::size_t>(i * 20 + j)] = gfx_.grab(2, j * 16 + 1, i * 16 + 1, 15, 15);
    for (i = 0; i < 6; i++) sprite_imgs[static_cast<std::size_t>(160 + i)] = gfx_.grab(2, i * 16 + 1, 129, 15, 15);
    for (i = 0; i < 8; i++) sprite_imgs[static_cast<std::size_t>(167 + i)] = gfx_.grab(2, i * 16 + 1, 145, 15, 15);
    sprite_imgs[166] = gfx_.grab(2, 97, 129, 15, 15);
}

// ---------------------------------------------------------------- ACTION.C

Task<void> Tac::examine_object() {
    int reply, tile;

    object_in_front();
    msg_buf.clear();
    tile = building(front_x, front_y);
    if (tile != -1) {
        tile &= 0x3F;
        if (tile == 60) {
            if (mission_flags & 1) co_return;
            mission_flags |= 1;
            if (target_person >= 0 && target_person < static_cast<int>(persons.size()))
                persons[static_cast<std::size_t>(target_person)].found |= persons[static_cast<std::size_t>(target_person)].clues;
            building(front_x, front_y) = -1;
        }
        if (tile == 61) {
            if (tac_mode == 2) co_return;
            compute_room_distances(cur_room, 240);
            if (mission_flags & 2) co_return;
            present_back_buffer();
            numlock_ = false;
            play_tune(0);
            if (target_person >= 0 && target_person < static_cast<int>(persons.size())) {
                persons[static_cast<std::size_t>(target_person)].known |= 1;
                hooks_->suspectDossier(*this, target_person);
                co_await lib_play();
            }
            msg_buf = "Do you want to\narrest ";
            hooks_->appendPersonName(*this, target_person);
            msg_buf += "?\n Yes\n No\n";
            reply = co_await show_menu(msg_buf, 100, 140);
            numlock_ = true;
            save_to_back_buffer();
            if (reply) co_return;
            mission_flags |= 2;
            msg_buf.clear();
        }
        tile &= 0x3F;
        msg_buf = obj(tile).name;
        if (objFlags(building(front_x, front_y) & 0x3F) & 4) msg_buf += " ... bug";
        if (objFlags(building(front_x, front_y) & 0x3F) & 8) msg_buf += " ... photo";
        if ((objFlags(building(front_x, front_y) & 0x3F) & 0x200) && !(building(front_x, front_y) & 0x40)) {
            building(front_x, front_y) |= 0x40;
            reveal_password_letter();
        }
        if (objFlags(tile) & 2) {
            if (((tile & 0xFD) != 0x1D && tile != 0x1C) || (equipment & 0x10)) {
                if (building(front_x, front_y) & 0x80) {
                    msg_buf = obj(tile).name + " closed.";
                    doors_open--;
                    play_sound((objFlags(tile & 0x3F) & 0x10) ? 14 : 17);
                } else {
                    doors_open++;
                    play_sound((objFlags(tile & 0x3F) & 0x10) ? 13 : 16);
                }
                building(front_x, front_y) ^= static_cast<std::int8_t>(0x80);
                reveal_rooms(front_x, front_y);
            }
        }
    }
    show_status_line(msg_buf);
}

void Tac::object_in_front() {
    const int x = (step_dx[max_dir] * 3 + max_x) / 16;
    const int y = (step_dy[max_dir] * 3 + max_y) / 16;
    const int tile = building(x, y);
    const TacRoom& r = room(cur_room);
    if (tile != -1 && (tile & 0x3F) > 7 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
        front_x = x;
        front_y = y;
    } else {
        front_x = cell_x;
        front_y = cell_y;
    }
}

Task<void> Tac::take_photo() {
    int saved, worth = 0, ok, tile;

    object_in_front();
    tile = building(front_x, front_y);
    ok = 0;
    msg_buf = "Nothing to photograph.";
    if (tile != -1 && (objFlags(tile & 0x3F) & 8)) {
        if (tile & 0x40)
            msg_buf = "Already photographed.";
        else if ((tile & 0x80) || !(objFlags(tile & 0x3F) & 2)) {
            msg_buf = obj(tile & 0x3F).name + " photographed.";
            building(front_x, front_y) |= 0x40;
            ok = 1;
        } else
            msg_buf = "Open " + obj(tile & 0x3F).name + " first.";
    }
    if (photos_taken >= 36) {
        ok = 0;
        msg_buf = "Out of film.";
    }
    show_status_line(msg_buf);
    if (ok) {
        photos_taken++;
        photo_score += kTacRoomTypes[static_cast<std::size_t>(room(cur_room).type) % 10].value;
        draw_film_count();
        play_tune(1);
        play_sound(9);
        co_await wait_retrace(5);
        play_tune(0);
        tile &= 0x3F;
        if (tile >= 24) worth = 2;
        if (tile >= 29) worth = 3;
        if (tile >= 32) worth = 2;
        if (tile == 28) worth = 4;
        if (real_mission && !(options & 0x20)) {
            popup_x = (front_x - room(cur_room).x) * 16 + view_x0;
            popup_y = (front_y - room(cur_room).y) * 16 + view_y0;
            saved = difficulty;
            difficulty = orig_difficulty;
            present_back_buffer();
            if (worth != 4) {
                hooks_->describeNewInfo(*this, cur_org, cur_city,
                                        kTacRoomTypes[static_cast<std::size_t>(room(cur_room).type) % 10].value * worth, worth);
                co_await lib_play();
            } else
                co_await search_files();
            save_to_back_buffer();
            gfx_.fillcolor = 1;
            if (worth == 4) {
                draw_room(180, 100);
                draw_equipment_panel();
            }
            difficulty = saved;
        }
    }
}

void Tac::plant_bug() {
    object_in_front();
    const int t = building(front_x, front_y);
    if (!bugs_left) return;
    if (t != -1 && (objFlags(t & 0x3F) & 4) && !(room(cur_room).flags & 4)) {
        bugs_planted++;
        play_sound(10);
        bug_score += kTacRoomTypes[static_cast<std::size_t>(room(cur_room).type) % 10].value;
        room(cur_room).flags |= 4;
        bugs_left--;
        draw_equipment_panel();
        msg_buf = obj(t & 0x3F).name + " bugged.";
    } else
        msg_buf = (room(cur_room).flags & 4) ? "Room already bugged." : "Nothing to bug.";
    show_status_line(msg_buf);
}

Task<void> Tac::use_object() {
    object_in_front();
    const int t = building(front_x, front_y);
    if (t != -1 && (objFlags(t & 0x3F) & 0x200)) co_await computer_terminal();
}

void Tac::draw_disguise_status() {
    gfx_.fillXY(123, 2, 168, 9, 1);
    if (frame_count & 4) {
        if (room(cur_room).flags & 0x10)
            gfx_.textTransparent("GAS", 132, 2, 12);
        else
            gfx_.textTransparent(format_clock_hms(frame_count), 124, 2, 15);
    } else if (disguised)
        gfx_.textTransparent("Disguise", 124, 2, 2);
    else
        gfx_.textTransparent(format_clock_hms(frame_count), 124, 2, 15);
}

void Tac::show_status_line(const std::string& s) {
    gfx_.fillXY(27, 10, 168, 17, 1);
    gfx_.textTransparent(s, 100 - static_cast<int>(s.size()) * 3, 10, 15);
}

Task<void> Tac::mission_end_text() {
    if (cur_room) {
        result_score = -1;
        if (guards[0].stun > 256)
            msg_buf = "You are overcome\nby gas and pass out.\n";
        else if (((equipment & 0x40) ? 3 : 1) < wounds)
            msg_buf = "You slump to\nthe ground, overcome\nby your wounds.\n";
        else
            msg_buf = "The prisoner is\nreleased while you\nare out of the room.\n";
    } else {
        result_score = bug_score;
        if (frame_count > 64 && (tac_mode == 3 || tac_mode == 2) && guards_near == 0)
            msg_buf = "You have eliminated\nall of your enemies.\n";
        else
            msg_buf = "You quickly slip\nout of the building.\n";
    }
    message_shown = 1;
    co_await show_menu(msg_buf, 32, 100);
    co_await wait_retrace(120);
}

Task<void> Tac::search_files() {
    int tries = 0, i, k;
    auto P = [&](int p) -> TacSetup::Person& {
        static TacSetup::Person none;
        if (p < 0 || p >= static_cast<int>(persons.size())) {
            none = {};
            return none;
        }
        return persons[static_cast<std::size_t>(p)];
    };
    auto cityName = [&](int c) { return c >= 0 && c < 16 ? setup_.cityNames[static_cast<std::size_t>(c)] : std::string(); };

    do {
        switch (random(5)) {
        case 4:
            k = cur_city;
            if (!(raided_cities & (1u << cur_city))) k = random(16);
            if (raided_cities & (1u << k)) {
                msg_buf = "The CIA agent in\n" + cityName(k) + " is\na double agent!!\n";
                co_await show_menu(msg_buf, 20, 100);
                tries = 999;
            }
            break;
        case 0:
            if (target_person != -1 && (P(target_person).clues & 0x7FFF) && random(3) == 0)
                k = 0;
            else
                k = cur_org;
            if (!(known_plans & (1u << k))) {
                hooks_->showMasterPlan(*this, k, 1);
                co_await lib_play();
                tries = 999;
                break;
            }
            k = random(4);
            if (setup_.agents[static_cast<std::size_t>(k)].person != -1) {
                msg_buf = setup_.agents[static_cast<std::size_t>(k)].name + " now\nin " +
                          cityName(P(setup_.agents[static_cast<std::size_t>(k)].person).city) + "!\n";
                co_await show_menu(msg_buf, 20, 100);
                tries = 999;
            }
            break;
        case 1:
            k = file_items[random(8)];
            if (!(equipment & (1u << k))) {
                msg_buf = std::string("You find\n") + item_names[k] + "!\n";
                co_await show_menu(msg_buf, 20, 80);
                equipment |= 1u << k;
                if (k == 15)
                    for (i = 0; i <= map_.nRooms; i++) room(i).flags |= 2;
                if (k == 8) grenades[0] = clamp(grenades[0] + 2, 0, 8);
                if (k == 9) grenades[1] = clamp(grenades[1] + 2, 0, 8);
                if (k == 10) grenades[2] = clamp(grenades[2] + 2, 0, 8);
                tries = 999;
            }
            break;
        default: {   // 2, 3
            i = random(n_clues - orig_difficulty * 4);
            k = intShift(i);
            TacSetup::Person& t = P(target_person);
            if (target_person == -1 || !(k & t.clues) || ((mission_flags & 4) && (k & t.found))) break;
            msg_buf.clear();
            if (!(k & t.found)) {
                t.found |= k;
                hooks_->suspectDossier(*this, target_person);
                co_await lib_play();
                msg_buf.clear();
                hooks_->appendPersonName(*this, target_person);
                msg_buf += " has\nbeen identified as\na participant\nin the plot.\n";
            } else if (!(mission_flags & 4) && target_person != 0) {
                hooks_->suspectDossier(*this, target_person);
                co_await lib_play();
                msg_buf = "You find incriminating\nevidence which will\nturn ";
                hooks_->appendPersonName(*this, target_person);
                msg_buf += "\ninto a double agent!\n";
                mission_flags |= 4;
            }
            if (!msg_buf.empty()) {
                co_await show_menu(msg_buf, 20, 100);
                tries = 999;
            }
            break;
        }
        }
        tries++;
    } while (tries < 9);
}

void Tac::choose_password() {
    const Bytes bytes = data_.read("text.dta");
    std::size_t end = bytes.size();
    for (std::size_t i = 0; i < bytes.size(); ++i)
        if (bytes[i] == 0x1A) {      // text mode: Ctrl-Z ends the file
            end = i;
            break;
        }
    std::size_t pos = static_cast<std::size_t>(random(20000));
    auto isws = [](unsigned char c) { return c == ' ' || (c >= 9 && c <= 13); };
    // fscanf("%32s")
    auto word = [&](std::string& out) {
        while (pos < end && isws(bytes[pos])) ++pos;
        if (pos >= end) return false;
        out.clear();
        while (pos < end && !isws(bytes[pos]) && out.size() < 32) out += static_cast<char>(bytes[pos++]);
        return true;
    };
    std::string w;
    word(w);
    password = w;
    int ok = 0, wraps = 0;
    do {
        if (!word(w)) {     // (the original would loop forever at the end of the file)
            pos = 0;
            if (++wraps > 1) {
                password = "covert";
                break;
            }
            continue;
        }
        password = w;
        if (password.size() >= 5 && password.size() <= 9) {
            ok = 1;
            for (char c : password)
                if (c < 'a' || c > 'z') {
                    ok = 0;
                    break;
                }
        }
    } while (!ok);
    password_shown = std::string(password.size(), '.');
}

void Tac::reveal_password_letter() {
    int tries = 0, k;
    do {
        k = random(static_cast<int>(password.size()));
        if (password[static_cast<std::size_t>(k)] != ' ' && password_shown[static_cast<std::size_t>(k)] == '.') {
            password_shown[static_cast<std::size_t>(k)] = password[static_cast<std::size_t>(k)];
            tries = 99;
        }
        tries++;
    } while (tries < 99);
    gfx_.fillRect(gfx_.gpage, 248, 88, 64, 9, 10);
    gfx_.rectOutline(248, 88, 64, 9, 2);
    gfx_.textTransparent(password_shown, 250, 89, 5);
}

Task<void> Tac::computer_terminal() {
    present_back_buffer();
    draw_terminal_frame("Enter PassWord:");
    msg_buf.clear();
    if (co_await terminal_input(116, 100, 12) != -1) {
        draw_terminal_frame("");
        if (equalNoCase(msg_buf, password)) {
            gfx_.textCentered("Access CONFIRMED", 160, 103, 10);
            co_await terminal_search();
            if (random(3) < orig_difficulty) compute_room_distances(cur_room, 240);
        } else {
            gfx_.textCentered("Access DENIED", 160, 103, 12);
            compute_room_distances(cur_room, 240);
            co_await wait_retrace(90);
        }
        choose_password();
    }
    gfx_.fillcolor = 1;
    save_to_back_buffer();
    draw_equipment_panel();
}

void Tac::draw_terminal_frame(const std::string& title) {
    gfx_.fillXY(99, 85, 221, 114, 8);
    gfx_.fillXY(100, 87, 220, 113, 5);
    gfx_.fillXY(101, 88, 219, 112, 8);
    gfx_.fillXY(105, 101, 215, 110, 5);
    gfx_.line(101, 86, 219, 86, 7);
    gfx_.line(103, 99, 217, 99, 5);
    gfx_.line(102, 112, 218, 112, 5);
    gfx_.line(104, 101, 104, 110, 7);
    gfx_.line(216, 101, 216, 110, 7);
    gfx_.textCentered(title, 161, 90, 2);
}

Task<void> Tac::terminal_search() {
    int p, tries, j, k, found = 0;
    std::string word;
    auto cityName = [&](int c) { return c >= 0 && c < 16 ? setup_.cityNames[static_cast<std::size_t>(c)] : std::string(); };
    auto orgName = [&](int o) { return o >= 0 && o < 16 ? setup_.orgNames[static_cast<std::size_t>(o)] : std::string(); };
    auto contains = [](const std::string& hay, const std::string& needle) {
        return upper(hay).find(needle) != std::string::npos;
    };

    co_await wait_retrace(60);
    tries = 0;
    for (;;) {
        draw_terminal_frame("Search for:");
        msg_buf.clear();
        co_await terminal_input(106, 100, 15);
        flush_input();
        word = upper(msg_buf);
        if (word.empty()) co_return;
        for (p = 0; p < static_cast<int>(persons.size()) && p < 64; p++) {
            TacSetup::Person& pe = persons[static_cast<std::size_t>(p)];
            if (!pe.id) continue;
            msg_buf.clear();
            hooks_->personName(*this, p);
            if (!contains(msg_buf, word)) continue;
            if (pe.city != cur_city && pe.org != cur_org && orig_difficulty >= 2) continue;
            pe.known |= 0x1f;
            if (pe.city == cur_city && pe.org == cur_org) pe.found |= pe.clues & 0x7fff;
            hooks_->suspectDossier(*this, p);
            co_await lib_play();
            co_await wait_key_or_button();
            co_return;
        }
        bool show = false;
        for (j = 0; j < 16 && !show; j++) {
            if (!cityName(j).empty() && contains(cityName(j), word)) {
                msg_buf = orgName(cur_org) + " has\n";
                if (hooks_->orgAlertLevel(*this, j, cur_org)) {
                    msg_buf += "a";
                    hooks_->appendBuildingType(*this, j, cur_org);
                    // (cities[j].orgs_known |= 1 << cur_org: set_location_known does it too)
                    hooks_->setLocationKnown(*this, cur_org, j, 12);
                    found = 1;
                } else {
                    msg_buf += "no operatives";
                    found = 0;
                }
                msg_buf += "\nin " + cityName(j) + "\n";
                if (found) {
                    popup_x = 100;
                    popup_y = 100;
                    popup_face = -2;
                    draw_intercept_popup();
                    co_await wait_key_or_button();
                    co_return;
                }
                show = true;
            }
        }
        for (j = 2; j < 16 && !show; j++) {
            if (!orgName(j).empty() && contains(orgName(j), word)) {
                msg_buf = orgName(j);
                if (hooks_->orgAlertLevel(*this, cur_city, j)) {
                    hooks_->appendBuildingType(*this, cur_city, j);
                    hooks_->setLocationKnown(*this, j, cur_city, 12);
                    found = 1;
                } else {
                    msg_buf += " has\nno operatives";
                    found = 0;
                }
                msg_buf += "\nin " + cityName(cur_city) + "\n";
                if (!found) {
                    show = true;
                    break;
                }
                popup_x = 100;
                popup_y = 100;
                popup_face = -2;
                draw_intercept_popup();
                co_await wait_key_or_button();
                co_return;
            }
        }
        if (!show) {
            for (j = 0; j < n_clues; j++) {
                k = hooks_->loadClueText(*this, j, 0);
                msg_buf.clear();
                hooks_->appendNumber(*this, k);
                if (contains(msg_buf, word) && hooks_->revealRandomClue(*this, j, cur_org, cur_city)) {
                    co_await lib_play();
                    co_return;
                }
            }
            for (j = 0; j < 4 && !show; j++) {
                const TacAgent& a = setup_.agents[static_cast<std::size_t>(j)];
                if (a.person != -1 && contains(a.name, word)) {
                    const int city = a.person < static_cast<int>(persons.size()) ? persons[static_cast<std::size_t>(a.person)].city : 0;
                    msg_buf = a.name + " now\nin " + cityName(city) + "!\n";
                    show = true;
                }
            }
        }
        if (show) {
            co_await show_menu(msg_buf, 100, 80);
            co_return;
        }
        draw_terminal_frame("");
        gfx_.textCentered("No Data", 160, 103, 7);
        co_await wait_retrace(90);
        if (++tries >= 3) co_return;
    }
    (void)k;
}

Task<int> Tac::terminal_input(int x, int y, int n) {
    int first, key, i;
    char mb[160] = {};
    char tmp[160];

    std::strncpy(mb, msg_buf.c_str(), 64);
    gfx_.fillRect(gfx_.gpage, x, y + 1, n * 7, 9, 5);
    gfx_.fillcolor = 5;
    cursor_pos = 0;
    first = 1;
    for (i = static_cast<int>(std::strlen(mb)); i < n; i++) mb[i] = ' ';
    mb[n] = 0;
    msg_buf = mb;
    draw_input_field(x, y, n);
    flush_input();
    numlock_ = false;
    for (;;) {
        while (!kbhit()) {
            gfx_.recolor(gfx_.gpage, cursor_x, y + 1, cursor_w, 10, 5, 2);
            co_await wait_retrace(10);
            gfx_.recolor(gfx_.gpage, cursor_x, y + 1, cursor_w, 10, 2, 5);
            if (!kbhit()) co_await wait_retrace(10);
        }
        key = get_key_now();
        if (key == 13) {
            numlock_ = true;
            break;
        }
        if (key == 0x4b00 && cursor_pos) cursor_pos--;
        if (key == 0x4d00 && cursor_pos < n - 1) cursor_pos++;
        if (key == 0x5200) {
            std::strcpy(tmp, mb + cursor_pos);
            std::strcpy(mb + cursor_pos + 1, tmp);
            mb[cursor_pos] = ' ';
            mb[n] = 0;
        }
        if (key == 0x5300) {
            std::memmove(mb + cursor_pos, mb + cursor_pos + 1, std::strlen(mb + cursor_pos + 1) + 1);
            mb[cursor_pos] = ' ';
            mb[n - 1] = ' ';
            mb[n] = 0;
        }
        if (key == 8 && cursor_pos) {
            std::memmove(mb + cursor_pos - 1, mb + cursor_pos, std::strlen(mb + cursor_pos) + 1);
            mb[n - 1] = ' ';
            mb[n] = 0;
            cursor_pos--;
        }
        if (key < 0x80 && key >= 0x20) {
            if (first)
                for (i = 0; i < n; i++) mb[i] = ' ';
            mb[cursor_pos] = static_cast<char>(key);
            if (cursor_pos < n - 1) cursor_pos++;
        }
        msg_buf = mb;
        if (key == 27) co_return -1;
        first = 0;
        draw_input_field(x, y, n);
    }
    for (i = n - 1; i >= 0 && mb[i] == ' '; i--) mb[i] = 0;
    msg_buf = mb;
    co_return 1;
}

void Tac::draw_input_field(int x, int y, int n) {
    x += 2;
    y += 2;
    for (int i = 0; i < n; i++) {
        gfx_.fillRect(gfx_.gpage, x, y, 8, 8, 5);
        if (static_cast<int>(msg_buf.size()) > i) {
            const std::string ch(1, msg_buf[static_cast<std::size_t>(i)]);
            gfx_.text(ch, x, y, 10);
            if (i == cursor_pos) {
                cursor_x = x;
                cursor_w = gfx_.font->charWidth(static_cast<std::uint8_t>(ch[0]));
            }
            x += gfx_.font->charWidth(static_cast<std::uint8_t>(ch[0]));
        }
    }
}

void Tac::draw_intercept_popup() {
    const int x = clamp(31, popup_x - 146, popup_x);
    const int y = clamp(49, popup_y - 102, popup_y);
    gfx_.image(gfx_.gpage, x, y, &camera_img);
    if (popup_face > -1) {
        drawPortrait(gpage(), faces_, popup_face, x + 66, y + 24);
        popup_face = -1;   // (draw_face_parts: face_shown, which is popup_face)
    }
    if (popup_face == -2) gfx_.image(gfx_.gpage, x + 55, y + 7, &camera_img2);
    gfx_.fillcolor = 11;
    drawWrappedText(gfx_.page[static_cast<std::size_t>(gfx_.gpage)], *gfx_.font, msg_buf, 14, x + 20, y + 67, 3);
    drawWrappedText(gfx_.page[static_cast<std::size_t>(gfx_.gpage)], *gfx_.font, msg_buf, 14, x + 19, y + 67, 5);
    gfx_.rectOutline(x + 13, y + 10, 119, 78, 15);
    gfx_.rectOutline(x + 57, y + 41, 31, 16, 15);
}

void Tac::present_back_buffer() { gfx_.copy(gfx_.gpage, 0, 0, 320, 200, 3, 0, 0); }

void Tac::save_to_back_buffer() { gfx_.copy(3, 0, 0, 320, 200, gfx_.gpage, 0, 0); }

std::string Tac::format_clock_hms(int frames) const {
    const unsigned long m = static_cast<unsigned long>(static_cast<std::uint16_t>(frames / 60 + static_cast<int>(clock_base))) % 1440;
    const unsigned h = static_cast<unsigned>(m / 60), mi = static_cast<unsigned>(m % 60);
    const int s = frames % 60;
    std::string out = "00:00:00";
    out[0] = static_cast<char>(h / 10 + '0');
    out[1] = static_cast<char>(h % 10 + '0');
    out[3] = static_cast<char>(mi / 10 + '0');
    out[4] = static_cast<char>(mi % 10 + '0');
    out[6] = static_cast<char>(s / 10 + '0');
    out[7] = static_cast<char>(s % 10 + '0');
    return out;
}

}  // namespace reaction
