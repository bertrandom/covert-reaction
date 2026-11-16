// redux-tool sim CRIME WORLD DIFFICULTY SEED DAYS [options]: generate a case and
// run GAME's world simulation day by day (src/game/Sim.h).
#include <cstdio>
#include <fstream>
#include <string>

#include "Tool.h"
#include "game/CaseGen.h"
#include "game/Career.h"
#include "game/NewChar.h"
#include "game/Sim.h"

using namespace redux;

namespace {

std::string escaped(const std::string& s) {
    std::string out;
    char buf[8];
    for (unsigned char c : s) {
        if (c == '\n') out += "\\n";
        else if (c == '\\') out += "\\\\";
        else if (c < 0x20 || c >= 0x7F) {
            std::snprintf(buf, sizeof buf, "\\x%02X", c);
            out += buf;
        } else out += static_cast<char>(c);
    }
    return out;
}

// The comparison dump (the same lines tests/msc-game/HARNESS.C prints).
void dumpRaw(const char* label, const World& w, const Sim& sim, const Career& c, const Rng& rng) {
    const auto& h = w.hdr;
    const auto& g = sim.globals();
    std::printf("D %s rng=%08X clk=%u gt=%d nm=%d ns=%d heat=%d mole=%04X jb=%d ht=%d mm=%d hc=%d ho=%d opt=%04X "
                "ch=%d cd=%d cm=%d mmk=%04X e48=%d\n",
                label, rng.state(), static_cast<unsigned>(static_cast<std::uint16_t>(h.clockBase)), h.gameTime,
                h.numMsgs, h.numSites, h.var52, h.doubleAgents & 0xFFFF, h.jailbreakPerson, g.hidingTarget,
                g.mastermind, h.hereCity, h.hereOrg, h.options & 0xFFFF, g.clockHour, g.clockDay, g.clockMinute,
                c.mm.known, h.var48);
    for (std::size_t i = 0; i < 64; ++i) {
        const auto& p = w.persons[i];
        if (!p.id && !p.known) continue;
        std::printf("P%zu %d %04X %d %04X %04X %04X %d %d\n", i, p.org, p.id & 0xFFFF, p.city, p.known, p.flags,
                    p.roles, p.points, p.fate);
    }
    for (int i = 0; i < h.numSites && i < 256; ++i) {
        const auto& s = w.sites[static_cast<std::size_t>(i)];
        std::printf("S%d %d %d %04X %d\n", i, s.org, s.city, s.flags, s.strength);
    }
    for (std::size_t i = 0; i < 80; ++i) {
        const auto& cl = w.clues[i];
        if (cl.suspect == -1) continue;
        std::printf("C%zu %d %d %d %04X %04X\n", i, cl.org, cl.city, cl.suspect, cl.kind & 0xFFFF, cl.text & 0xFFFF);
    }
    for (int i = 0; i < h.numMsgs && i < 128; ++i) {
        const auto& m = w.msgs[static_cast<std::size_t>(i)];
        std::printf("M%d %d %d %d %04X %d %d %d %d %d\n", i, m.type, m.time, m.msg, m.flags, m.level, m.sender,
                    m.senderCity, m.receiver, m.receiverCity);
    }
    std::printf("L");
    for (int i = 0; i < h.numPlots; ++i) std::printf(" %d", w.plots[static_cast<std::size_t>(i)].state);
    std::printf("\nU");
    for (std::size_t i = 0; i < w.suspects.size(); ++i)
        std::printf(" %04X/%04X/%d/%d", w.suspects[i].flags, w.suspects[i].found, w.suspectState[i].time,
                    w.suspectState[i].progress);
    std::printf("\nY");
    for (std::size_t i = 0; i < 16; ++i)
        std::printf(" %04X/%04X/%04X", w.cities[i].orgsKnown, w.cities[i].orgsVisited, w.orgs[i].flags);
    std::printf("\nI");
    for (const auto& it : w.items) std::printf(" %d", it.holder);
    std::printf("\nK");
    for (auto k : w.cityClueCount) std::printf(" %d", k);
    std::printf("\n");
}

void printEvents(Sim& sim, bool raw) {
    for (const auto& e : sim.takeEvents()) {
        using K = SimEvent::Kind;
        if (raw) {
            switch (e.kind) {
            case K::Teletype: std::printf("TT %s|%s\n", escaped(e.title).c_str(), escaped(e.text).c_str()); break;
            case K::Notice: std::printf("MN %s\n", escaped(e.text).c_str()); break;
            case K::Wait: std::printf("WK %s\n", escaped(e.text).c_str()); break;
            case K::CaseOver: std::printf("END\n"); break;
            default: break;
            }
            continue;
        }
        switch (e.kind) {
        case K::Teletype:
            std::printf("  [teletype%s%s] %s\n", e.title.empty() ? "" : ": ", e.title.c_str(), escaped(e.text).c_str());
            break;
        case K::Notice: std::printf("  [notice] %s\n", escaped(e.text).c_str()); break;
        case K::Clue:
            if (e.title.empty()) std::printf("  [clue %d] %s\n", e.index, escaped(e.text).c_str());
            break;
        case K::NewInfo: std::printf("  [new info] %s\n", escaped(e.text).c_str()); break;
        case K::Popup: std::printf("  [tip] %s\n", escaped(e.text).c_str()); break;
        case K::CaseOver: std::printf("  [case over]\n"); break;
        default: break;
        }
    }
}

void printDay(const World& w, Sim& sim) {
    const auto& h = w.hdr;
    std::printf("  plot:");
    for (int i = 0; i < h.numPlots; ++i) {
        const auto& p = w.plots[static_cast<std::size_t>(i)];
        if (p.state != 999) std::printf(" %d:%s@%d", i, p.name.str().c_str(), p.state);
    }
    std::printf("\n  messages %d, sites %d, clues", h.numMsgs, h.numSites);
    int n = 0;
    for (const auto& c : w.clues) n += c.suspect != -1;
    std::printf(" %d, heat %d\n", n, h.var52);
    (void)sim;
}

// --scan city: one scripted city action a day, the same as tests/msc-game/HARNESS.C's
// city_day (which runs the original routines): travel, the hotel lounge, the
// research office, watching a building, interrogating a participant (or turning
// him), accusing the CIA contact / being captured.
void cityDay(Sim& sim, World& w, int d) {
    auto& h = w.hdr;
    switch (d % 8) {
    case 0: {
        const int city = d * 5 % 16;
        if (city != h.hereCity) sim.travelTo(city);
        break;
    }
    case 1:
        sim.hotelLounge();
        break;
    case 2:
        sim.researchOffice();
        break;
    case 3:
        for (int o = 1; o < 16; ++o) {
            if (sim.setLocationKnown(o, h.hereCity, 0) != -1) {
                sim.watchBuilding(o, h.hereCity);   // then Esc
                break;
            }
        }
        break;
    case 4: {   // a participant whose role we know, else any known one
        int who = -1;
        for (int p = 0; p < 64 && who == -1; ++p) {
            const auto& pp = w.persons[static_cast<std::size_t>(p)];
            if (pp.id && pp.fate >= 0 && (pp.roles & 0x7FFF)) who = p;
        }
        for (int p = 0; p < 64 && who == -1; ++p) {
            const auto& pp = w.persons[static_cast<std::size_t>(p)];
            if (pp.id && pp.fate >= 0 && (pp.known & 8) && (pp.flags & 0x7FFF)) who = p;
        }
        if (who != -1) sim.interrogateSuspect(who, !(w.persons[static_cast<std::size_t>(who)].roles & 0x7FFF));
        break;
    }
    case 5:
        if (d / 8 % 2) sim.accuseDoubleAgent();
        else sim.capturedScreen(d / 16 % 2 == 1);
        break;
    case 6:   // a wiretap placed (BUG: score 40, 1 alarm)
    case 7: {   // a break-in (TAC: score 30, alert 20, an item search)
        int p = 0, i = 0;
        for (; i < 63; ++i) {   // go to a participant's building
            p = (d * 7 + i) % 63 + 1;
            const auto& pp = w.persons[static_cast<std::size_t>(p)];
            if (pp.id && pp.fate >= 0 && (pp.flags & 0x7FFF) && pp.org > 1) break;
        }
        if (i == 63) break;
        h.hereCity = w.persons[static_cast<std::size_t>(p)].city;
        const int o = h.hereOrg = w.persons[static_cast<std::size_t>(p)].org;
        if (d % 8 == 6) {
            int scorex = 0;
            sim.wiretapResult(o, h.hereCity, 40, 1, true, scorex);
        } else {
            Sim::MiniGameResult r;
            r.lastExit = 1;
            r.env8 = 30;
            r.env9 = 20;
            r.env10 = 1;
            sim.successBriefing(o, h.hereCity, r);
        }
        break;
    }
    }
}

}  // namespace

REDUX_COMMAND(
    "sim",
    "sim CRIME WORLD DIFFICULTY SEED DAYS [--raw] [--scan none|local|intl|both] [--game-seed N] [--save-init FILE]: "
    "generate a case (as case-gen, MasterMind defaults from SEED), then run GAME: the mission briefing and DAYS x "
    "advance_time(1440) (plus a CIA scan each day with --scan), printing the plot steps, messages and clues. "
    "GAME's randomize() seed is SEED & 7FFFh unless --game-seed. --raw prints the comparison dump of "
    "tests/msc-game; --save-init writes the world before GAME starts as a saved game (the harness's input)",
    [](const GameData& d, const std::vector<std::string>& args) {
        std::vector<std::string> a;
        bool raw = false;
        std::string scan = "none", saveInit;
        long gameSeed = -1;
        for (std::size_t i = 0; i < args.size(); ++i) {
            const auto& s = args[i];
            if (s == "--raw") raw = true;
            else if (s == "--scan" && i + 1 < args.size()) scan = args[++i];
            else if (s == "--game-seed" && i + 1 < args.size()) gameSeed = std::stol(args[++i], nullptr, 0);
            else if (s == "--save-init" && i + 1 < args.size()) saveInit = args[++i];
            else a.push_back(s);
        }
        if (a.size() < 5) return 2;
        CaseSetup cs;
        cs.crime = std::stoi(a[0]);
        cs.world = std::stoi(a[1]);
        cs.difficulty = std::stoi(a[2]);
        const auto seed = static_cast<std::uint32_t>(std::stoul(a[3], nullptr, 0));
        const int days = std::stoi(a[4]);
        World w;
        // FINAL's tables start as its BSS: zero (World's defaults mark free slots).
        for (auto& m : w.msgs) m = Msg{0, 0, 0, 0, 0, 0, 0, 0, 0};
        for (auto& c : w.clues) c.org = 0;
        w.loadWorld(d, cs.world);
        int mmOrg = 4 + static_cast<int>(seed % 12);
        for (int k = 0; k < 12 && (w.orgs[static_cast<std::size_t>(mmOrg)].flags & 0xFFFF) == 0xFFFF; ++k)
            mmOrg = 4 + (mmOrg - 4 + 1) % 12;
        cs.mm.org = static_cast<std::int16_t>(mmOrg);
        cs.mm.city = static_cast<std::int16_t>(seed % 16);
        const int leader = w.orgs[static_cast<std::size_t>(mmOrg)].flags & 0xFF;
        cs.mm.id = static_cast<std::int16_t>(leader < 26 ? leaderFaceCode(leader) : 0);
        cs.mm.known = 0;
        w.hdr.numMsgs = w.hdr.clockBase = w.hdr.gameTime = w.hdr.numSites = w.hdr.seenOrgs = 0;
        Rng rng(seed);
        CaseGenerator gen(w, rng, d, cs);
        CaseResult r = gen.generate();
        if (r.faceCheck) clearedForDuty(w);   // (the player passes the line-up)

        Career career;
        career.mm = cs.mm;
        career.episodeCrime = static_cast<std::int16_t>(cs.crime);
        career.worldNum = static_cast<std::int16_t>(cs.world);
        career.curMonth = 0;
        // The briefing's roles: the first plot with this crime in an episode.
        career.playerName.set("the US Ambassador");
        career.playerOrgName.set("the plans");
        for (const auto& pd : kPlotDefs) {
            bool found = false;
            for (std::size_t e = 0; e < pd.crime.size() && !found; ++e) {
                if (pd.crime[e] == cs.crime && pd.roles[e][0]) {
                    career.playerName.set(pd.roles[e][0]);
                    career.playerOrgName.set(pd.roles[e][1] ? pd.roles[e][1] : "");
                    found = true;
                }
            }
            if (found) break;
        }
        career.pack(w.playerBlock);
        w.hdr.difficulty = static_cast<std::int16_t>(cs.difficulty);

        const std::uint32_t gseed = gameSeed >= 0 ? static_cast<std::uint32_t>(gameSeed) : (seed & 0x7FFF);
        if (!saveInit.empty()) {
            auto bytes = w.save();
            std::ofstream f(saveInit, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        rng.seed(gseed);   // GAME main: randomize()
        Sim sim(w, rng, d, career);
        if (!raw) {
            std::printf("crime %d world %d difficulty %d seed %u (GAME seed %u): %d suspects, %d plot steps\n",
                        cs.crime, cs.world, cs.difficulty, seed, gseed, w.hdr.numSuspects, w.hdr.numPlots);
            std::printf("mission briefing\n");
        }
        sim.startNewGame();
        printEvents(sim, raw);
        if (raw) dumpRaw("B", w, sim, career, rng);
        else printDay(w, sim);
        for (int day = 1; day <= days && !sim.caseOver(); ++day) {
            if (!raw) std::printf("day %d (%s)\n", day, sim.formatDateTime(static_cast<std::uint16_t>(w.hdr.clockBase)).c_str());
            sim.advanceTime(1440);
            if (!sim.caseOver() && (scan == "local" || scan == "both")) sim.localScan();
            if (!sim.caseOver() && (scan == "intl" || scan == "both")) sim.internationalScan();
            if (!sim.caseOver() && scan == "city") cityDay(sim, w, day);
            printEvents(sim, raw);
            if (raw) dumpRaw(std::to_string(day).c_str(), w, sim, career, rng);
            else printDay(w, sim);
        }
        return 0;
    });
