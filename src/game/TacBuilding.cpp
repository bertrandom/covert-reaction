// generate_building and its helpers (covert-decomp src/TAC/ROOM.C), statement for
// statement: the order of the random() calls is the original's.
#include "game/TacBuilding.h"

#include <cstdlib>

namespace reaction {

const std::array<TacRoomType, 10> kTacRoomTypes = {{
    {"Office", 4, 2, 7},
    {"File Room", 6, 1, 7},
    {"Computer Room", 8, 4, 7},
    {"Cafeteria", 2, 4, 7},
    {"Lounge", 2, 2, 7},
    {"Cipher Room", 12, 1, 7},
    {"Bed Room", 6, 2, 0},
    {"Bathroom", 12, 1, 7},
    {"Kitchen", 4, 2, 0},
    {"Storage Room", 4, 4, 7},
}};

std::array<TacObjType, 62> tacDefaultObjTypes() {
    return {{
        {"Wall", 0, 0, 0x0, 0x0},
        {"Wall", 16, 0, 0x0, 0x0},
        {"Wall", 32, 0, 0x0, 0x0},
        {"Wall", 48, 0, 0x0, 0x0},
        {"Wall", 64, 0, 0x0, 0x0},
        {"Wall", 80, 0, 0x0, 0x0},
        {"Wall", 96, 0, 0x0, 0x0},
        {"Wall", 112, 0, 0x0, 0x0},
        {"Door", 64, 16, 0x12, 0x0},
        {"Door", 80, 16, 0x12, 0x0},
        {"Door", 96, 16, 0x12, 0x0},
        {"Door", 112, 16, 0x12, 0x0},
        {"Chair", 128, 0, 0x1, 0x219},
        {"Chair", 144, 0, 0x1, 0x218},
        {"Chair", 160, 0, 0x1, 0x219},
        {"Chair", 176, 0, 0x1, 0x218},
        {"Terminal", 192, 0, 0x201, 0x25},
        {"Typewriter", 208, 0, 0x5, 0x3},
        {"Terminal", 224, 0, 0x201, 0x21},
        {"Typewriter", 240, 0, 0x5, 0x3},
        {"Chair", 256, 0, 0x1, 0x445},
        {"Plant", 272, 0, 0x5, 0x451},
        {"Chair", 288, 0, 0x1, 0x445},
        {"Plant", 304, 0, 0x5, 0x451},
        {"Files", 128, 16, 0x12b, 0x207},
        {"Files", 144, 16, 0x12b, 0x227},
        {"Files", 160, 16, 0x12b, 0x207},
        {"Files", 176, 16, 0x12b, 0x227},
        {"Floor Safe", 192, 16, 0x12b, 0x66},
        {"Wall Safe", 208, 16, 0x10a, 0x23},
        {"Picture", 224, 16, 0x106, 0x459},
        {"Wall Safe", 240, 16, 0x10a, 0x463},
        {"Table", 64, 48, 0x81, 0x318},
        {"Table", 80, 48, 0x81, 0x318},
        {"Desk", 32, 16, 0x4b, 0x1},
        {"Desk", 48, 16, 0x4b, 0x1},
        {"Desk", 0, 16, 0x4b, 0x1},
        {"Desk", 16, 16, 0x4b, 0x1},
        {"Couch", 96, 48, 0x45, 0x611},
        {"Couch", 112, 48, 0x45, 0x611},
        {"Toilet", 0, 64, 0x10b, 0x80},
        {"Toilet", 16, 64, 0x10b, 0x80},
        {"Sink", 96, 64, 0x10b, 0x80},
        {"Sink", 112, 64, 0x10b, 0x80},
        {"Bathtub", 128, 64, 0x41, 0x80},
        {"Bathtub", 144, 64, 0x41, 0x80},
        {"Bed", 192, 48, 0x41, 0x40},
        {"Bed", 208, 48, 0x41, 0x40},
        {"Range", 160, 64, 0x101, 0x100},
        {"Range", 176, 64, 0x101, 0x100},
        {"Refrigerator", 192, 80, 0x121, 0x100},
        {"Refrigerator", 208, 80, 0x121, 0x100},
        {"Stereo", 224, 48, 0x101, 0x650},
        {"Television", 240, 48, 0x101, 0x440},
        {"Lamp", 256, 48, 0x101, 0x608},
        {"Television", 272, 48, 0x101, 0x400},
        {"Computer", 0, 48, 0x125, 0x24},
        {"Drawers", 16, 96, 0x10b, 0x440},
        {"Computer", 32, 48, 0x125, 0x24},
        {"Drawers", 48, 96, 0x10b, 0x440},
        {"MacGuffin  ", 0, 96, 0x3, 0x0},
        {"Local Agent", 256, 16, 0x3, 0x0},
    }};
}

int TacMap::wallAt(int x, int y) const {
    const int i = x * kH + y;
    if (i >= 0 && i < kW * kH) return wallMap[static_cast<std::size_t>(i)];
    // DGROUP around wall_map (CF8Ah): ... CF88h guards_left | wall_map | D48Ah cur_org ...
    // (CF4Ah-CF87h hold nothing TAC uses; above D48Ch is padding and the bottom of
    // the stack, which is taken as zero.)
    auto sbyte = [](int v) { return static_cast<int>(static_cast<std::int8_t>(v & 0xFF)); };
    if (i == -2) return sbyte(guardsLeftForOob);
    if (i == -1) return sbyte(guardsLeftForOob >> 8);
    if (i == kW * kH) return sbyte(orgForOob);
    if (i == kW * kH + 1) return sbyte(orgForOob >> 8);
    return 0;
}

namespace {

// `1 << n` as MSC computes it for an int n, widened to long the way C does when
// it meets a long: a 16-bit shift (0 from 16 on), sign-extended (bit 15 sets the
// upper half too).
std::int32_t intBit(int n) {
    const auto v = static_cast<std::int16_t>(n < 16 ? static_cast<std::uint16_t>(1u << n) : 0u);
    return static_cast<std::int32_t>(v);
}

struct Gen {
    TacMap& m;
    Rng& rng;
    const TacBuildingParams& p;

    int random(int n) { return rng.below(n); }

    void setWall(int x, int y, int dir) {
        m.wall(x, y) |= static_cast<std::int8_t>(1 << dir);
        if (dir == 0 && y > 0) m.wall(x, y - 1) |= 4;
        if (dir == 1 && x < 39) m.wall(x + 1, y) |= 8;
        if (dir == 2 && y < 31) m.wall(x, y + 1) |= 1;
        if (dir == 3 && x > 0) m.wall(x - 1, y) |= 2;
        m.b(x, y) = kWallCells[m.wall(x, y) & 15];
        const int nx = x + kDoorDx[dir], ny = y + kDoorDy[dir];
        m.b(nx, ny) = kWallCells[m.wall(nx, ny) & 15];
    }

    void initGrid() {
        int w, h;
        if (p.mode == 3) {
            w = 12;
            h = 13;
        } else {
            w = 40;
            h = 32;
        }
        for (int x = 0; x < 40; x++)
            for (int y = 0; y < 32; y++) {
                m.wall(x, y) = m.room(x, y) = 0;
                m.b(x, y) = -1;
            }
        for (int k = 1; k < w - 1; k++) {
            setWall(k, 1, 0);
            setWall(k, h - 2, 2);
            m.wall(k, 0) = m.wall(k, 31) = 0;
        }
        for (int k = 1; k < h - 1; k++) {
            setWall(1, k, 3);
            setWall(w - 2, k, 1);
            m.wall(0, k) = m.wall(39, k) = 0;
        }
    }

    void splitRooms() {
        for (int w = 0; w < 20; w++) {
            int t = 0, x, y, dir, ok;
            do {
                x = random(38) + 1;
                y = random(30) + 1;
                dir = -1;
                if (m.wall(x, y) == 1) dir = 2;
                if (m.wall(x, y) == 2) dir = 3;
                if (m.wall(x, y) == 4) dir = 0;
                if (m.wall(x, y) == 8) dir = 1;
                ok = 1;
                for (int i = -4; i <= 4; i++)
                    if (((dir & 1) ? 5 : 10) & m.wallAt(x + kDoorDx[dir + 1] * i, y + kDoorDy[dir + 1] * i)) ok = 0;
            } while ((dir == -1 || !ok) && ++t < 999);
            if (t >= 999) break;
            int cnt = -1;
            do {
                if (m.wall(x, y)) cnt++;
                setWall(x, y, (dir + 1) & 3);
                x += kDoorDx[dir];
                y += kDoorDy[dir];
            } while (!cnt && x >= 0 && y >= 0 && x < 40 && y < 40);
        }
    }

    int labelRooms() {
        m.nRooms = 0;
        for (int y = 1; y < 31; y++) {
            int len = 1;
            for (int x = 1; x < 39; x++) {
                if (!(m.wall(x, y) & 8)) {
                    m.room(x, y) = m.room(x - 1, y);
                    len++;
                    room(m.room(x, y)).w = static_cast<std::int8_t>(len);
                } else {
                    len = 1;
                    if (!(m.wall(x, y) & 1)) {
                        m.room(x, y) = m.room(x, y - 1);
                        room(m.room(x, y)).h++;
                    } else {
                        m.room(x, y) = static_cast<std::int8_t>(++m.nRooms);
                        room(m.nRooms).x = static_cast<std::int8_t>(x);
                        room(m.nRooms).y = static_cast<std::int8_t>(y);
                        room(m.nRooms).h = 1;
                    }
                }
            }
        }
        int ok = 1;
        for (int i = 1; i <= m.nRooms; i++)
            if (room(i).w > 10 || room(i).h > 10) ok = 0;
        return ok;
    }

    TacRoom& room(int i) {
        static TacRoom scratch;
        if (i < 0 || i >= static_cast<int>(m.rooms.size())) return scratch;
        return m.rooms[static_cast<std::size_t>(i)];
    }

    int placeDoors() {
        for (int k = 0; k < 24; k++) {
            m.roomDoors[static_cast<std::size_t>(k)] = 0;
            room(k).links = 0;
        }
        int hidden = 0, tries = 0;
        m.nDoors = 0;
        int cur = random(m.nRooms) + 1;
        int conn = 1;
        int cx = 0, y2 = 0, di = 0, dst = 0, ok2;
        do {
            do {
                switch (random(4)) {
                case 0:
                    cx = room(cur).x + room(cur).w / 2;
                    y2 = room(cur).y;
                    di = 0;
                    break;
                case 1:
                    cx = room(cur).x + room(cur).w / 2;
                    y2 = room(cur).y + room(cur).h - 1;
                    di = 2;
                    break;
                case 2:
                    y2 = room(cur).y + room(cur).h / 2;
                    cx = room(cur).x;
                    di = 3;
                    break;
                case 3:
                    y2 = room(cur).y + room(cur).h / 2;
                    cx = room(cur).x + room(cur).w - 1;
                    di = 1;
                    break;
                }
                ok2 = 1;
                dst = m.room(cx + kDoorDx[di], y2 + kDoorDy[di]);
                if (!(room(cur).links & intBit(dst))) {
                    const int mask = (di & 1) ? 5 : 10;
                    for (int k = -1; k <= 1; k++) {
                        if (mask & m.wall(cx + kDoorDx[di + 1] * k, y2 + kDoorDy[di + 1] * k)) ok2 = 0;
                        if (mask & m.wall(cx + kDoorDx[di] + kDoorDx[di + 1] * k, y2 + kDoorDy[di] + kDoorDy[di + 1] * k))
                            ok2 = 0;
                    }
                }
                if (!dst) ok2 = 0;
            } while (++tries < 999 && !ok2);
            if (!(room(cur).links & intBit(dst)) && tries < 999) {
                m.wall(cx, y2) &= static_cast<std::int8_t>(15 - (1 << di));
                m.wall(cx + kDoorDx[di], y2 + kDoorDy[di]) &= static_cast<std::int8_t>(15 - (1 << ((di + 2) & 3)));
                m.roomDoors[static_cast<std::size_t>(cur)]++;
                m.roomDoors[static_cast<std::size_t>(dst)]++;
                tries = 0;
                if (!room(dst).links) conn++;
                room(cur).links |= intBit(dst);
                room(dst).links |= intBit(cur);
                m.b(cx, y2) = static_cast<std::int8_t>(di + 8);
                m.b(cx + kDoorDx[di], y2 + kDoorDy[di]) = static_cast<std::int8_t>(((di + 2) & 3) + 8);
                TacDoor& d = m.doors[static_cast<std::size_t>(m.nDoors)];
                d.roomA = static_cast<std::int8_t>(cur);
                d.roomB = static_cast<std::int8_t>(dst);
                d.x = static_cast<std::int8_t>(cx);
                d.y = static_cast<std::int8_t>(y2);
                d.dir = static_cast<std::int8_t>(di);
                m.nDoors++;
                if (hidden && (room(dst).flags & 2)) hidden = 0;
            }
            cur = dst;
        } while ((conn < m.nRooms || hidden) && tries < 999 && m.nDoors < 96);
        if (!hidden && tries < 999 && m.nDoors < 96) return 1;
        return 0;
    }

    void removeRoom(int r) {
        room(r).flags |= 1;
        for (int x = room(r).x; x < room(r).x + room(r).w; x++)
            for (int y = room(r).y; y < room(r).y + room(r).h; y++) {
                m.wall(x, y) = m.room(x, y) = 0;
                if ((m.b(x, y) & 0xfc) != 8) m.b(x, y) = -1;
            }
        if (m.roomDoors[static_cast<std::size_t>(r)]) m.curRoom = r;
        for (int x = 0; x < m.nDoors; x++) {
            if (m.doors[static_cast<std::size_t>(x)].roomA == r) m.doors[static_cast<std::size_t>(x)].roomA = 0;
            if (m.doors[static_cast<std::size_t>(x)].roomB == r) m.doors[static_cast<std::size_t>(x)].roomB = 0;
        }
    }

    int assignRoomTypes() {
        int lost = 0, nrem = 0, big = 0, top = -999;
        for (int i = 1; i <= m.nRooms; i++) {
            const int doorsHere = m.roomDoors[static_cast<std::size_t>(i)];
            if (!doorsHere) {
                removeRoom(i);
                nrem++;
            } else {
                int outer = 0;
                if (room(i).x == 1) outer++;
                if (room(i).y == 1) outer++;
                if (room(i).x + room(i).w == 39) outer++;
                if (room(i).y + room(i).h == 31) outer++;
                const int sz = room(i).w * room(i).h;
                if (outer) {
                    if (doorsHere < 2) {
                        removeRoom(i);
                        nrem++;
                        lost = 1;
                    } else {
                        const int score = (outer - doorsHere) * 50 + sz;
                        if (score > top) {
                            top = score;
                            m.postRoom = i;
                        }
                    }
                } else if (sz > big) {
                    big = sz;
                    m.startRoom = i;
                }
            }
        }
        if (!lost) {
            int i = 0;
            do {
                i++;
                if (i != m.postRoom && i != m.startRoom) {
                    removeRoom(i);
                    nrem++;
                }
            } while (!m.roomDoors[static_cast<std::size_t>(i)] || i == m.postRoom || i == m.startRoom);
        }
        return nrem;
    }

    void placeObjects(int r) {
        if (room(r).flags & 1) return;
        const int size = room(r).w * room(r).h;
        unsigned f = 1;
        if (size > 40) f = 2;
        if (size > 72) f = 4;
        int kind = -1;
        do {
            for (int i = 0; i < 10; i++)
                if ((f & kTacRoomTypes[static_cast<std::size_t>(i)].sizes) &&
                    (kTacRoomTypes[static_cast<std::size_t>(i)].modes & 1u /* room_type_filter */) && random(2)) {
                    kind = i;
                    break;
                }
        } while (kind == -1);
        if (p.mode == 3) kind = 0;
        room(r).type = static_cast<std::int16_t>(kind);
        auto objFlags = [&](int o) { return m.obj[static_cast<std::size_t>(o)].flags; };
        for (int i = 0; i < size / 6; i++) {
            int t = 0, x, y, obj;
            do {
                x = room(r).x + random(room(r).w);
                y = room(r).y + random(room(r).h);
                obj = -1;
                if (m.b(x, y) == -1) {
                    int ok = 1;
                    for (int j = 0; j < 4; j++)
                        if (m.b(x + kDoorDx[j], y + kDoorDy[j]) > 4) ok = 0;
                    if (ok) {
                        const int dx = x - room(r).x, dy = y - room(r).y;
                        int face;
                        if (dx > dy)
                            face = room(r).w / 2 < dx ? 1 : 3;
                        else
                            face = room(r).h / 2 < dy ? 2 : 0;
                        do
                            obj = random(16) * 4 + face + 12;
                        while (obj >= 61);
                        if (objFlags(obj) & 0x100) obj = -1;
                    }
                } else if (m.b(x, y) < 4) {
                    do
                        obj = random(16) * 4 + (m.b(x, y) & 3) + 12;
                    while (obj >= 61);
                    if (!(objFlags(obj) & 0x100)) obj = -1;
                }
            } while ((obj == -1 || !(m.obj[static_cast<std::size_t>(obj)].rooms & (1u << kind))) && ++t < 99);
            if (t < 99) {
                m.b(x, y) = static_cast<std::int8_t>(obj);
                if (objFlags(obj) & 0x40) {
                    if (obj & 1)
                        m.b(x - 1, y) = static_cast<std::int8_t>(obj - 1);
                    else
                        m.b(x + 1, y) = static_cast<std::int8_t>(obj + 1);
                }
            }
        }
        if (r == m.startRoom && ((p.targetPerson != -1 && p.mode != 1) || p.mode == 2)) {
            const int cx = room(r).x + room(r).w / 2, cy = room(r).y + room(r).h / 2;
            for (int i = 0; i < 4; i++)
                if (p.agents[static_cast<std::size_t>(i)].person == p.targetPerson && p.targetPerson != -1) {
                    m.b(cx, cy) = 60;
                    m.obj[60].sx += p.agents[static_cast<std::size_t>(i)].rank << 4;
                    m.obj[60].name = p.agents[static_cast<std::size_t>(i)].name.substr(0, 11);
                    m.onAlert = true;
                }
            m.b(cx, cy - 1) = 61;
            if (!(p.targetId & 1)) m.obj[61].sx += 16;
        }
    }

    void run() {
        do {
            for (int i = 0; i < 24; i++) room(i).flags = 0;
            m.nRooms = 0;
            if (p.mode == 3) {
                initGrid();
                labelRooms();
                m.startRoom = m.postRoom = 1;
                break;
            }
            do {
                do {
                    initGrid();
                    if (p.mode != 3) splitRooms();
                } while (!labelRooms());
            } while (!placeDoors());
        } while (m.nRooms - assignRoomTypes() < 12);
        for (int i = 1; i <= m.nRooms; i++) placeObjects(i);
        for (int i = 0; i < 39; i++) {
            if ((m.b(i, 0) & 0xfc) != 8) m.b(i, 0) = -1;
            if ((m.b(i, 31) & 0xfc) != 8) m.b(i, 31) = -1;
        }
        for (int i = 0; i < 31; i++) {
            if ((m.b(0, i) & 0xfc) != 8) m.b(0, i) = -1;
            if ((m.b(39, i) & 0xfc) != 8) m.b(39, i) = -1;
        }
    }
};

}  // namespace

void generateBuilding(TacMap& m, Rng& rng, const TacBuildingParams& p) {
    m.guardsLeftForOob = p.guardsLeft;
    m.orgForOob = p.org;
    Gen g{m, rng, p};
    g.run();
}

}  // namespace reaction
