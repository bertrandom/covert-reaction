// TAC's buildings (covert-decomp src/TAC/ROOM.C generate_building .. set_wall and
// the tables of TAC.C): a 40x32 grid of cells with wall bits, room numbers and
// object tiles, up to 23 rooms and 96 doors.
//
// Buildings aren't stored: TAC seeds the random numbers from the city, the
// organization and the game's seed, (city + 13) * (org + 7) * 23 + (seed & 3FFFh)
// in 16-bit arithmetic, and generates by rejection sampling: walls, rooms, doors
// until the rooms are connected, room types, then the furniture. The same inputs
// give the original's building (checked against the original C compiled with
// MSC 5.1, tests/msc-tac).
//
// The original reads a few bytes outside its arrays (split_rooms looks 4 cells
// left and right of the wall it starts, past the grid's first and last columns).
// TacMap::wallAt emulates the variables around wall_map in TAC's DGROUP
// (guards_left just before it at CF88h, cur_org just after it at D48Ah), but
// those bytes never decide anything: the outer wall columns 1 and 38, always
// within the 4 cells, reject such a start on their own (a sweep of 100,000
// buildings with different bytes there gave identical results).
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "game/Rng.h"

namespace reaction {

// Kinds of room (TAC.C room_types).
struct TacRoomType {
    const char* name;
    int value;          // worth of a photo or a bug; tries a guard makes to find something to look at
    unsigned sizes;     // 1 small, 2 medium, 4 large
    unsigned modes;     // matched against room_type_filter (1)
};

// Kinds of object in a map cell (TAC.C obj_types): walls 0-7, doors 8-11, the
// furniture 12-59 in groups of four (one per facing), 60 the captured agent
// ("MacGuffin"), 61 the suspect.
struct TacObjType {
    std::string name;   // char[12]
    int sx, sy;         // sprite position on the sprite page (guys3.pic)
    unsigned flags;     // 1 solid, 2 opens, 4 bug, 8 photo, 10h door/passable, 20h blocks sight,
                        // 40h two cells wide, 80h ?, 100h against a wall, 200h terminal
    unsigned rooms;     // bit per room type it may stand in
};

extern const std::array<TacRoomType, 10> kTacRoomTypes;
std::array<TacObjType, 62> tacDefaultObjTypes();

// direction tables (TAC.C): door sides 0 up, 1 right, 2 down, 3 left (4 = 0 again)
inline constexpr int kDoorDx[5] = {0, 1, 0, -1, 0};
inline constexpr int kDoorDy[5] = {-1, 0, 1, 0, -1};
// wall_cells: building tile for each combination of wall bits
inline constexpr std::int8_t kWallCells[16] = {0, 0, 1, 4, 2, 0, 5, 0, 3, 7, 0, 0, 6, 0, 0, 0};

struct TacRoom {            // 0x0E bytes
    std::int8_t x = 0, y = 0, w = 0, h = 0;   // in map cells
    std::int32_t links = 0;  // bit per room a door leads to (the original's int shifts: see Building.cpp)
    std::uint16_t flags = 0; // 1 removed, 2 seen, 4 bugged / map visible, 8 guards alerted, 10h gas
    std::int16_t type = 0;   // index into kTacRoomTypes
    std::int16_t dist = 0;   // doors from the noise source, -1 unreached
};

struct TacDoor {            // 5 bytes
    std::int8_t roomA = 0, roomB = 0;   // room 0 = outside
    std::int8_t x = 0, y = 0;           // map cell on room A's side
    std::int8_t dir = 0;                // side of that cell (0 up, 1 right, 2 down, 3 left)
};

struct TacAgent {           // the captured agents (env "items" block, 0x12 bytes)
    std::string name;       // char[16]
    int rank = 0;
    int person = -1;        // -1: none
};

// What generate_building reads besides the random numbers.
struct TacBuildingParams {
    int mode = 0;           // tac_mode: 0 break-in, 1 kidnap, 2 rescue, 3 bomb
    int targetPerson = -1;  // the suspect inside, -1 none
    int targetId = 319;     // persons[target_person].id (for target -1 the original reads 319 from
                            // the RastPort before persons[]: an odd id, a male suspect)
    std::array<TacAgent, 4> agents{};
    int guardsLeft = 8;     // only for the out-of-bounds read (TacMap::wallAt)
    int org = 0;            // likewise
};

struct TacMap {
    static constexpr int kW = 40, kH = 32;
    // building[x][y]: object tile (3Fh type, 40h done/photographed, 80h open), -1 none
    std::array<std::int8_t, kW * kH> building{};
    std::array<std::int8_t, kW * kH> roomMap{};
    std::array<std::int8_t, kW * kH> wallMap{};
    std::array<TacRoom, 24> rooms{};
    std::array<TacDoor, 96> doors{};
    std::array<int, 24> roomDoors{};
    int nRooms = 0, nDoors = 0;
    int startRoom = 0, postRoom = 0;
    int curRoom = 0;            // remove_room sets cur_room (sic)
    bool onAlert = false;       // place_objects sets it when the captured agent is here
    std::array<TacObjType, 62> obj = tacDefaultObjTypes();

    // The original's 2-D arrays are flat x * 32 + y, so a y outside 0..31 runs
    // into the next column; these follow that (indices outside the array give 0
    // / a scratch byte).
    std::int8_t& b(int x, int y) { return cell(building, x, y); }
    std::int8_t b(int x, int y) const { return cellc(building, x, y); }
    std::int8_t& room(int x, int y) { return cell(roomMap, x, y); }
    std::int8_t room(int x, int y) const { return cellc(roomMap, x, y); }
    std::int8_t& wall(int x, int y) { return cell(wallMap, x, y); }
    // wall_map as split_rooms reads it, outside the array included.
    int wallAt(int x, int y) const;

    int guardsLeftForOob = 8, orgForOob = 0;

private:
    static std::int8_t& cell(std::array<std::int8_t, kW * kH>& a, int x, int y) {
        const int i = x * kH + y;
        static std::int8_t scratch;
        if (i < 0 || i >= kW * kH) { scratch = 0; return scratch; }
        return a[static_cast<std::size_t>(i)];
    }
    static std::int8_t cellc(const std::array<std::int8_t, kW * kH>& a, int x, int y) {
        const int i = x * kH + y;
        return i < 0 || i >= kW * kH ? 0 : a[static_cast<std::size_t>(i)];
    }
};

// generate_building (with the random numbers already seeded; the final
// randomize() is left to the caller).
void generateBuilding(TacMap& m, Rng& rng, const TacBuildingParams& p);

// The seed TAC gives srand before generating.
inline std::uint32_t tacBuildingSeed(int city, int org, unsigned gameSeed) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(city + 13) * static_cast<unsigned>(org + 7) * 23u +
                                      (gameSeed & 0x3FFF));
}

}  // namespace reaction
