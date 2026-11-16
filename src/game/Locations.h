// Geography and the people/site tables: the game library's location helpers
// (covert-decomp GAMELIB/PERSON.C city_distance, org_alert_level, org_can_reach;
// GAMELIB/CLUE.C add_location, set_location_known, clear_location,
// free_location_if_unused). Shared by case generation (FINAL) and the city loop
// (GAME), so they live apart from CaseGen.
#pragma once

#include <cstdint>

#include "game/Rng.h"
#include "game/World.h"

namespace reaction {

// The MasterMind's defaults for person slot 0 (FINAL's mm_known, mm_org, mm_city,
// mm_id; words 4-0Ah of env.sve's player block). NEWCHAR.C plot_briefing picks them.
struct Mastermind {
    std::int16_t org = 0;
    std::int16_t city = 0;
    std::int16_t id = 0;
    std::uint16_t known = 0;
};

class Locations {
public:
    // Difficulty is read from world.hdr.difficulty (the library's `difficulty`).
    Locations(World& world, Rng& rng, const Mastermind& mm) : w_(world), rng_(rng), mm_(mm) {}

    // city_distance: distance between two city codes (low nibble x, high nibble y),
    // the longer axis counted twice.
    static int cityDistance(unsigned a, unsigned b);

    // org_alert_level: the organization's presence in a city, 0 (none) to 5.
    int orgAlertLevel(int city, int org) const;
    // org_can_reach: whether `org` (the CIA, in practice) can reach org2's people in city.
    bool orgCanReach(int org, int org2, int city) const;

    // add_location: the person slot of (org, city), ORing `flags` into it; else, with
    // flags, a new person in a random free slot (slot 0 for the MasterMind's place).
    // Returns -1 if absent and flags == 0 (after drawing a random slot all the same).
    int addLocation(int org, int city, std::uint16_t flags);
    // set_location_known: marks a site (org, city) known with `flags`, adding it.
    int setLocationKnown(int org, int city, std::uint16_t flags);
    // clear_location: forget a site.
    void clearLocation(int org, int city);
    // free_location_if_unused: free person slot i unless it still has flags 0..7FFFh.
    void freeLocationIfUnused(int i);

    const Mastermind& mastermind() const { return mm_; }

private:
    World& w_;
    Rng& rng_;
    Mastermind mm_;
};

}  // namespace reaction
