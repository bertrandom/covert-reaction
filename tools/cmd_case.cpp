// reaction-tool case-gen CRIME WORLD DIFFICULTY SEED [MM_ORG [MM_CITY [CUR_MONTH]]] [--raw]
#include <cstdio>
#include <string>

#include "Tool.h"
#include "game/CaseGen.h"

using namespace reaction;

namespace {

// The raw tables generate_case writes, one line each (compared against the
// original C compiled with MSC 5.1, see the case-gen notes in CaseGen.h).
void dumpRaw(const World& w, const CaseResult& r, Rng& rng) {
    const auto& h = w.hdr;
    std::printf("hdr %d %d %d %d %d %d %d %d %d %d %d %d %d %04X %d\n", h.hereCity, h.hereOrg, h.msgDay,
                h.numPersons, h.gameTime, h.var38, h.var52, h.org2, h.org3, h.city1, h.city2, h.numSuspects,
                h.numSites, h.doubleAgents & 0xFFFF, h.options & 0xFFFF);
    std::printf("res %d %d %d %d", r.org1, r.city3, r.briefingOrg, r.faceCheck ? 1 : 0);
    if (r.faceCheck) {
        std::printf(" %d %d", r.lineup.leader, r.lineup.right);
        for (int f : r.lineup.faces) std::printf(" %d", f);
    }
    std::printf("\n");
    for (std::size_t i = 0; i < w.suspects.size(); ++i) {
        const auto& s = w.suspects[i];
        const auto& st = w.suspectState[i];
        std::printf("s%zu %d %d %02X%02X %04X %04X %d %d %d %d\n", i, s.person, s.importance,
                    static_cast<std::uint8_t>(s.f25), s.info, s.flags, s.found, s.type, s.rank, st.time,
                    st.progress);
    }
    for (std::size_t i = 0; i < w.persons.size(); ++i) {
        const auto& p = w.persons[i];
        if (!p.id) continue;
        std::printf("p%zu %d %04X %d %04X %04X %04X %d %d\n", i, p.org, p.id & 0xFFFF, p.city, p.known, p.flags,
                    p.roles, p.points, p.fate);
    }
    for (int i = 0; i < h.numSites; ++i) {
        const auto& s = w.sites[static_cast<std::size_t>(i)];
        std::printf("t%d %d %d %04X\n", i, s.org, s.city, s.flags);
    }
    for (std::size_t i = 0; i < 16; ++i)
        std::printf("c%zu %04X o %02X %04X\n", i, w.cities[i].orgsKnown, w.orgs[i].home & 0xFFFF,
                    w.orgs[i].flags);
    for (int i = 0; i < h.numPlots; ++i) std::printf("%d ", w.plots[static_cast<std::size_t>(i)].state);
    std::printf("\nrng %d\n", rng.next());
}

void dumpReadable(const World& w, const CaseSetup& cs, const CaseResult& r) {
    const auto& h = w.hdr;
    auto orgName = [&](int o) { return o >= 0 && o < 16 ? w.orgs[static_cast<std::size_t>(o)].abbrev.str() : "?"; };
    auto cityName = [&](int c) { return c >= 0 && c < 16 ? w.cities[static_cast<std::size_t>(c)].name.str() : "?"; };
    std::printf("crime %d world %d difficulty %d; MasterMind %s in %s (id %04X)%s\n", cs.crime, cs.world,
                cs.difficulty, orgName(cs.mm.org).c_str(), cityName(cs.mm.city).c_str(), cs.mm.id & 0xFFFF,
                r.retries ? (" after " + std::to_string(r.retries) + " restarts").c_str() : "");
    std::printf("main roles: org1 %s, org2 %s, org3 %s; city1 %s, city2 %s, city3 %s\n", orgName(r.org1).c_str(),
                orgName(h.org2).c_str(), orgName(h.org3).c_str(), cityName(h.city1).c_str(),
                cityName(h.city2).c_str(), cityName(r.city3).c_str());
    std::printf("double agents:");
    for (int i = 0; i < 16; ++i)
        if (h.doubleAgents & (1 << i)) std::printf(" %s", orgName(i).c_str());
    std::printf("\n");
    if (r.briefingOrg >= 0)
        std::printf("briefing: the %s is known to be involved\n",
                    w.orgs[static_cast<std::size_t>(r.briefingOrg)].name.str().c_str());
    if (r.faceCheck) {
        std::printf("face check: find leader %d at position %d among", r.lineup.leader, r.lineup.right);
        for (int f : r.lineup.faces) std::printf(" %d", f);
        std::printf("\n");
    }
    std::printf("\n  # role                             imp person  id    sex first last  org    city         "
                "flags\n");
    for (std::size_t i = 0; i < w.suspects.size(); ++i) {
        const auto& s = w.suspects[i];
        if (s.person < 0 || s.person >= 64) {
            std::printf("%3zu %-32s unplaced\n", i, s.name.str().c_str());
            continue;
        }
        const auto& p = w.persons[static_cast<std::size_t>(s.person)];
        int nat = p.org >= 0 && p.org < 16 ? w.orgs[static_cast<std::size_t>(p.org)].nationality : 0;
        std::printf("%3zu %-32s %3d %6d  %04X  %s %5d %4d  %-6s %-12s %04X\n", i, s.name.str().c_str(),
                    s.importance, s.person, p.id & 0xFFFF, (p.id & 1) ? "M" : "F", nat * 2 + ((p.id >> 1) & 15),
                    nat * 2 + ((p.id >> 5) & 15), orgName(p.org).c_str(), cityName(p.city).c_str(), s.flags);
    }
    std::printf("\nknown sites (%d):\n", h.numSites);
    for (int c = 0; c < 16; ++c) {
        std::printf("  %-12s", cityName(c).c_str());
        for (int o = 0; o < 16; ++o)
            if (w.cities[static_cast<std::size_t>(c)].orgsKnown & (1 << o)) std::printf(" %s", orgName(o).c_str());
        std::printf("\n");
    }
    std::printf("organizations known:");
    for (int o = 0; o < 16; ++o)
        if (w.orgs[static_cast<std::size_t>(o)].flags & 1) std::printf(" %s", orgName(o).c_str());
    std::printf("\n");
}

}  // namespace

REACTION_COMMAND(
    "case-gen",
    "case-gen CRIME WORLD DIFFICULTY SEED [MM_ORG [MM_CITY [CUR_MONTH]]] [--raw] (\"-\" for a default): generate a case (CASE.C "
    "generate_case) with the Rng seeded to SEED on entry; MasterMind defaults: org 4 + SEED % 12 (the next one "
    "with a leader), city SEED % 16; CUR_MONTH > 0 also sets options 80h (no face check)",
    [](const GameData& d, const std::vector<std::string>& args) {
        std::vector<std::string> a;
        bool raw = false;
        for (const auto& s : args) {
            if (s == "--raw") raw = true;
            else a.push_back(s);
        }
        if (a.size() < 4) return 2;
        CaseSetup cs;
        cs.crime = std::stoi(a[0]);
        cs.world = std::stoi(a[1]);
        cs.difficulty = std::stoi(a[2]);
        const auto seed = static_cast<std::uint32_t>(std::stoul(a[3], nullptr, 0));
        World w;
        w.loadWorld(d, cs.world);
        auto given = [&](std::size_t i) { return a.size() > i && a[i] != "-"; };
        int mmOrg = given(4) ? std::stoi(a[4]) : 4 + static_cast<int>(seed % 12);
        if (mmOrg < 0 || mmOrg > 15) return 2;
        if (!given(4))
            for (int k = 0; k < 12 && (w.orgs[static_cast<std::size_t>(mmOrg)].flags & 0xFFFF) == 0xFFFF; ++k)
                mmOrg = 4 + (mmOrg - 4 + 1) % 12;
        cs.mm.org = static_cast<std::int16_t>(mmOrg);
        cs.mm.city = static_cast<std::int16_t>(given(5) ? std::stoi(a[5]) : static_cast<int>(seed % 16));
        int leader = w.orgs[static_cast<std::size_t>(mmOrg)].flags & 0xFF;
        cs.mm.id = static_cast<std::int16_t>(leader < 26 ? leaderFaceCode(leader) : 0);
        cs.mm.known = 0;
        cs.curMonth = given(6) ? std::stoi(a[6]) : 0;
        if (cs.curMonth) w.hdr.options = 0x80;   // a returning agent, cleared on an earlier case
        // case_intro_story's resets before generate_case
        w.hdr.numMsgs = w.hdr.clockBase = w.hdr.gameTime = w.hdr.numSites = w.hdr.seenOrgs = 0;
        Rng rng(seed);
        CaseGenerator gen(w, rng, d, cs);
        CaseResult r = gen.generate();
        if (raw) dumpRaw(w, r, rng);
        else dumpReadable(w, cs, r);
        return 0;
    });
