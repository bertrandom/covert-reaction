// TAC.EXE GUARD.C: the guards' AI, room distances and path finding, guard
// placement, line of sight, the equipment room and the equipment panel.
#include <cstdlib>
#include <cstring>

#include "formats/Pic.h"
#include "game/Tac.h"

namespace reaction {

namespace {

constexpr int step_dx[9] = {0, 0, 2, 3, 2, 0, -2, -3, -2};
constexpr int step_dy[9] = {0, -3, -2, 0, 2, 3, 2, 0, -2};
constexpr int dir_dx[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
constexpr int dir_dy[9] = {0, -1, -1, 0, 1, 1, 1, 0, -1};
constexpr const int* door_dx = kDoorDx;
constexpr const int* door_dy = kDoorDy;
constexpr int dir_from_sign[3][3] = {{8, 7, 6}, {1, 0, 5}, {2, 3, 4}};
constexpr int room_none = -1;

int clamp(int v, int lo, int hi) { return clampTo(v, lo, hi); }
int sign(int n) { return n > 0 ? 1 : n < 0 ? -1 : 0; }

struct Nav {
    int up, down, left, right;
};
constexpr Nav equip_nav[12] = {
    {0, 0, 0, 0}, {1, 2, 1, 5}, {1, 3, 2, 6}, {2, 8, 3, 6}, {6, 4, 8, 4}, {5, 6, 1, 7},
    {7, 4, 2, 6}, {7, 6, 5, 7}, {3, 9, 8, 4}, {8, 10, 9, 4}, {9, 11, 10, 4}, {10, 11, 11, 4},
};
struct Rect {
    int x0, y0, x1, y1;
};
constexpr Rect equip_pic_rects[15] = {
    {162, 25, 189, 38},   {162, 71, 220, 93},   {272, 101, 296, 122}, {258, 41, 264, 45},  {220, 172, 273, 198},
    {254, 1, 291, 31},    {223, 71, 270, 132},  {246, 41, 255, 58},   {285, 137, 293, 149}, {285, 152, 293, 164},
    {285, 167, 293, 179}, {201, 41, 203, 47},   {188, 41, 198, 55},   {162, 95, 170, 101},  {298, 101, 312, 113},
};
constexpr Rect equip_rects[12] = {
    {0, 0, 0, 0},       {2, 15, 62, 39},     {2, 54, 62, 72},    {2, 88, 62, 105},
    {69, 136, 157, 184}, {69, 15, 113, 50},  {69, 60, 157, 126}, {116, 15, 157, 50},
    {2, 122, 62, 138},  {2, 140, 62, 154},   {2, 156, 62, 170},  {2, 172, 62, 186},
};

}  // namespace

Task<void> Tac::guard_think(int n) {
    int nvals, chase, dist, here, tries, destx = 0, mindist, desty = 0, wayout, i, minvals, ok;
    int cx, sx, dx, cy, sy, rm, dy, item, dir;
    Guard& g = guards[static_cast<std::size_t>(n)];

    if (n == 1) {
        last_at_goal = guards_at_goal;
        guards_at_goal = max_heard = guards_active = guards_near = guard_in_room = 0;
        if (!frame_count) {
            compute_room_distances(post_room, 0);
            for (i = 0; i < 24; i++) room_dist[static_cast<std::size_t>(i)] = room(i).dist;
            compute_room_distances(start_room, 0);
        }
        if (made_noise && target_person != -1 && cur_room == start_room) compute_room_distances(start_room, 240);
    }
    if (!(g.flags & 0x80) && !(frame_count & 15) &&
        ((tac_mode != 2 && (random(1000 / (difficulty + 1)) < alarm_timer || !frame_count)) ||
         ((tac_mode == 2 || on_alert) && (n & 7) == ((frame_count >> 5) & 7))))
        spawn_guard(n);
    if (!(g.flags & 0x80)) co_return;
    if (g.stun < 999) guards_active++;
    if (g.stun < 24) guards_near++;
    if (g.room == goal_room && (g.stun < 999 || difficulty < 2) && ++guards_at_goal >= 2 && goal_room != cur_room)
        compute_room_distances(-1, 0);
    if (tac_mode == 2 && g.room == start_room && g.stun < 24 && cur_room != start_room) mission_over = 1;
    cx = g.x / 16;
    cy = g.y / 16;
    if (guard_sees_max(n) && g.stun < 24 && tac_mode != 1) {
        if (disguised) {
            dx = g.x - guards[0].x;
            dy = g.y - guards[0].y;
            dir = direction8_coarse(dx, dy);
            dir = std::abs(dir - guards[0].dir);
            if (++max_moved > 256 / (difficulty + 1) || dir < 2 || dir > 6)
                g.spotted += 2;
            else
                g.spotted = 0;
        } else {
            g.spotted++;
            g.flags &= ~1u;
            last_seen_x = max_x / 16;
            last_seen_y = max_y / 16;
            g.wait = 1;
        }
    } else
        g.spotted = 0;
    if (guard_saw_max) {
        if (made_noise && g.stun < 24) {
            disguised = 0;
            compute_room_distances(cur_room, 240);
        }
        if (++g.heard > max_heard && g.stun < 999) max_heard = g.heard;
    } else
        g.heard = 0;
    if (g.room == cur_room) {
        if (g.stun < 999) guard_in_room = 1;
        if (guard_saw_max || g.firing || !alarm_timer || goal_room != cur_room)
            g.flags &= ~8u;
        else if (!(frame_count & 3))
            g.flags |= 8;
    }
    if ((room(g.room).flags & 0x10) && (n & 3)) g.stun += 2;
    if (g.stun && --g.stun > 32) {
        g.spotted = 0;
        g.flags |= 4;
        co_return;
    }
    if (g.room == goal_room && goal_room == cur_room && alarm_timer && !(frame_count & 7)) {
        g.flags |= 4;
        if (last_seen_x && line_of_sight(g.x, g.y, last_seen_x * 16 + 8, last_seen_y * 16 + 8) + (see_bits & 1) >= 2)
            g.flags &= ~4u;
    }
    if (g.spotted) {
        dx = max_x - g.x;
        dy = max_y - g.y;
        dir = direction8_coarse(dx, dy);
        if (dir) g.dir = dir;
        if (g.room == cur_room && g.spotted > (5 - difficulty) * 7 - 6) {
            compute_room_distances(cur_room, 240);
            room_noise[static_cast<std::size_t>(cur_room)] = 0;
            if (!g.firing) g.firing += 4;
            g.wait = 9;
            g.flags &= ~4u;
        }
    }
    rm = room_map(cx, cy);
    if (!rm) {
        g.flags &= ~0x80u;
        guards_left++;
        co_return;
    }
    if (room(rm).flags & 8) {
        room(rm).flags &= static_cast<std::uint16_t>(~8u);
        if (!alarm_timer)
            compute_room_distances(rm, 240);
        else
            alarm_timer = 240;
    }
    chase = g.room == goal_room && alarm_timer;
    if (g.tx == -1 && !g.wait && !g.firing) {
        ok = 0;
        tries = 0;
        if (chase) {
            mindist = 999;
            destx = -1;
            do {
                dx = room(rm).x + random(room(rm).w);
                dy = room(rm).y + random(room(rm).h);
                item = building(dx, dy);
                dist = std::abs(dx - last_seen_x) + std::abs(dy - last_seen_y);
                dist += std::abs(dx - cx) + std::abs(dy - cy);
                if (item == -1 && dist < mindist &&
                    line_of_sight(dx * 16 + 8, dy * 16 + 8, last_seen_x * 16 + 8, last_seen_y * 16 + 8) + (see_bits & 1) < 2) {
                    destx = dx;
                    desty = dy;
                    mindist = dist;
                }
            } while (++tries < 16 || (tries < 32 && destx == -1));
        } else {
            do {
                destx = room(rm).x + random(room(rm).w);
                desty = room(rm).y + random(room(rm).h);
                item = building(destx, desty);
                if (item != -1 && (objFlags(item & 0x3f) & 0x21c)) ok = 1;
                if (ok) break;
            } while (++tries < kTacRoomTypes[static_cast<std::size_t>(room(rm).type)].value - alert_level / 16 - violence * 2);
            if (!ok || (alarm_timer && rm != goal_room) || (room(rm).flags & 0x10)) {
                wayout = guard_choose_goal(rm, n);
                find_door(rm, wayout, &destx, &desty);
                g.flags |= 1;
            }
            if (!ok && guard_reserve_check(n) == 2 && g.room == post_room && !alarm_timer) {
                destx = room(rm).x + random(room(rm).w);
                desty = room(rm).y + random(room(rm).h);
                g.flags &= ~1u;
            }
        }
        if (destx == -1) {
            destx = cx;
            desty = cy;
        }
        guard_plan_path(n, rm, cx, cy, destx, desty);
        if (!gpath(n, cx, cy) && g.room != cur_room) {
            g.flags &= ~0x80u;
            guards_left++;
            co_return;
        }
        g.tx = destx;
        g.ty = desty;
        g.room = rm;
    }
    if (!g.stun) {
        for (i = 0; i < 16; i++) {
            Projectile& p = projectiles[static_cast<std::size_t>(i)];
            if (p.type != -1 && p.timer == -9998 && std::abs(p.x - g.x) + std::abs(p.y - g.y) < 24 &&
                room_map(p.x / 16, p.y / 16) == g.room) {
                p.timer = -63;
                break;
            }
        }
    }
    if (!g.wait && !g.firing) {
        here = gpath(n, cx, cy);
        minvals = 999;
        dir = 0;
        for (i = 0; i <= 8; i++) {
            nvals = gpath(n, dir_dx[i] + cx, dir_dy[i] + cy);
            if (nvals && (here - 1 == nvals || (!here && nvals < minvals)) && (!dir || (i & 1))) {
                dir = i;
                minvals = nvals;
            }
        }
        sx = (dir_dx[dir] + cx) * 16 + 8;
        sy = (dir_dy[dir] + cy) * 16 + 8;
        dx = (sx - g.x) / 2;
        dy = (sy - g.y) / 2;
        dir = dir_from_sign[sign(dx) + 1][sign(dy) + 1];
        if (dir) {
            if (g.dir == dir) {
                if (g.flags & 4) {
                    g.x += dir_dx[dir];
                    g.y += dir_dy[dir];
                } else {
                    g.x += step_dx[dir];
                    g.y += step_dy[dir];
                }
            } else {
                if (((dir - g.dir) & 7) < 4)
                    g.dir++;
                else
                    g.dir--;
                g.dir = ((g.dir - 1) & 7) + 1;
                if (g.dir != dir) g.flags &= ~4u;
            }
        }
        g.anim = (g.anim + 1) & 3;
        if (std::abs(dx) < 2 && std::abs(dy) < 2) {
            dx = g.tx - cx;
            dy = g.ty - cy;
            dir = dir_from_sign[sign(dx) + 1][sign(dy) + 1];
            if (dir)
                g.dir = dir;
            else
                g.dir = (building(cx, cy) & 3) * 2 + 1;
            if (chase) {
                g.tx = -1;
                dx = last_seen_x * 16 - g.x;
                dy = last_seen_y * 16 - g.y;
                dir = 0;
                if (std::abs(dx) < 12 || std::abs(dy) < 12 || std::abs(std::abs(dx) - std::abs(dy)) < 12) {
                    dir = direction8_coarse(dx, dy);
                    dist = approx_distance(dx, dy);
                    if (dist > 32 && dist < 112 && frame_count - last_grenade > (5 - difficulty) * 8) {
                        co_await grenade_menu(g.x, g.y, dir, (dist + 16) / 32, 0x80);
                        last_grenade = frame_count;
                    }
                }
                g.wait = random(8) + (4 - difficulty) * 4;
            } else {
                g.wait = (4 - difficulty) * 6 + random(8);
                g.anim = 0;
                item = building(g.tx, g.ty);
                if (item != -1 && g.tx != -1) {
                    item &= 0x3f;
                    if (objFlags(item) & 2) {
                        if (g.room == cur_room && !(objFlags(item) & 0x10)) doors_open++;
                        building(g.tx, g.ty) |= static_cast<std::int8_t>(0x80);
                        reveal_rooms(g.tx, g.ty);
                        if (!(g.flags & 1)) g.wait += 18;
                    }
                }
            }
            if (g.flags & 1) {
                if (orig_difficulty > 1 && alarm_timer && !last_at_goal &&
                    room_map(dir_dx[g.dir] + cx, dir_dy[g.dir] + cy) == goal_room) {
                    g.wait = random(3) * 5 + 9;
                    co_await grenade_menu(g.x, g.y, g.dir, g.wait / 5, 0x82);
                }
                g.dir = ((g.dir + 3) & 7) + 1;
            }
        }
    }
    if (g.wait && !--g.wait) {
        item = building(g.tx, g.ty);
        if (item != -1 && g.tx != -1) {
            item &= 0x3f;
            if (objFlags(item) & 2) {
                if (g.room == cur_room && !(objFlags(item) & 0x10)) doors_open--;
                building(g.tx, g.ty) &= 0x7f;
                reveal_rooms(g.tx, g.ty);
            }
        }
        g.tx = -1;
        if (g.flags & 1) {
            for (i = 0; i < 4; i++) {
                dx = door_dx[i] + cx;
                dy = door_dy[i] + cy;
                if (g.room != room_map(dx, dy)) {
                    g.x = dx * 16 + 8;
                    g.y = dy * 16 + 8;
                    g.room = room_map(dx, dy);
                    g.dir = i * 2 + 1;
                    g.spotted = 10;
                    break;
                }
            }
            g.flags &= ~1u;
        }
    }
}

int Tac::guard_choose_goal(int rm, int guard) {
    int mode, mindist, i, next = 0;

    mindist = 9999;
    mode = guard_reserve_check(guard);
    for (i = 0; i < map_.nDoors; i++) {
        const TacDoor& d = map_.doors[static_cast<std::size_t>(i)];
        if (d.roomA == rm && guard_next_room(d.roomB, mode) < mindist && d.roomB) {
            next = d.roomB;
            mindist = guard_next_room(next, mode);
        }
        if (d.roomB == rm && guard_next_room(d.roomA, mode) < mindist && d.roomA) {
            next = d.roomA;
            mindist = guard_next_room(next, mode);
        }
    }
    rm = next;
    if (rm >= 0 && rm < 24) room_noise[static_cast<std::size_t>(rm)] = frame_count + 1;
    return rm;
}

int Tac::guard_next_room(int rm, int mode) {
    switch (mode) {
    case 0:
        return room_noise[static_cast<std::size_t>(rm)];
    case 1:
        if (goal_room > 0) return room(rm).dist * 4 + random(7);
        return room_noise[static_cast<std::size_t>(rm)];
    case -1:
    case 2:
        if (alarm_timer) return room(rm).dist * 4 + random(7);
        return room_dist[static_cast<std::size_t>(rm)] * 4 + random(7);
    }
    return 0;   // (falls off the end: whatever AX held)
}

void Tac::compute_room_distances(int rm, int noise) {
    int a, b, done, i;

    if (noise) disguised = 0;
    noise = noise / 4 * (difficulty + 4);
    goal_room = rm;
    if (tac_mode == 2) {
        if (rm == -1) goal_room = start_room;
        rm = start_room;
    }
    alarm_timer = clamp(alarm_timer, noise, 9999);
    if (rm == room_none) return;
    for (i = 0; i <= map_.nRooms; i++) room(i).dist = -1;
    room(rm).dist = 0;
    do {
        done = 1;
        for (i = 0; i < map_.nDoors; i++) {
            a = map_.doors[static_cast<std::size_t>(i)].roomA;
            b = map_.doors[static_cast<std::size_t>(i)].roomB;
            if (a || b) {
                if (room(b).dist != -1 && (room(a).dist == -1 || room(b).dist + 1 < room(a).dist)) {
                    room(a).dist = static_cast<std::int16_t>(room(b).dist + 1);
                    done = 0;
                }
                if (room(a).dist != -1 && (room(b).dist == -1 || room(a).dist + 1 < room(b).dist)) {
                    room(b).dist = static_cast<std::int16_t>(room(a).dist + 1);
                    done = 0;
                }
            }
        }
    } while (!done);
    room(0).dist = 999;
}

int Tac::find_door(int a, int b, int* px, int* py) {
    for (int i = 0; i < map_.nDoors; i++) {
        const TacDoor& d = map_.doors[static_cast<std::size_t>(i)];
        if (d.roomA == a && d.roomB == b) {
            *px = d.x;
            *py = d.y;
            return 1;
        }
        if (d.roomA == b && d.roomB == a) {
            *px = d.x + door_dx[d.dir];
            *py = d.y + door_dy[d.dir];
            return 1;
        }
    }
    return 0;
}

int Tac::guard_plan_path(int g, int rm, int tx, int ty, int sx, int sy) {
    int tailv, head_, dn, foundw, cxy, cyz, in, nxs, nys1, obj1;
    int okk[10];
    std::int8_t vw;

    std::memset(guard_paths.data() + g * 40 * 32, 0, 0x500);
    tailv = 0;
    head_ = 0;
    queue_x[0] = static_cast<std::uint8_t>(sx);
    queue_y[static_cast<std::size_t>(tailv++)] = static_cast<std::uint8_t>(sy);
    obj1 = building(sx, sy) & 0x3f;
    if (obj1 != 0x3f && (objFlags(obj1) & 1))
        gpath(g, sx, sy) = 0;
    else
        gpath(g, sx, sy) = 1;
    foundw = 0;
    do {
        cxy = queue_x[static_cast<std::size_t>(head_)];
        cyz = queue_y[static_cast<std::size_t>(head_)];
        dn = gpath(g, cxy, cyz);
        head_++;
        head_ = static_cast<std::uint8_t>(head_);
        for (in = 1; in <= 8; in++) {
            nxs = dir_dx[in] + cxy;
            nys1 = dir_dy[in] + cyz;
            okk[in] = 1;
            if (room_map(nxs, nys1) != rm) okk[in] = 0;
            obj1 = building(nxs, nys1) & 0x3f;
            if (obj1 != 0x3f && (objFlags(obj1) & 1)) okk[in] = 0;
            if (!dn && !(in & 1)) okk[in] = 0;
        }
        okk[0] = okk[8];
        okk[9] = okk[1];
        for (in = 1; in <= 8; in++) {
            nxs = dir_dx[in] + cxy;
            nys1 = dir_dy[in] + cyz;
            if (!okk[in]) continue;
            if (!(in & 1)) {
                if (!okk[in - 1]) continue;
                if (!okk[in + 1]) continue;
            }
            if (!gpath(g, nxs, nys1)) {
                gpath(g, nxs, nys1) = static_cast<std::int8_t>(dn + 1);
                queue_x[static_cast<std::size_t>(tailv)] = static_cast<std::uint8_t>(nxs);
                queue_y[static_cast<std::size_t>(tailv)] = static_cast<std::uint8_t>(nys1);
                tailv++;
                tailv = static_cast<std::uint8_t>(tailv);
                if (nxs == tx && nys1 == ty) foundw = 1;
            }
        }
    } while (!foundw && head_ != tailv);
    if (!gpath(g, tx, ty))
        for (in = 0; in < 4; in++) {
            nxs = door_dx[in] + tx;
            nys1 = door_dy[in] + ty;
            if ((vw = gpath(g, nxs, nys1)) != 0) gpath(g, tx, ty) = vw;
        }
    return foundw;
}

void Tac::spawn_guard(int n) {
    int triesy, distsz, doorn, mindistk;
    Guard& g = guards[static_cast<std::size_t>(n)];

    if ((on_alert ? 5 : 3) + difficulty * 2 < n) return;
    if (!guards_left) return;
    mindistk = (4 - difficulty) * 75;
    triesy = 0;
    if (tac_mode == 2) {
        g.x = -1;
        do {
            doorn = random(map_.nDoors);
            const TacDoor& d = map_.doors[static_cast<std::size_t>(doorn)];
            if (!d.roomA && d.roomB && d.roomB != start_room) {
                g.room = d.roomB;
                g.x = d.x + door_dx[d.dir];
                g.y = d.y + door_dy[d.dir];
                g.dir = d.dir * 2 + 1;
            }
            if (!d.roomB && d.roomA && d.roomA != start_room) {
                g.room = d.roomA;
                g.x = d.x;
                g.y = d.y;
                g.dir = ((d.dir + 2) & 3) * 2 + 1;
            }
        } while (g.x == -1 && ++triesy < 999);
        g.x = g.x * 16 + 8;
        g.y = g.y * 16 + 8;
    } else if (!on_alert || tac_mode) {
        do {
            g.x = random(40) * 16 + 8;
            g.y = random(32) * 16 + 8;
            g.room = room_map(g.x / 16, g.y / 16);
            distsz = std::abs(g.x - max_x) + std::abs(g.y - max_y);
        } while (!(g.room && (g.room != cur_room || tac_mode == 3) && building(g.x / 16, g.y / 16) == -1 &&
                   (g.room == start_room || g.room == post_room || !frame_count || start_room <= 0) &&
                   distsz >= mindistk--) &&
                 ++triesy < 999);
    } else {
        if (frame_count > 16 && cur_room == start_room) return;
        do {
            g.x = random(40) * 16 + 8;
            g.y = random(32) * 16 + 8;
            g.room = room_map(g.x / 16, g.y / 16);
        } while ((g.room != start_room || building(g.x / 16, g.y / 16) != -1) && ++triesy < 999);
    }
    if (triesy < 999) {
        guards_left--;
        g.flags = 0x80;
        g.tx = -1;
        g.anim = 0;
        g.dir = 1;
        g.firing = 0;
        g.wait = 0;
        g.mode = 0;
        g.spotted = g.heard = g.stun = 0;
    }
}

int Tac::guard_sees_max(int n) {
    int diffw, dxs2, dys2;
    const Guard& g = guards[static_cast<std::size_t>(n)];

    guard_saw_max = 0;
    if (g.room != cur_room) return 0;
    guard_saw_max = guard_can_see_max(n);
    dxs2 = max_x - g.x;
    dys2 = max_y - g.y;
    diffw = g.dir - direction8_coarse(dxs2, dys2);
    if ((!alarm_timer || g.room != goal_room) && std::abs(diffw) > 2 && std::abs(diffw) < 6) return 0;
    return guard_saw_max;
}

int Tac::guard_can_see_max(int n) {
    const Guard& g = guards[static_cast<std::size_t>(n)];
    const int los = line_of_sight(g.x, g.y, max_x, max_y);
    if (!los) return 0;
    if (los == 2) return 1;
    if (!(g.flags & 4) && (!sneaking || fire_timer)) return 1;
    if ((g.flags & 4) && sneaking && !fire_timer) return 0;
    if (sneaking && !fire_timer) return see_bits & 2;
    return see_bits & 1;
}

int Tac::line_of_sight(int x1, int y1, int x2, int y2) {
    int nyy, diagsy, cxw, cyx, pxsz, pysz, sxsy, sysy, dxsz, dysz, res1, nxv, obj2, yxsz, xyn;
    // 16-bit products, as the original's ints
    auto mul = [](int a, int b) { return static_cast<int>(static_cast<std::int16_t>(a * b)); };

    dxsz = x2 - x1;
    dysz = y2 - y1;
    sxsy = sysy = 1;
    res1 = 2;
    see_bits = 3;
    if (dxsz < 0) {
        dxsz = -dxsz;
        x1 = 639 - x1;
        x2 = 639 - x2;
        sxsy = -1;
    }
    if (dysz < 0) {
        dysz = -dysz;
        y1 = 511 - y1;
        y2 = 511 - y2;
        sysy = -1;
    }
    pxsz = x1;
    pysz = y1;
    diagsy = x2 - x1 + y2 - y1;
    while (dxsz > 100 || dysz > 100) {
        dxsz /= 2;
        dysz /= 2;
    }
    for (;;) {
        nxv = (pxsz & ~15) + 16;
        if (dxsz)
            xyn = mul(nxv - x1, dysz) / dxsz + y1;
        else {
            nxv = 999;
            xyn = 999;
        }
        nyy = (pysz & ~15) + 16;
        if (dysz)
            yxsz = mul(nyy - y1, dxsz) / dysz + x1;
        else {
            yxsz = 999;
            nyy = 999;
        }
        if (nxv + xyn < nyy + yxsz) {
            pxsz = nxv;
            pysz = xyn;
        } else {
            pxsz = yxsz;
            pysz = nyy;
        }
        if (pxsz > x2 || pysz > y2) return res1;
        cxw = pxsz >> 4;
        cyx = pysz >> 4;
        if (sxsy < 0) cxw = 39 - cxw;
        if (sysy < 0) cyx = 31 - cyx;
        obj2 = building(cxw, cyx) & 0x3f;
        if (obj2 != 0x3f) {
            if (objFlags(obj2) & 0x20) return 0;
            if (objFlags(obj2) & 1) {
                res1 = 1;
                if (pxsz - x1 + pysz - y1 < diagsy / 2 + 4) see_bits &= ~1;
                if (pxsz - x1 + pysz - y1 > diagsy / 2 - 4) see_bits &= ~2;
            }
        }
    }
}

int Tac::guard_reserve_check(int n) {
    if (tac_mode == 2) return 1;
    if ((on_alert ? 5 : 3) + difficulty * 2 < n && !guards[static_cast<std::size_t>(n)].stun) {
        guards_left++;
        return -1;
    }
    if (alarm_timer && goal_room > 0) {
        if (n < 3) return 0;
        return 1;
    }
    if (alert_level / 25 + 3 > n) return 0;
    if (alert_level / 25 + 5 > n) return 1;
    if (is_night && !on_alert) return 2;
    return 0;
}

int Tac::direction8(int dx, int dy) {
    const int steep = std::abs(dx) > std::abs(dy);
    if (dx > 0) {
        if (dy > 0) return steep ? 3 : 4;
        return steep ? 2 : 1;
    }
    if (dy > 0) return steep ? 6 : 5;
    return steep ? 7 : 8;
}

int Tac::direction8_coarse(int dx, int dy) {
    const int horiz = std::abs(dy) * 2 < std::abs(dx);
    const int vert = std::abs(dx) * 2 < std::abs(dy);
    if (dx > 0) {
        if (dy > 0) {
            if (horiz) return 3;
            if (vert) return 5;
            return 4;
        }
        if (horiz) return 3;
        if (vert) return 1;
        return 2;
    }
    if (dy > 0) {
        if (horiz) return 7;
        if (vert) return 5;
        return 6;
    }
    if (horiz) return 7;
    if (vert) return 1;
    return 8;
}

Task<void> Tac::equipment_screen(int interactive) {
    int iy, keyz, prevw, leftz, sel2;
    auto R = [](int k) -> const Rect& { return equip_rects[k]; };

    {
        const Picture pic = decodePicture(data_.read("equip2.pic"));
        gfx_.page[3].blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
    }
    for (iy = 0; iy < 15; iy++) {
        const Rect& r = equip_pic_rects[iy];
        equip_images[static_cast<std::size_t>(iy)] = gfx_.grab(3, r.x0, r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1);
    }
    for (iy = 0; iy < 13; iy++) panel_icons[static_cast<std::size_t>(iy)].x += 6;
    ammo = 6;
    clips = 3;
    room(0).flags |= 2;
    draw_room(180, 100);
    if (!interactive) {
        gfx_.fillRect(gfx_.gpage, 0, 0, 320, 200, 5);
        co_return;
    }
    play_sound(39);
    leftz = 5;
    equipment = 1;
    sel2 = 1;
    draw_equipment_panel();
    do {
        gfx_.recolor(gfx_.gpage, R(sel2).x0, R(sel2).y0, R(sel2).x1 - R(sel2).x0 + 1, R(sel2).y1 - R(sel2).y0 + 1, 1, 11);
        co_await wait_retrace(15);
        keyz = co_await get_key_wait();
        prevw = sel2;
        switch (keyz) {
        case 2: case 0x4900:            // PgUp
            sel2 = equip_nav[sel2].right;
            [[fallthrough]];
        case 1: case 0x4800:            // Up
            sel2 = equip_nav[sel2].up;
            break;
        case 6: case 0x4f00:            // End
            sel2 = equip_nav[sel2].left;
            [[fallthrough]];
        case 5: case 0x5000:            // Down
            sel2 = equip_nav[sel2].down;
            break;
        case 8: case 0x4700:            // Home
            sel2 = equip_nav[sel2].up;
            [[fallthrough]];
        case 7: case 0x4b00:            // Left
            sel2 = equip_nav[sel2].left;
            break;
        case 4: case 0x5100:            // PgDn
            sel2 = equip_nav[sel2].down;
            [[fallthrough]];
        case 3: case 0x4d00:            // Right
            sel2 = equip_nav[sel2].right;
            break;
        case 0xd: case 0x20:
            if (equipment & (1u << sel2)) {
                equipment &= ~(1u << sel2);
                gfx_.copy(1, R(sel2).x0, R(sel2).y0, R(sel2).x1 - R(sel2).x0 + 1, R(sel2).y1 - R(sel2).y0 + 1, gfx_.gpage,
                          R(sel2).x0, R(sel2).y0);
                leftz++;
            } else if (leftz) {
                equipment |= 1u << sel2;
                gfx_.copy(3, R(sel2).x0, R(sel2).y0, R(sel2).x1 - R(sel2).x0 + 1, R(sel2).y1 - R(sel2).y0 + 1, gfx_.gpage,
                          R(sel2).x0, R(sel2).y0);
                leftz--;
            }
            equipment = ((equipment & 2) == 0) + (equipment & 0x7ffe);
            bugs_left = (equipment & 8) ? 10 : 0;
            for (iy = 0; iy < 3; iy++) grenades[static_cast<std::size_t>(iy)] = 0;
            if (equipment & 0x800) grenades = {2, 2, 2};
            for (iy = 0; iy < 3; iy++)
                if (equipment & (0x100u << iy)) grenades[static_cast<std::size_t>(iy)] = 8;
            draw_equipment_panel();
            break;
        case 0x1b:
            leftz = -1;
            break;
        default:
            break;
        }
        gfx_.copy((equipment & (1u << prevw)) ? 3 : 1, R(prevw).x0, R(prevw).y0, R(prevw).x1 - R(prevw).x0 + 1,
                  R(prevw).y1 - R(prevw).y0 + 1, gfx_.gpage, R(prevw).x0, R(prevw).y0);
    } while (leftz != -1);
    play_sound(1);
}

void Tac::draw_film_count() {
    gfx_.fillRect(gfx_.gpage, 222, 86, 15, 9, 0);
    msg_buf = std::to_string(36 - photos_taken);
    gfx_.textTransparent(msg_buf, 224, 87, 10);
}

void Tac::draw_equipment_panel() {
    int i, j;
    const int gp = gfx_.gpage;
    auto P = [&](int k) -> const Pos& { return panel_icons[static_cast<std::size_t>(k)]; };
    auto E = [&](int k) { return &equip_images[static_cast<std::size_t>(k)]; };

    gfx_.copy(1, 176, 0, 144, 100, gp, 176, 0);
    for (j = 0; grenades[static_cast<std::size_t>(cur_grenade)] <= 0 && ++j < 4;)
        if (++cur_grenade == 3) cur_grenade = 0;
    if (j < 4)
        gfx_.fillRect(gp, P(cur_grenade + 8).x - 1, P(cur_grenade + 8).y - 1,
                      grenades[static_cast<std::size_t>(cur_grenade)] * 6 + 8, 15, 9);
    for (i = 7; i >= 0; i--)
        if ((equipment & (1u << i)) && i != 3) gfx_.image(gp, P(i).x, P(i).y, E(i));
    if (equipment & 4) draw_film_count();
    for (i = 0; i < ammo; i++) gfx_.image(gp, i * 5 + P(11).x, P(11).y, E(11));
    for (i = 0; i < clips; i++) gfx_.image(gp, i * 13 + P(12).x, P(12).y, E(12));
    for (i = 0; i < bugs_left; i++) gfx_.image(gp, (i & 1) * 9 + P(3).x, i / 2 * 7 + P(3).y, E(3));
    for (i = 0; i < wounds; i++) gfx_.image(gp, i * 6 + 260, (i & 1) * 10 + i + 63, E(13));
    for (j = 0; j < 3; j++)
        for (i = 0; i < grenades[static_cast<std::size_t>(j)]; i++) gfx_.image(gp, i * 6 + P(j + 8).x, P(j + 8).y, E(j + 8));
    for (i = 0; i < static_cast<int>(password.size()); i++)
        if (password_shown[static_cast<std::size_t>(i)] != '.') {
            gfx_.fillRect(gp, 248, 88, 64, 9, 10);
            gfx_.rectOutline(248, 88, 64, 9, 2);
            gfx_.textTransparent(password_shown, 250, 89, 5);
            break;
        }
}

}  // namespace reaction
