// reaction-tool world N / crime N / save-roundtrip FILE
#include <cstdio>
#include <fstream>

#include "Tool.h"
#include "game/World.h"

using namespace reaction;

REACTION_COMMAND("world", "world N: cities and organizations of worldN.dta",
              [](const GameData& d, const std::vector<std::string>& a) {
                  World w;
                  w.loadWorld(d, a.empty() ? 0 : std::stoi(a[0]));
                  for (const auto& c : w.cities)
                      std::printf("city  %-12s %-12s code %02X size %d map %d,%d\n", c.name.str().c_str(),
                                  c.country.str().c_str(), c.code & 0xFF, c.size, c.mapX, c.mapY);
                  for (const auto& o : w.orgs)
                      std::printf("org   %-6s %-20s home %02X strength %d range %d leader %04X\n",
                                  o.abbrev.str().c_str(), o.name.str().c_str(), o.home & 0xFF, o.strength,
                                  o.range, o.flags);
                  return 0;
              });

REACTION_COMMAND("crime", "crime N: suspects, plot steps and items of crimeN.dta",
              [](const GameData& d, const std::vector<std::string>& a) {
                  World w;
                  w.loadCrime(d, a.empty() ? 0 : std::stoi(a[0]));
                  for (const auto& s : w.suspects)
                      std::printf("role  %-32s importance %d type %d rank %d flags %04X\n", s.name.str().c_str(),
                                  s.importance, s.type, s.rank, s.flags);
                  for (const auto& p : w.plots)
                      std::printf("step  %-32s by %2d msg %3d flags %04X items %04X severity %d\n",
                                  p.name.str().c_str(), p.suspect, p.msg, p.flags & 0xFFFF, p.items & 0xFFFF,
                                  p.severity);
                  for (const auto& i : w.items)
                      std::printf("item  %-16s icon %d\n", i.name.str().c_str(), i.icon);
                  return 0;
              });

REACTION_COMMAND("save-roundtrip", "save-roundtrip FILE: load a saved game and check it saves back identically",
              [](const GameData&, const std::vector<std::string>& a) {
                  if (a.empty()) return 2;
                  std::ifstream f(a[0], std::ios::binary);
                  std::vector<std::uint8_t> in((std::istreambuf_iterator<char>(f)), {});
                  auto w = World::loadSave(in);
                  auto out = w.save();
                  bool same = out.size() <= in.size() && std::equal(out.begin(), out.end(), in.begin());
                  std::printf("%zu bytes read, %zu written: %s\n", in.size(), out.size(),
                              same ? "identical" : "DIFFERENT");
                  return same ? 0 : 1;
              });
