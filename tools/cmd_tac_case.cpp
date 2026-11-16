// reaction-tool tac-case: a break-in during a case, headless, with GAMELIB's routines
// on the case's tables (game/TacCase.h). Generates the test scenes' case (the
// "city-street" scene: COVERT_CITY_SEED, COVERT_CITY_LEVEL, COVERT_GAME_SEED),
// puts the player at a suspect's building and runs TAC with a scripted visit:
// Max is placed next to objects (debugPlaceMax) and photographs them (F2),
// opens safes and files first, optionally uses a terminal (the password, then a
// search word), examines the suspect (arrest: Yes) and leaves. It prints the
// library's trace and the tables that changed (- before, + after).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>

#include "CaseDump.h"
#include "Tool.h"
#include "formats/Font.h"
#include "game/CaseGen.h"
#include "game/Career.h"
#include "game/NewChar.h"
#include "game/Sim.h"
#include "game/Tac.h"
#include "game/TacCase.h"

using namespace reaction;
using namespace reaction::tool;

namespace {

class Driver {
public:
    Driver(Tac& t, std::string shots) : t_(t), shots_(std::move(shots)) {}

    // Runs n retraces, answering what TAC waits for: the equipment screen
    // (Esc), text windows (item 0: "Yes" to the arrest), keys (Enter).
    void pump(int n) {
        for (int i = 0; i < n && !t_.finished(); ++i) {
            if (t_.waiting() == Tac::Wait::Menu) {
                std::string s = t_.menuRequest().text;
                std::replace(s.begin(), s.end(), '\n', '|');
                std::printf("  menu: %s\n", s.c_str());
                shot("menu");
                t_.menuDone(0);
                continue;
            }
            if (t_.waiting() == Tac::Wait::Key && t_.keyBufferEmpty()) {
                if (equip_.empty()) shot("key");
                // the equipment screen first: Uzi off; camera, bugs, safecracking kit on; Esc
                if (!equip_.empty()) {
                    t_.pushKey(equip_.front());
                    equip_.erase(equip_.begin());
                } else
                    t_.pushKey(13);
            }
            t_.tick();
        }
    }
    void key(int k, int after = 24) {
        t_.pushKey(k);
        pump(after);
        if (k >= 0x100) shot("after-key");
    }
    void type(const std::string& s) {
        for (char c : s) key(static_cast<unsigned char>(c), 4);
    }
    void shot(const char* what) {
        if (shots_.empty()) return;
        char name[64];
        std::snprintf(name, sizeof name, "%03d-%s.bmp", n_++, what);
        writeBmp(std::filesystem::path(shots_) / name, t_.screen(), t_.palette());
    }
    // Max in the cell next to (x, y) inside its room, facing it.
    bool goTo(int x, int y) {
        static constexpr int dx[4] = {0, 1, 0, -1}, dy[4] = {1, 0, -1, 0};
        static constexpr int face[4] = {1, 7, 5, 3};   // looking back at (x, y)
        const TacMap& m = t_.map();
        for (int d = 0; d < 4; ++d) {
            const int cx = x + dx[d], cy = y + dy[d];
            if (cx < 0 || cy < 0 || cx >= 40 || cy >= 32) continue;
            if (m.room(cx, cy) != m.room(x, y) || m.b(cx, cy) != -1) continue;
            t_.debugPlaceMax(cx, cy, face[d]);
            pump(20);
            t_.debugPlaceMax(cx, cy, face[d]);   // (entering a room moves Max back a step)
            pump(20);
            return true;
        }
        return false;
    }

private:
    Tac& t_;
    std::string shots_;
    int n_ = 0;
    std::vector<int> equip_{13, 0x5000, 13, 0x5000, 13, 0x4d00, 0x5000, 13, 27};
};

int run(const GameData& data, const std::vector<std::string>& args) {
    std::string shots, search;
    int suspectSlot = -1, photos = 12;
    bool terminal = false, arrest = true, map = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--shots" && i + 1 < args.size()) shots = args[++i];
        else if (args[i] == "--suspect" && i + 1 < args.size()) suspectSlot = std::atoi(args[++i].c_str());
        else if (args[i] == "--photos" && i + 1 < args.size()) photos = std::atoi(args[++i].c_str());
        else if (args[i] == "--search" && i + 1 < args.size()) {
            terminal = true;
            search = args[++i];
        } else if (args[i] == "--no-arrest") arrest = false;
        else if (args[i] == "--map") map = true;
    }
    if (!shots.empty()) std::filesystem::create_directories(shots);

    GameState g;
    makeTestCase(g, data);
    World& w = g.world;
    goToSuspect(w, suspectSlot);
    const auto before = dumpTables(w);

    const FontSet fonts = FontSet::load(data);
    auto hooks = std::make_shared<TacCaseHooks>(w, data, g.career);
    TacSetup setup = TacCaseHooks::setupFrom(w, 1, -1, static_cast<std::uint32_t>(envInt("COVERT_TAC_RUNSEED", 777)));
    Tac tac(data, fonts, setup, hooks);
    tac.start();
    Driver d(tac, shots);
    d.pump(200);   // the equipment screen (Esc) and the way in
    std::printf("target person %d, room %d, Max at %d,%d (cell %d,%d)\n", tac.targetPerson(), tac.curRoom(), tac.maxX(),
                tac.maxY(), tac.maxX() / 16, tac.maxY() / 16);
    if (map) {
        // the building, y down: # walls, . floor (room 0 blank), M Max, S the suspect, F files, D desk,
        // W wall safe, V floor safe, T terminal, o other objects
        const TacMap& mm = tac.map();
        for (int y = 0; y < 32; ++y) {
            std::string row;
            for (int x = 0; x < 40; ++x) {
                const int b = mm.b(x, y), t = b & 0x3F;
                char c = mm.room(x, y) == 0 ? ' ' : '.';
                if (b != -1) {
                    c = t < 8 ? '#' : t < 12 ? '+' : 'o';
                    if (t >= 24 && t <= 27) c = 'F';
                    if (t >= 34 && t <= 37) c = 'D';
                    if (t == 29 || t == 31) c = 'W';
                    if (t == 28) c = 'V';
                    if (mm.obj[static_cast<std::size_t>(t)].flags & 0x200) c = 'T';
                    if (t == 61) c = 'S';
                }
                if (x == tac.maxX() / 16 && y == tac.maxY() / 16) c = 'M';
                row += c;
            }
            std::printf("%2d %s\n", y, row.c_str());
        }
        return 0;
    }

    const TacMap& m = tac.map();
    int target_x = -1, target_y = -1, term_x = -1, term_y = -1, taken = 0;
    for (int x = 0; x < 40 && !tac.finished(); ++x)
        for (int y = 0; y < 32 && !tac.finished(); ++y) {
            const int b = m.b(x, y);
            if (b == -1) continue;
            const unsigned f = m.obj[static_cast<std::size_t>(b & 0x3F)].flags;
            if ((b & 0x3F) == 61) {
                target_x = x;
                target_y = y;
            }
            if ((f & 0x200) && term_x < 0) {
                term_x = x;
                term_y = y;
            }
            if (!(f & 8) || (b & 0x40) || taken >= photos || m.room(x, y) <= 0) continue;
            if (!d.goTo(x, y)) continue;
            std::printf("photo: %s at %d,%d (room %d)\n", m.obj[static_cast<std::size_t>(b & 0x3F)].name.c_str(), x, y,
                        tac.curRoom());
            if ((f & 2) && !(m.b(x, y) & 0x80)) d.key(13);   // open it
            d.key(0x3C00, 30);                               // F2
            ++taken;
        }
    if (terminal && term_x >= 0 && !tac.finished() && d.goTo(term_x, term_y)) {
        std::printf("terminal at %d,%d: password %s, search '%s'\n", term_x, term_y, tac.currentPassword().c_str(),
                    search.c_str());
        d.key(0x3E00, 10);   // F4: use
        d.type(tac.currentPassword());
        d.key(13, 80);
        d.type(search);
        d.key(13, 120);
    }
    if (arrest && target_x >= 0 && !tac.finished() && d.goTo(target_x, target_y)) {
        std::printf("examine the suspect at %d,%d\n", target_x, target_y);
        d.key(13, 40);
    }
    // out: onto an outside cell (room 0)
    for (int x = 0; x < 40 && !tac.finished(); ++x)
        for (int y = 0; y < 32; ++y)
            if (m.room(x, y) == 0 && m.b(x, y) == -1) {
                tac.debugPlaceMax(x, y, 1);
                x = 40;
                break;
            }
    d.pump(400);
    if (!tac.finished()) std::printf("(TAC still running)\n");
    const TacResult r = tac.result();
    std::printf("result: score %d violence %d flags %d wounds %d minutes %d photos %d bugs %d rooms %08X\n", r.score,
                r.violence, r.missionFlags, r.wounds, r.minutes, r.photos, r.bugs, static_cast<unsigned>(r.roomsSeen));
    for (const auto& l : hooks->trace) std::printf("lib: %s\n", l.c_str());
    if (tac.finished()) TacCaseHooks::applyResult(w, r);
    const auto after = dumpTables(w);
    printChanges(before, after);
    return 0;
}

}  // namespace

REACTION_COMMAND("tac-case",
              "tac-case [--suspect N] [--photos N] [--search WORD] [--no-arrest] [--shots DIR]: a scripted break-in "
              "into a suspect's building in the test case (COVERT_CITY_SEED, COVERT_CITY_LEVEL, COVERT_GAME_SEED, "
              "COVERT_TAC_RUNSEED, COVERT_SEED); prints the library's trace and the tables it changed",
              run);
