// FINAL's career data: the player block that env.sve and the saved games carry
// (ENV.C save_env writes 0x28E bytes from plot_num; World::playerBlock holds them
// raw), and the session state Covert Reaction scenes share.
//
// Player block layout (FINAL.EXE DGROUP 7DF2h-8080h, words little-endian):
//   +000 plot_num        +002 plot_episodes   +004 mm_known     +006 mm_org
//   +008 mm_city         +00A mm_id           +00C leader_code  +00E last_episode
//   +010 episode_crime   +012 world_num       +014 career_points
//   +016 cur_month       +018 env_var_30_copy +01A cur_person_name[12]
//   +026 career_rank     +028 (unnamed word)  +02A player_name[40]
//   +052 player_org_name[40]                  +07A codename[16]
//   +08A history[512]    +28A masterminds_arrested (long)
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "game/Locations.h"
#include "game/Rng.h"
#include "game/World.h"

namespace reaction {

struct Career {
    static constexpr std::size_t kBlockSize = 0x28E;

    std::int16_t plotNum = 0;          // plot_defs index of the MasterMind's plan
    std::uint16_t plotEpisodes = 0;    // bit per episode already played
    Mastermind mm;                     // mm_known, mm_org, mm_city, mm_id
    std::int16_t leaderCode = 0;       // the MasterMind's leader code (orgs[mm_org].flags & 0xFF)
    std::int16_t lastEpisode = -1;     // episode of the current case
    std::int16_t episodeCrime = 0;     // crimeN.dta of the case
    std::int16_t worldNum = 0;         // worldN.dta
    std::int16_t careerPoints = 0;
    std::int16_t curMonth = 0;         // cases played (the date: month cur_month % 12 of 1990 + n)
    std::int16_t envVar30Copy = 0;
    FixedString<12> curPersonName;
    std::int16_t careerRank = 99;
    std::int16_t f28 = 0;
    FixedString<40> playerName;        // the case's first briefing role (plot_defs roles[ep][0])
    FixedString<40> playerOrgName;     // its second role ([ep][1])
    FixedString<16> codename;          // Max's code name
    std::array<std::uint8_t, 0x200> history{};  // per case: plot/episode, result, EP hi, lo,
                                                // leader if arrested; FFh ends
    std::uint32_t mastermindsArrested = 0;      // bit per leader code

    static Career unpack(const std::array<std::uint8_t, kBlockSize>& block);
    void pack(std::array<std::uint8_t, kBlockSize>& block) const;
};

// What Covert Reaction keeps for a run of the game: the original's tables (env.sve),
// the career, the random numbers, and FINAL's globals that aren't saved.
struct GameState {
    GameState();

    World world;
    Career career;
    // FINAL main: srand(rand_seed = time(0) & 7FFFh). COVERT_SEED in the
    // environment overrides the seed (reproducible runs for testing).
    Rng rng;
    bool briefed = false;              // FINAL's `briefed`: the plot briefing was shown

    // identify_leader_faces is the copy protection: the 1990 floppy release
    // refuses a wrong answer (the case is replaced by a practice mission), the GOG
    // and eXoDOS releases have the check patched out (the line-up is still shown,
    // any answer passes). Off by default, as in the GOG release; COVERT_COPY_PROTECTION=1
    // in the environment turns it on.
    bool copyProtection = false;

    // The player block <-> career.
    void storeCareer() { career.pack(world.playerBlock); }
    void loadCareer() { career = Career::unpack(world.playerBlock); }
};

}  // namespace reaction
