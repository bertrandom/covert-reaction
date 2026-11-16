// bug-gen: dump BUG's board generation (game/Wiretap) in the format of
// tests/msc-bug/BUGGEN.C, the original's build_board compiled with MSC 5.1.
#include <cstdio>
#include <string>

#include "Tool.h"
#include "core/Image.h"
#include "game/Wiretap.h"

using namespace redux;

REDUX_COMMAND("bug-gen", "bug-gen [SEEDS]: BUG board generation for difficulties 0-3, seeds 0..SEEDS-1 "
                         "(default 250), blank board picture (compare with tests/msc-bug)",
              [](const GameData&, const std::vector<std::string>& args) {
                  const int seeds = args.empty() ? 250 : std::stoi(args[0]);
                  const Image blank(320, 200, 0);
                  for (int d = 0; d < 4; ++d)
                      for (int s = 0; s < seeds; ++s) {
                          WiretapSetup setup;
                          setup.difficulty = d;
                          setup.electronics = 1;
                          setup.seed = static_cast<std::uint32_t>(s);
                          Wiretap w(setup);
                          w.build(blank);
                          std::printf("d%d s%d c%d sp%d ", d, s, w.crossings(), w.spare());
                          for (int x = 0; x < 7; ++x)
                              for (int y = 0; y < 5; ++y) std::printf("%02x", w.chip(x, y));
                          std::printf(" ");
                          for (int x = 0; x < 6; ++x)
                              for (int y = 0; y < 10; ++y) std::printf("%x", w.link(x, y));
                          std::printf(" ");
                          for (int i = 0; i < 10; ++i) std::printf("%d", w.signal(13, i));
                          std::uint32_t h = 0;
                          for (int x = 0; x < 40; ++x)
                              for (int y = 0; y < 25; ++y)
                                  h = h * 31 + static_cast<std::uint32_t>(w.trace(x, y) & 0xFF);
                          std::printf(" h%08x r%d\n", h, w.rng().next());
                      }
                  return 0;
              });
