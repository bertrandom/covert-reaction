#include "game/CaseGen.h"

#include <cstring>
#include <stdexcept>

namespace redux {

namespace {

constexpr std::uint16_t bit16(int n) { return n >= 0 && n < 16 ? static_cast<std::uint16_t>(1u << n) : 0; }

// NEWCHAR.C face_defs: per leader code, sex, features, ..., icon.
constexpr int kFaceDefs[26][8] = {
    {0, 1, 2, 2, 2, 2, 3, 1}, {0, 0, 3, 3, 3, 3, 1, 2}, {0, 1, 5, 3, 4, 6, 0, 3}, {0, 0, 8, 4, 6, 1, 3, 1},
    {0, 1, 5, 5, 5, 5, 0, 0}, {0, 1, 6, 6, 6, 6, 3, 2}, {0, 1, 4, 8, 5, 2, 3, 1}, {0, 1, 7, 7, 8, 7, 1, 3},
    {0, 1, 1, 2, 3, 7, 2, 1}, {0, 1, 3, 1, 8, 5, 3, 3}, {0, 1, 2, 2, 2, 3, 3, 0}, {0, 0, 3, 3, 3, 1, 2, 1},
    {0, 1, 4, 8, 5, 3, 3, 2}, {0, 0, 8, 4, 6, 5, 3, 3}, {0, 1, 7, 7, 8, 8, 0, 0}, {0, 1, 6, 6, 6, 8, 3, 3},
    {1, 0, 2, 2, 2, 2, 3, 2}, {1, 0, 4, 4, 4, 4, 1, 3}, {1, 0, 7, 1, 8, 5, 0, 3}, {1, 0, 8, 5, 3, 7, 3, 3},
    {1, 1, 1, 8, 4, 2, 1, 2}, {1, 1, 4, 7, 2, 1, 0, 2}, {1, 1, 5, 2, 6, 3, 0, 0}, {1, 1, 5, 5, 5, 5, 3, 0},
    {1, 0, 5, 3, 1, 6, 3, 1}, {1, 0, 2, 6, 7, 8, 0, 0},
};

// FINAL.H suspect_id(n): the word at Suspect +24h (info, f25).
void setSuspectId(Suspect& s, int v) {
    s.info = static_cast<std::uint8_t>(v & 0xFF);
    s.f25 = static_cast<std::int8_t>((v >> 8) & 0xFF);
}
int suspectId(const Suspect& s) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(s.info | (static_cast<std::uint8_t>(s.f25) << 8)));
}

}  // namespace

// NEWCHAR.C face_code_for(p).
int leaderFaceCode(int p) {
    const int* fd = kFaceDefs[p];
    int code = fd[1] * 2 + fd[6] * 4 - fd[0] + 1;
    int i = 0;
    do
        code += (fd[i + 2] - 1) << (i * 3 + 4);
    while (++i < 4);
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(code));
}

// NEWCHAR.C identify_leader_faces: the draws only (the rest is the screen).
LeaderLineup drawLeaderLineup(Rng& rng) {
    LeaderLineup l;
    int used[26] = {};
    l.leader = rng.below(26);
    l.right = rng.below(15);
    used[l.leader] = 1;
    for (int where = 0; where < 15; ++where) {
        int face;
        if (where == l.right) {
            face = l.leader;
        } else {
            do
                face = rng.below(26);
            while (used[face]);
            used[face] = 1;
        }
        l.faces[static_cast<std::size_t>(where)] = face;
    }
    return l;
}

// CASE.C generate_case().
CaseResult CaseGenerator::generate() {
    auto& h = w_.hdr;
    auto& orgs = w_.orgs;
    auto& cities = w_.cities;
    const auto& mm = setup_.mm;
    auto home = [&](int o) { return static_cast<std::uint16_t>(orgs[static_cast<std::size_t>(o)].home); };
    auto code = [&](int c) { return static_cast<std::uint16_t>(cities[static_cast<std::size_t>(c)].code); };
    auto dist = Locations::cityDistance;
    auto alert = [&](int c, int o) { return loc_.orgAlertLevel(c, o); };

    r_ = {};
    h.difficulty = static_cast<std::int16_t>(setup_.difficulty);
    h.hereCity = 1;
    h.hereOrg = 1;
    h.var52 = 0;                                        // env_var_52
    h.msgDay = static_cast<std::int16_t>(setup_.crime); // msg_day = episode_crime
    const int world = setup_.world;
    w_.loadCrime(data_, setup_.crime);                  // load_crime_file
    w_.loadWorld(data_, world);                         // load_world_file
    for (auto& c : w_.clues) c.suspect = -1;
    for (int i = 0; i < 16; ++i)                        // city_clue_count, org_clue_count
        w_.cityClueCount[static_cast<std::size_t>(i)] = w_.cityClueCount[static_cast<std::size_t>(16 + i)] = 2;
    std::uint16_t doubleAgents = 0;
    for (int i = 0; i < difficulty(); ++i) doubleAgents |= bit16(rng_.below(16));
    doubleAgents &= static_cast<std::uint16_t>(~2u);
    h.doubleAgents = static_cast<std::int16_t>(doubleAgents);

    int org1 = 0, city3 = 0, attempts = 0;
    for (int outer = 0;; ++outer) {
        // The original loops forever if a world can't satisfy the constraints.
        if (outer > 100000) throw std::runtime_error("generate_case: no case after 100000 tries");
        r_.retries = outer;
        for (auto& p : w_.persons) {
            p.org = -1;
            p.id = 0;
            p.known = 0;
            p.fate = 0;
        }
        h.numPersons = 64;
        h.gameTime = 99;
        h.var38 = -1;                                   // env_var_38
        attempts = 0;
        // (the original counts the plot steps into nmsg here; the count is unused)
        int d, d2;
        do {
            org1 = rng_.below(15) + 1;
            d = dist(home(1), home(org1));
        } while ((d > 10 || org1 == 1) && ++attempts < 999);
        int org2, org3, city1, city2;
        do {
            org2 = rng_.below(15) + 1;
            d = dist(home(1), home(org2));
            d2 = dist(home(mm.org), home(org2));
        } while ((d < 8 || d2 > 8 || (home(org2) & 0xF) <= 4) && ++attempts < 999);
        h.org2 = static_cast<std::int16_t>(org2);
        do {
            org3 = rng_.below(15) + 1;
            d = dist(home(org2), home(org3));
        } while ((d > 8 || (home(org3) & 0xF) <= 4) && ++attempts < 999);
        h.org3 = static_cast<std::int16_t>(org3);
        do {
            city1 = rng_.below(16);
            d = dist(home(1), code(city1));
        } while ((d < 8 || !alert(city1, org2) || mm.city == city1) && ++attempts < 999);
        h.city1 = static_cast<std::int16_t>(city1);
        do {
            city2 = rng_.below(16);
            d = dist(code(city1), code(city2));
        } while ((d > 12 || !alert(city2, org3) || mm.city == city2) && ++attempts < 999);
        h.city2 = static_cast<std::int16_t>(city2);
        do {
            city3 = rng_.below(16);
            d = dist(home(1), code(city3));
        } while ((d > 10 || !alert(city3, org1)) && ++attempts < 999);
        r_.org1 = org1;
        r_.city3 = city3;
        if (attempts >= 999) continue;
        // Headquarters in the east half (x >= 8) move by up to 2 in x and y. The
        // world isn't reloaded on a retry, so the moves add up.
        for (int j = 0; j < 16; ++j) {
            auto& o = orgs[static_cast<std::size_t>(j)];
            if ((o.home & 0xF) >= 8) {
                int i = clampTo(rng_.below(5) + (o.home & 0xF) - 2, 0, 15);
                int cnt = clampTo(rng_.below(5) + o.home / 16 - 2, 0, 15);
                o.home = static_cast<std::int16_t>((cnt << 4) + i);
            }
        }
        for (int i = 0; i < h.numSuspects; ++i) attempts += placeParticipant(i) ? 0 : 999;
        if (attempts < 999) break;
    }
    resetPeopleMarks();
    caseBriefing();
    for (auto& o : orgs) o.flags = 0;
    // Organizations the CIA knows of: everywhere the CIA reaches them (all of the
    // local contacts', org 0), else their two nearest places (the first two found
    // in city order that get nearer).
    for (int j = 0; j < 16; ++j) {
        int closest = 999, first = -1, second = -1;
        for (int i = 0; i < 16; ++i) {
            bool rch = loc_.orgCanReach(1, j, i);
            int al;
            if ((al = alert(i, j)) != 0) {
                al = dist(code(i), home(j));
                if (al < closest) {
                    closest = al;
                    second = first;
                    first = i;
                }
            }
            if (rch || !j) {
                loc_.setLocationKnown(j, i, 4);
                cities[static_cast<std::size_t>(i)].orgsKnown |= bit16(j);
                orgs[static_cast<std::size_t>(j)].flags |= 1;
                closest = -1;
            }
        }
        if (first != -1) {
            loc_.setLocationKnown(j, first, 4);
            cities[static_cast<std::size_t>(first)].orgsKnown |= bit16(j);
        }
        if (second != -1) {
            loc_.setLocationKnown(j, second, 4);
            cities[static_cast<std::size_t>(second)].orgsKnown |= bit16(j);
        }
    }
    // Each city: the nearest one or two hostile organizations, where fewer than
    // two or three are within the CIA's reach.
    for (int i = 0; i < 16; ++i) {
        int cnt = 0, closest = 999, first = -1, second = -1;
        for (int j = 4; j < 16; ++j) {
            bool rch = loc_.orgCanReach(1, j, i);
            int al;
            if ((al = alert(i, j)) != 0) {
                al = dist(code(i), home(j));
                if (al < closest) {
                    closest = al;
                    second = first;
                    first = j;
                }
            }
            if (rch) cnt++;
        }
        if (cnt < 2 && first != -1) {
            loc_.setLocationKnown(first, i, 4);
            cities[static_cast<std::size_t>(i)].orgsKnown |= bit16(first);
        }
        if (cnt < 3 && second != -1) {
            loc_.setLocationKnown(second, i, 4);
            cities[static_cast<std::size_t>(i)].orgsKnown |= bit16(second);
        }
    }
    // Red herrings: 4 per difficulty level, each a new person of a hostile
    // organization (not the MasterMind's) in a city where it is present.
    const int numSuspects = h.numSuspects;
    const int total = difficulty() * 4 + numSuspects;
    w_.suspects.resize(static_cast<std::size_t>(total));
    w_.suspectState.resize(static_cast<std::size_t>(total));
    for (int i = numSuspects; i < total; ++i) {
        int org, city;
        do {
            do
                org = rng_.below(12) + 4;
            while (org == mm.org);
            do
                city = rng_.below(16);
            while (!alert(city, org));
        } while (loc_.addLocation(org, city, 0) != -1);
        auto& s = w_.suspects[static_cast<std::size_t>(i)];
        s.person = static_cast<std::int16_t>(loc_.addLocation(org, city, 0x8000));
        std::memcpy(s.name.raw.data(), "Red Herring", 12);   // strcpy: bytes after the NUL kept
        s.importance = static_cast<std::int16_t>(rng_.below(4) + 3);
        bool ok;
        do {
            setSuspectId(s, rng_.below(0x7FFF));
            ok = true;
            for (int j = 0; j < i; ++j)
                if ((suspectId(s) & 0x7E) == (suspectId(w_.suspects[static_cast<std::size_t>(j)]) & 0x7E)) ok = false;
        } while (!ok);
        s.flags = 0;
        s.found = 0;
        auto& st = w_.suspectState[static_cast<std::size_t>(i)];
        st.time = static_cast<std::int16_t>(rng_.below(h.numPlots / 2) + 4);
        st.progress = 0;
        s.type = static_cast<std::int16_t>(rng_.below(7));
        s.rank = static_cast<std::int16_t>(rng_.below(7));
    }
    h.numSuspects = static_cast<std::int16_t>(total);
    return r_;
}

// CASE.C reset_people_marks(): every plot step due.
void CaseGenerator::resetPeopleMarks() {
    for (int i = 0; i < w_.hdr.numPlots; ++i) w_.plots[static_cast<std::size_t>(i)].state = 999;
}

// CASE.C place_participant(n): the suspect's organization and city from its role
// flags (1/80h/40h: org1/org2/org3; 20h/10h/2: city1/city2/city3; 100h: the
// MasterMind; 200h: id bit 0 cleared), else random ones; a new person there.
bool CaseGenerator::placeParticipant(int n) {
    auto& h = w_.hdr;
    auto& s = w_.suspects[static_cast<std::size_t>(n)];
    auto& st = w_.suspectState[static_cast<std::size_t>(n)];
    const auto& mm = setup_.mm;
    auto home = [&](int o) { return static_cast<std::uint16_t>(w_.orgs[static_cast<std::size_t>(o)].home); };
    auto code = [&](int c) { return static_cast<std::uint16_t>(w_.cities[static_cast<std::size_t>(c)].code); };
    auto dist = Locations::cityDistance;
    auto alert = [&](int c, int o) { return loc_.orgAlertLevel(c, o); };
    // persons[site] with site == -1 (only when tries ran out) reads the 16 bytes
    // before persons[]: "env.sve\0env.sve\0", whose fate word (0065h) is >= 0.
    auto fate = [&](int site) { return site >= 0 ? w_.persons[static_cast<std::size_t>(site)].fate : 0x65; };

    int site = -1, tries = 0, org, city, dd;
    do {
        setSuspectId(s, rng_.below(0x7FFF));
        s.found = 0;
        st.time = -1;
        st.progress = 0;
        for (;;) {
            org = -1;
            if (s.flags & 1) org = r_.org1;
            if (s.flags & 0x80) org = h.org2;
            if (s.flags & 0x40) org = h.org3;
            if (org == -1) {
                do {
                    org = rng_.below(15) + 1;
                    dd = dist(home(org), home(h.org2));
                } while ((dd > 12 || (home(org) & 0xF) <= 4 || org == 1) && ++tries < 999);
            }
            city = -1;
            if (s.flags & 0x20) city = h.city1;
            if (s.flags & 0x10) city = h.city2;
            if (s.flags & 2) city = r_.city3;
            if (city == -1) {
                do {
                    city = rng_.below(16);
                    dd = dist(code(city), code(h.city1));
                } while ((dd < 8 || !alert(city, org)) && ++tries < 999);
            }
            if (!alert(city, org) && ++tries < 999) continue;
            if (s.flags & 0x100) {
                org = mm.org;
                city = mm.city;
            } else if (org == mm.org && city == mm.city && ++tries < 999) {
                continue;
            }
            if (!difficulty() && !n) city = 1;
            site = loc_.addLocation(org, city, 0);
            if (site != -1 && w_.persons[static_cast<std::size_t>(site)].flags && ++tries < 999) continue;
            break;
        }
        if (tries < 999) {
            site = loc_.addLocation(org, city, bit16(n));
            auto& p = w_.persons[static_cast<std::size_t>(site)];
            if ((s.flags & 0x100) && p.known) p.roles |= bit16(n);
            if (p.fate < 0) p.flags &= static_cast<std::uint16_t>(~bit16(n));
        }
    } while (fate(site) < 0 && ++tries < 999);
    s.person = static_cast<std::int16_t>(site);
    // (with site == -1 the original clears bit 0 of the "v." of "env.sve", already 0)
    if ((s.flags & 0x200) && site >= 0) w_.persons[static_cast<std::size_t>(site)].id &= static_cast<std::int16_t>(~1);
    return tries < 999;
}

// CASE.C case_briefing(region): the briefing's random draws and state; the text is
// the caller's to show (region = world: Europe, Africa/Middle East, Central America).
void CaseGenerator::caseBriefing() {
    auto& h = w_.hdr;
    if (difficulty() < 2) {
        int org = w_.persons[static_cast<std::size_t>(
                                 w_.suspects[static_cast<std::size_t>(rng_.below(h.numSuspects))].person)]
                      .org;
        r_.briefingOrg = org;
        w_.orgs[static_cast<std::size_t>(org)].flags |= 1;   // (generate_case clears it right after)
    }
    if (setup_.curMonth == 0 || !(h.options & 0x80)) {
        r_.faceCheck = true;
        r_.lineup = drawLeaderLineup(rng_);
    } else {
        clearedForDuty(w_);
    }
}

}  // namespace redux
