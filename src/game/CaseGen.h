// Case generation: a new case from crimeN.dta and worldN.dta, as FINAL builds it
// (covert-decomp FINAL/CASE.C generate_case, place_participant, reset_people_marks,
// the state changes of case_briefing, and the random draws of NEWCHAR.C
// identify_leader_faces). The random() calls are made in the original's order, so
// the same Rng state gives the original's case.
#pragma once

#include <array>
#include <cstdint>

#include "core/GameData.h"
#include "game/Locations.h"
#include "game/Rng.h"
#include "game/World.h"

namespace redux {

// What the new-character and plot code (FINAL.C, NEWCHAR.C) has set up when
// FINAL's main calls generate_case.
struct CaseSetup {
    int crime = 0;          // episode_crime: plot_defs[plot].crime[episode] (case_intro_story)
    int world = 0;          // world_num: random(3) in plot_briefing
    int difficulty = 0;     // 0..3, the new-character menu; stored in world.hdr.difficulty
    Mastermind mm;          // mm_org/mm_city: random(12)+4 / random(16), mm_id: face code of
                            // the org's leader, mm_known: 0 (plot_briefing)
    int curMonth = 0;       // cur_month (career month; 0 on the first case)
    // The Rng's state on entry is the remaining input: FINAL seeds it once with
    // srand(time & 7FFFh) and draws through the title, plot and episode choice first.
    // world.hdr.options (bit 80h: cleared for duty) is read and updated too, and the
    // caller is expected to have done case_intro_story's resets (numMsgs, clockBase,
    // gameTime, numSites, seenOrgs = 0).
};

// identify_leader_faces' line-up (the copy protection): which leader to find and
// the 15 faces shown, in screen order (5 x 3).
struct LeaderLineup {
    int leader = -1;        // leader2: random(26)
    int right = -1;         // position of his face: random(15)
    std::array<int, 15> faces{};
};

struct CaseResult {
    int org1 = 0, city3 = 0;    // FINAL-only (not in env.sve); org2/3, city1/2 are in world.hdr
    int retries = 0;            // times generate_case's outer loop started over
    int briefingOrg = -1;       // case_briefing's "The X is known to be involved" (difficulty < 2)
    // case_briefing runs identify_leader_faces when cur_month == 0 or options lacks 80h;
    // the draws are made here and the player's answer is up to the caller: on a right
    // answer set options |= 80h (clearedForDuty); on a wrong one FINAL discards the
    // case for a practice mission. When no check is needed options |= 80h is done here.
    bool faceCheck = false;
    LeaderLineup lineup;
};

// face_code_for(leader): the face/id code of an organization leader (NEWCHAR.C,
// face_defs), as plot_briefing sets mm_id = face_code_for(orgs[mm_org].flags & 0xff).
int leaderFaceCode(int leader);

// identify_leader_faces' random draws.
LeaderLineup drawLeaderLineup(Rng& rng);

// Marks the player cleared after passing the face check (case_briefing's else branch).
inline void clearedForDuty(World& w) { w.hdr.options = static_cast<std::int16_t>(w.hdr.options | 0x80); }

class CaseGenerator {
public:
    CaseGenerator(World& world, Rng& rng, const GameData& data, const CaseSetup& setup)
        : w_(world), rng_(rng), data_(data), setup_(setup), loc_(world, rng, setup.mm) {}

    // generate_case.
    CaseResult generate();

private:
    bool placeParticipant(int n);   // place_participant
    void resetPeopleMarks();        // reset_people_marks
    void caseBriefing();            // case_briefing (state only)
    std::uint16_t& suspectFlags(int n) { return w_.suspects[static_cast<std::size_t>(n)].flags; }
    int difficulty() const { return w_.hdr.difficulty; }

    World& w_;
    Rng& rng_;
    const GameData& data_;
    CaseSetup setup_;
    Locations loc_;
    CaseResult r_;
};

}  // namespace redux
