// Helpers for the case commands (tac-case, bug-case): the test scenes' case
// and a dump of the tables a mini-game's library calls change.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "core/GameData.h"
#include "game/CaseGen.h"
#include "game/Career.h"
#include "game/NewChar.h"
#include "game/Sim.h"

namespace reaction::tool {

inline int envInt(const char* n, int d) {
    const char* v = std::getenv(n);
    return v && *v ? std::atoi(v) : d;
}

// The "city-street" scene's case (COVERT_CITY_SEED, COVERT_CITY_LEVEL,
// COVERT_GAME_SEED; COVERT_SEED the env's rand_seed, default 1).
inline void makeTestCase(GameState& g, const GameData& data) {
    g.world.hdr.randSeed = static_cast<std::int16_t>(envInt("COVERT_SEED", 1));   // (else the time: GameState)
    g.rng.seed(static_cast<std::uint32_t>(envInt("COVERT_CITY_SEED", 1234)));
    startNewCharacter(g);
    chooseGender(g, 0);
    g.career.codename.set("Test");
    chooseDifficulty(g, clampTo(envInt("COVERT_CITY_LEVEL", 1), 0, 3));
    std::array<int, 4> lvl{1, 1, 1, 1};
    finishTraining(g, lvl, 4);
    for (int k = 0; k < 8; ++k) {
        choosePlot(g, data);
        g.briefed = true;
        if (chooseEpisode(g).ok) break;
    }
    startCaseClock(g);
    g.storeCareer();
    CaseGenerator(g.world, g.rng, data, caseSetupFor(g)).generate();
    clearedForDuty(g.world);
    g.world.hdr.options = static_cast<std::int16_t>(g.world.hdr.options | 0xC);
    g.rng.seed(static_cast<std::uint32_t>(envInt("COVERT_GAME_SEED", 4321)));
    Sim sim(g.world, g.rng, data, g.career);
    sim.startNewGame();
    g.world.hdr.var50 = g.world.hdr.difficulty;
}

// Puts the player at suspect `sus`'s building (-1: the first active one alive
// after the MasterMind); returns the suspect slot.
inline int goToSuspect(World& w, int sus) {
    const int active = w.hdr.numSuspects - w.hdr.difficulty * 4;
    if (sus < 0)
        for (int i = 1; i < active; ++i)
            if (w.persons[static_cast<std::size_t>(w.suspects[static_cast<std::size_t>(i)].person)].fate >= 0) {
                sus = i;
                break;
            }
    const Person& tp = w.persons[static_cast<std::size_t>(w.suspects[static_cast<std::size_t>(sus)].person)];
    w.hdr.hereOrg = tp.org;
    w.hdr.hereCity = tp.city;
    w.hdr.var48 = 0;
    std::printf("case: %d suspects (%d active), building: suspect %d '%s' person %d, org %d (%s) city %d (%s)\n",
                w.hdr.numSuspects, active, sus, w.suspects[static_cast<std::size_t>(sus)].name.str().c_str(),
                w.suspects[static_cast<std::size_t>(sus)].person, tp.org,
                w.orgs[static_cast<std::size_t>(tp.org)].name.str().c_str(), tp.city,
                w.cities[static_cast<std::size_t>(tp.city)].name.str().c_str());
    return sus;
}

inline std::vector<std::string> dumpTables(const World& w) {
    std::vector<std::string> out;
    char buf[160];
    const auto& h = w.hdr;
    std::snprintf(buf, sizeof buf, "H ns=%d seen=%04X diff=%d skill=%04X", h.numSites, h.seenOrgs & 0xFFFF,
                  h.difficulty, h.skillLevels & 0xFFFF);
    out.push_back(buf);
    for (std::size_t i = 0; i < 64; ++i) {
        const auto& p = w.persons[i];
        if (!p.id && !p.known) continue;
        std::snprintf(buf, sizeof buf, "P%zu org=%d id=%04X city=%d known=%04X flags=%04X roles=%04X pts=%d fate=%d", i,
                      p.org, p.id & 0xFFFF, p.city, p.known, p.flags, p.roles, p.points, p.fate);
        out.push_back(buf);
    }
    for (int i = 0; i < h.numSites && i < 256; ++i) {
        const auto& s = w.sites[static_cast<std::size_t>(i)];
        std::snprintf(buf, sizeof buf, "S%d org=%d city=%d flags=%04X str=%d rooms=%04X%04X", i, s.org, s.city, s.flags,
                      s.strength, s.fa & 0xFFFF, s.f8 & 0xFFFF);
        out.push_back(buf);
    }
    for (std::size_t i = 0; i < 80; ++i) {
        const auto& c = w.clues[i];
        if (c.suspect == -1) continue;
        std::snprintf(buf, sizeof buf, "C%zu org=%d city=%d sus=%d kind=%04X text=%04X", i, c.org, c.city, c.suspect,
                      c.kind & 0xFFFF, c.text & 0xFFFF);
        out.push_back(buf);
    }
    for (int i = 0; i < h.numMsgs && i < 128; ++i) {
        const auto& m = w.msgs[static_cast<std::size_t>(i)];
        std::snprintf(buf, sizeof buf, "M%d msg=%d flags=%04X from=%d to=%d", i, m.msg, m.flags, m.sender, m.receiver);
        out.push_back(buf);
    }
    for (std::size_t i = 0; i < w.suspects.size(); ++i) {
        std::snprintf(buf, sizeof buf, "U%zu person=%d found=%04X progress=%d", i, w.suspects[i].person,
                      w.suspects[i].found, w.suspectState[i].progress);
        out.push_back(buf);
    }
    for (std::size_t i = 0; i < 16; ++i) {
        std::snprintf(buf, sizeof buf, "Y%zu orgsKnown=%04X orgFlags=%04X map=%d,%d", i, w.cities[i].orgsKnown,
                      w.orgs[i].flags, w.cities[i].mapX, w.cities[i].mapY);
        out.push_back(buf);
    }
    std::string k = "K";
    for (auto v : w.cityClueCount) k += " " + std::to_string(v);
    out.push_back(k);
    return out;
}

// The lines that changed: - before, + after.
inline void printChanges(const std::vector<std::string>& before, const std::vector<std::string>& after) {
    const std::set<std::string> a(after.begin(), after.end()), b(before.begin(), before.end());
    for (const auto& l : before)
        if (!a.count(l)) std::printf("- %s\n", l.c_str());
    for (const auto& l : after)
        if (!b.count(l)) std::printf("+ %s\n", l.c_str());
}

}  // namespace reaction::tool
