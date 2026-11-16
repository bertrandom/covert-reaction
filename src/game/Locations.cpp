#include "game/Locations.h"

#include <cstdlib>

namespace reaction {

namespace {
// 1 << n as a 16-bit int (MSC small model): 0 once n reaches 16.
constexpr std::uint16_t bit16(int n) { return n >= 0 && n < 16 ? static_cast<std::uint16_t>(1u << n) : 0; }
}  // namespace

// PERSON.C city_distance(a, b): unsigned a, b.
int Locations::cityDistance(unsigned a, unsigned b) {
    int dx = std::abs(static_cast<int>(a & 15) - static_cast<int>(b & 15));
    int dy = std::abs(static_cast<int>((a & 0xF0) >> 4) - static_cast<int>((b & 0xF0) >> 4));
    if (dx > dy) return dx * 2 + dy;
    return dy * 2 + dx;
}

// PERSON.C org_alert_level(city, org).
int Locations::orgAlertLevel(int city, int org) const {
    const City& c = w_.cities[static_cast<std::size_t>(city)];
    const Org& o = w_.orgs[static_cast<std::size_t>(org)];
    int d = cityDistance(static_cast<std::uint16_t>(c.code), static_cast<std::uint16_t>(o.home));
    int level = (c.size + 4) * o.strength / (d + 1);
    if (level > 0) {
        // (the original also computes t = level * 60 / range + 10, unused)
        level = level * 6 / o.range + 1;
        if (level > 5) level = 5;
    }
    if (city == mm_.city && org == mm_.org) level = 1;
    if (level <= 0 && w_.hdr.difficulty == 0 && city == 1 && !w_.suspects.empty()) {
        // persons[suspects[0].person].org. Before the first participant is placed,
        // suspects[0].person is crimeN.dta's -1: FINAL then reads the word before
        // persons[], the "en" of a string literal "env.sve" (6E65h), which matches no
        // organization. Out-of-range slots are treated the same way.
        int p = w_.suspects[0].person;
        if (p >= 0 && p < 64 && w_.persons[static_cast<std::size_t>(p)].org == org) level = 1;
    }
    return level;
}

// PERSON.C org_can_reach(org, org2, city).
bool Locations::orgCanReach(int org, int org2, int city) const {
    const auto home = static_cast<std::uint16_t>(w_.orgs[static_cast<std::size_t>(org)].home);
    int d = cityDistance(home, static_cast<std::uint16_t>(w_.cities[static_cast<std::size_t>(city)].code)) +
            cityDistance(home, static_cast<std::uint16_t>(w_.orgs[static_cast<std::size_t>(org2)].home)) * 3 / 2;
    return orgAlertLevel(city, org2) * 4 + 16 > d;
}

// CLUE.C add_location(org, city, flags).
int Locations::addLocation(int org, int city, std::uint16_t flags) {
    auto& persons = w_.persons;
    for (int i = 0; i < w_.hdr.numPersons && i < 64; ++i) {
        auto& p = persons[static_cast<std::size_t>(i)];
        if (p.org == org && p.city == city) {
            p.flags |= flags;
            return i;
        }
    }
    int i;
    do
        i = rng_.below(64);
    while (persons[static_cast<std::size_t>(i)].id || !i);
    if (org == mm_.org && city == mm_.city) i = 0;
    if (flags) {
        auto& p = persons[static_cast<std::size_t>(i)];
        p.org = static_cast<std::int16_t>(org);
        p.city = static_cast<std::int16_t>(city);
        p.known = i ? 0 : mm_.known;
        p.flags = flags;
        p.roles = 0;
        p.points = 0;
        if (i) {
            p.id = static_cast<std::int16_t>(rng_.below(0x7FFE) + 1);
            if (!rng_.below(3)) p.id |= 1;
        } else {
            p.id = mm_.id;
        }
        p.fate = 0;
        return i;
    }
    return -1;
}

// CLUE.C set_location_known(org, city, flags).
int Locations::setLocationKnown(int org, int city, std::uint16_t flags) {
    if (flags) w_.cities[static_cast<std::size_t>(city)].orgsKnown |= bit16(org);
    auto& sites = w_.sites;
    auto& numSites = w_.hdr.numSites;
    for (int i = 0; i < numSites && i < 256; ++i) {
        auto& s = sites[static_cast<std::size_t>(i)];
        if (s.org == org && s.city == city) {
            if ((s.flags |= flags) != 0) return i;
            return -1;
        }
    }
    if (!flags) return -1;
    if (numSites >= 256) return -1;   // the original writes past sites[] (16 x 16 fit exactly)
    auto& s = sites[static_cast<std::size_t>(numSites)];
    s.org = static_cast<std::int16_t>(org);
    s.city = static_cast<std::int16_t>(city);
    s.flags = flags;
    s.strength = 0;
    s.f8 = s.fa = 0;
    ++numSites;
    return numSites - 1;
}

// CLUE.C clear_location(org, city).
void Locations::clearLocation(int org, int city) {
    for (int i = 0; i < w_.hdr.numSites && i < 256; ++i) {
        auto& s = w_.sites[static_cast<std::size_t>(i)];
        if (s.org == org && s.city == city) {
            s.flags = 0;
            s.strength = s.f8 = s.fa = 0;
            w_.cities[static_cast<std::size_t>(city)].orgsKnown &= static_cast<std::uint16_t>(~bit16(org));
            break;
        }
    }
}

// CLUE.C free_location_if_unused(i).
void Locations::freeLocationIfUnused(int i) {
    auto& p = w_.persons[static_cast<std::size_t>(i)];
    if (p.flags & 0x7FFF) return;
    p.id = 0;
    p.org = -1;
    p.city = -1;
    p.known = 0;
}

}  // namespace reaction
