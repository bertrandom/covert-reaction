// tac-gen: dump TAC's building generation (game/TacBuilding) in the format of
// tests/msc-tac/TACGEN.C, the original's generate_building compiled with MSC 5.1.
#include <cstdint>
#include <cstdio>
#include <string>

#include "Tool.h"
#include "game/TacBuilding.h"

using namespace redux;

namespace {

void run(int mode, int city, int org, unsigned seed, int diff, int target, int tid) {
    TacBuildingParams p;
    p.mode = mode;
    p.org = org;
    p.targetPerson = target;
    p.targetId = target >= 0 ? tid : 0;
    p.guardsLeft = diff * 4 + 8;
    if (mode == 2) p.guardsLeft = diff * 2 + 4;
    if (mode == 3) p.guardsLeft = diff * 2 + 2;
    if (mode == 1) p.guardsLeft = diff * 3 + 3;
    for (int i = 0; i < 4; i++) {
        p.agents[static_cast<std::size_t>(i)].person = -1;
        p.agents[static_cast<std::size_t>(i)].name = "Agent Smithers";
        p.agents[static_cast<std::size_t>(i)].rank = i;
    }
    if (target >= 0 && mode == 2) p.agents[static_cast<std::size_t>(target & 3)].person = target;
    TacMap m;
    m.obj[60].sx = 0;
    Rng rng(tacBuildingSeed(city, org, seed));
    generateBuilding(m, rng, p);
    std::uint32_t h1 = 0, h2 = 0, h3 = 0;
    for (int x = 0; x < 40; x++)
        for (int y = 0; y < 32; y++) {
            h1 = h1 * 31 + static_cast<std::uint8_t>(m.b(x, y));
            h2 = h2 * 31 + static_cast<std::uint8_t>(m.room(x, y));
            h3 = h3 * 31 + static_cast<std::uint8_t>(m.wall(x, y));
        }
    std::printf("m%d c%d o%d s%u d%d t%d: n%d st%d po%d cr%d al%d R", mode, city, org, seed, diff, target, m.nRooms,
                m.startRoom, m.postRoom, m.curRoom, m.onAlert ? 1 : 0);
    for (int i = 0; i <= m.nRooms; i++) {
        const TacRoom& r = m.rooms[static_cast<std::size_t>(i)];
        std::printf(" %d,%d,%d,%d,%x,%d,%lx", r.x, r.y, r.w, r.h, r.flags, r.type,
                    static_cast<unsigned long>(static_cast<std::uint32_t>(r.links)));
    }
    std::printf(" D");
    for (int i = 0; i < m.nDoors; i++) {
        const TacDoor& d = m.doors[static_cast<std::size_t>(i)];
        std::printf(" %d,%d,%d,%d,%d", d.roomA, d.roomB, d.x, d.y, d.dir);
    }
    std::printf(" H %08lx %08lx %08lx o60 %d %s o61 %d r%d\n", static_cast<unsigned long>(h1),
                static_cast<unsigned long>(h2), static_cast<unsigned long>(h3), m.obj[60].sx, m.obj[60].name.c_str(),
                m.obj[61].sx, rng.next());
}

}  // namespace

REDUX_COMMAND("tac-gen", "tac-gen: TAC building generation for every city x organization (2 seeds) plus "
                         "kidnap/rescue/bomb/suspect variants (compare with tests/msc-tac)",
              [](const GameData&, const std::vector<std::string>&) {
                  for (int city = 0; city < 16; city++)
                      for (int org = 0; org < 16; org++) {
                          run(0, city, org, 0, (city + org) % 5, -1, 0);
                          run(0, city, org, 12345, (city * 3 + org) % 5, -1, 0);
                      }
                  for (int i = 0; i < 16; i++) {
                      run(1, i, (i * 3) % 16, 777, i % 5, -1, 0);
                      run(2, i, (i * 5 + 1) % 16, 4242, i % 4, i % 6, i);
                      run(3, i, (i * 7 + 2) % 16, 999, i % 4, -1, 0);
                      run(0, i, (i * 11 + 3) % 16, 31337, i % 5, 1, i);
                  }
                  return 0;
              });
