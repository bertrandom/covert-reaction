// redux-tool bug-case: a wiretap during a case, headless, with show_intercepted_info's
// describe_new_info on the case's tables (game/BugCase.h). Generates the test
// scenes' case, puts the player at a suspect's building and plays BUG with a
// greedy player (each move: the swap that taps the most phones without an alarm,
// tried on copies of the board) until it is done. Prints the library's trace, the
// tables that changed, and with --shots DIR the screens the intercepts show.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "CaseDump.h"
#include "Tool.h"
#include "formats/Font.h"
#include "formats/McgaPalette.h"
#include "formats/Pic.h"
#include "game/BugCase.h"
#include "game/Wiretap.h"

using namespace redux;
using namespace redux::tool;

namespace {

// The keys that take the cursor to (c, r) and swap there.
std::vector<int> movesTo(const Wiretap& wt, int c, int r) {
    std::vector<int> k;
    for (int x = wt.cursorX(); x < c; ++x) k.push_back(bugkeys::kRight);
    for (int x = wt.cursorX(); x > c; --x) k.push_back(bugkeys::kLeft);
    for (int y = wt.cursorY(); y < r; ++y) k.push_back(bugkeys::kDown);
    for (int y = wt.cursorY(); y > r; --y) k.push_back(bugkeys::kUp);
    k.push_back(bugkeys::kEnter);
    return k;
}

int run(const GameData& data, const std::vector<std::string>& args) {
    std::string shots;
    int sus = -1;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--shots" && i + 1 < args.size()) shots = args[++i];
        else if (args[i] == "--suspect" && i + 1 < args.size()) sus = std::atoi(args[++i].c_str());
    }
    if (!shots.empty()) std::filesystem::create_directories(shots);
    GameState g;
    makeTestCase(g, data);
    World& w = g.world;
    goToSuspect(w, sus);
    const auto before = dumpTables(w);

    WiretapSetup s = WiretapSetup::fromEnv(w.hdr, 4);
    s.practice = false;
    s.seed = static_cast<std::uint32_t>(envInt("COVERT_BUG_SEED", 777));
    BugCaseHooks hooks(w, data, g.career);
    Wiretap wt(s);
    wt.intercept = [&](int amount) { return hooks.intercept(wt, amount); };
    const Image board = decodePicture(data.read("board.pic")).image;
    wt.build(board);
    std::vector<int> scripts;
    std::string keys;   // the moves (R L U D, E swap)
    auto collect = [&] {
        for (const auto& ev : wt.takeEvents())
            if (ev.kind == WiretapEvent::Kind::Intercept) {
                std::printf("intercept: amount %d, script %d\n", ev.a, ev.b);
                if (ev.b >= 0) scripts.push_back(ev.b);
            }
    };
    collect();
    for (int step = 0; step < 300 && !wt.done(); ++step) {
        int best = -999, bc = 0, br = 0;
        for (int c = 0; c < 6; ++c)
            for (int r = 0; r < 5; ++r) {
                if (wt.chip(c, r) & Wiretap::kLocked) continue;
                Wiretap t = wt;
                t.intercept = nullptr;
                for (int k : movesTo(t, c, r)) t.frame(k);
                const int score = t.alarms() ? -100 : t.tapped() * 10 - (c == wt.cursorX() && r == wt.cursorY() ? 1 : 0);
                if (score > best) best = score, bc = c, br = r;
            }
        for (int k : movesTo(wt, bc, br)) {
            keys += k == bugkeys::kRight ? "R" : k == bugkeys::kLeft ? "L" : k == bugkeys::kUp ? "U" : k == bugkeys::kDown ? "D" : "E";
            wt.frame(k);
            collect();
            if (wt.done()) break;
        }
    }
    const WiretapResult r = wt.result();
    std::printf("result: progress %d alarms %d tapped %d (env 8=%d 9=%d)\n", r.progress, r.alarms, wt.tapped(),
                r.envProgress(), r.envAlarms());
    std::printf("keys: %s\n", keys.c_str());
    for (const auto& l : hooks.trace) std::printf("lib: %s\n", l.c_str());
    printChanges(before, dumpTables(w));

    if (!shots.empty()) {
        // each intercept's screens over the board as it stands at the end: one
        // picture per wait (key or text window)
        const FontSet fonts = FontSet::load(data);
        const Palette pal = mcgaPalette(palettes::kGame);
        int n = 0;
        for (int id : scripts) {
            Image screen = board;
            int fill = 15;
            LibCanvas c{screen, fonts.font(1), hooks.faces(), fill};
            for (const LibStep& st : hooks.script(id)) {
                if (st.kind == LibStep::Kind::Draw) st.draw(c);
                if (st.kind == LibStep::Kind::Key || st.kind == LibStep::Kind::Menu) {
                    char name[64];
                    std::snprintf(name, sizeof name, "%03d-%d-%s.bmp", n++, id,
                                  st.kind == LibStep::Kind::Key ? "key" : "menu");
                    writeBmp(std::filesystem::path(shots) / name, screen, pal);
                }
            }
        }
    }
    return 0;
}

}  // namespace

REDUX_COMMAND("bug-case",
              "bug-case [--suspect N] [--shots DIR]: a wiretap on a suspect's building in the test case "
              "(COVERT_CITY_SEED, COVERT_CITY_LEVEL, COVERT_GAME_SEED, COVERT_SEED, COVERT_BUG_SEED), played by a "
              "greedy player; prints the intercepts' library trace and the tables they changed",
              run);
