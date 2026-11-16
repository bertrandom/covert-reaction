// chase-gen: dump CHASE's city maps and unsteered chases (game/Chase) in the
// format of tests/msc-chase/CHASEGEN.C, the original's code compiled with MSC 5.1.
#include <cstdio>
#include <string>

#include "Tool.h"
#include "game/Chase.h"

using namespace redux;

namespace {

void dumpCars(const Chase& g) {
    std::printf(" t%d", g.clock());
    for (int i = 0; i < 3; ++i) {
        const ChaseCar& c = g.car(i);
        std::printf(" %d,%d,%d,%d,%d,%d,%d,%d", c.x, c.y, c.dir, c.speed, c.heat, c.follow, c.tx, c.ty);
    }
    std::printf(" s%d e%d r%d\n", g.scene(), g.escapeTimer(), g.inRange() ? 1 : 0);
}

}  // namespace

REDUX_COMMAND("chase-gen", "chase-gen: CHASE city maps and unsteered chases for every city, difficulty and "
                           "mode 0/2 (compare with tests/msc-chase)",
              [](const GameData&, const std::vector<std::string>&) {
                  for (int city = 0; city < 16; ++city)
                      for (int d = 0; d < 4; ++d)
                          for (int m = 0; m <= 2; m += 2) {
                              ChaseSetup s;
                              s.mode = m == 2 ? 6 : 3;
                              s.cityId = city;
                              s.difficulty = d;
                              s.envFlags = 0x555;               // no driving adjustment
                              s.gameTime = (d & 1) ? 0 : 720;   // night on odd difficulties
                              s.alertLevel = city * 7;
                              s.targetBuilding = 4 + city % 12;
                              s.options = 0x20;
                              s.seed = static_cast<std::uint32_t>(city * 4 + d + m * 100);
                              Chase g(s);
                              if (m != 2) {
                                  auto o = g.rollOffers();
                                  g.setTeamCar(1, o[0].bits);
                                  g.setTeamCar(2, o[1].bits);
                              }
                              g.generateMap();
                              g.initCars();
                              std::uint32_t h = 0;
                              for (int i = 0; i < 24; ++i)
                                  for (int j = 0; j < 24; ++j) h = h * 31 + g.map(i, j);
                              std::printf("c%d d%d m%d map%08x st%d de%d hl%d b%d,%d", city, d, m, h, g.chaseStart(),
                                          g.chaseDest(), g.heatLimit(), g.car(1).bits, g.car(2).bits);
                              for (int i = 0; i < 16; ++i) std::printf(" %d,%d", g.buildingX(i), g.buildingY(i));
                              std::printf("\n");
                              while (g.clock() < 3000 && !g.chaseOver()) {
                                  g.loopStart();
                                  g.moveCars();
                                  g.endLoop();
                                  if (g.clock() % 250 == 0) dumpCars(g);
                              }
                              std::printf("end");
                              dumpCars(g);
                              std::printf("over%d out%d score%d lost%d rand%d\n", g.chaseOver(), g.outcome(), g.score(),
                                          g.lostCount(), g.rng().next());
                          }
                  return 0;
              });
