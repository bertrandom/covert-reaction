// The game world: the tables the original keeps in memory and passes between its
// programs in env.sve and saved games (ENV.C save_env/load_env). Field meanings
// follow covert-decomp's GAMELIB.H; the layouts are the original's, so saved games
// round-trip byte for byte.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/GameData.h"

namespace redux {

class ByteReader;
class ByteWriter;

// A fixed-size string field, kept as raw bytes (bytes after the NUL survive a
// load/save round trip).
template <std::size_t N>
struct FixedString {
    std::array<char, N> raw{};
    std::string str() const {
        std::size_t n = 0;
        while (n < N && raw[n]) ++n;
        return std::string(raw.data(), n);
    }
    void set(const std::string& s) {
        raw.fill(0);
        for (std::size_t i = 0; i < N - 1 && i < s.size(); ++i) raw[i] = s[i];
    }
};

struct Person {                 // 16 bytes; 64 slots, slot 0 is the mastermind
    std::int16_t org = 0;
    std::int16_t id = 0;        // 0 = free; bit 0 sex, bits 1-4 first name, 5-8 last name
    std::int16_t city = 0;
    std::uint16_t known = 0;    // 1 photo, 2 name, 4 org, 8 city, 10h participant, ...
    std::uint16_t flags = 0;    // bit per suspect slot he holds (role mask), 8000h decoy
    std::uint16_t roles = 0;    // bit per suspect slot whose role we've uncovered
    std::int16_t points = 0;
    std::int16_t fate = 0;      // -1 arrested, -2 in hiding, -3 turned
};

struct Site {                   // 12 bytes; 256 slots
    std::int16_t org = 0, city = 0;
    std::uint16_t flags = 0;
    std::int16_t strength = 0;  // wiretap / surveillance level, percent
    std::int16_t f8 = 0, fa = 0;
};

struct Clue {                   // 12 bytes; 80 slots
    std::int8_t org = -1;       // -1 = free
    std::int8_t city = 0;
    std::int16_t suspect = -1;
    std::int16_t kind = 0;      // (kind << 8) | value
    std::int16_t text = 0;      // (text flags << 8) | text number
    std::int16_t pad0 = 0, pad1 = 0;
};

struct City {                   // 34 bytes; 16 cities
    FixedString<12> name, country;
    std::int16_t code = 0;      // low nibble x, high nibble y
    std::int16_t size = 0;
    std::uint16_t orgsKnown = 0, orgsVisited = 0;
    std::uint8_t mapX = 0, mapY = 0;
};

struct Msg {                    // 14 bytes; 128 slots
    std::int16_t type = -1;     // 0 message, else a chronology event; -1 none
    std::int16_t time = 0;      // minutes
    std::int16_t msg = 0;       // plot step, <0 generic
    std::uint16_t flags = 0;    // bits 0-3 revealed details, 8000h decoded
    std::int16_t level = 0;
    std::int8_t sender = 0, senderCity = 0, receiver = 0, receiverCity = 0;
};

struct Plot {                   // 44 bytes; a step of the plot
    std::int8_t suspect = -1;   // suspect slot doing it, -1 ends the list
    std::int8_t f1 = 0;
    std::int16_t state = 0;     // day it happened, 999 = due
    std::int16_t msg = 0;
    FixedString<32> name;
    std::int16_t flags = 0;     // low byte: receiving slot; 100h done, 200h message, ...
    std::int16_t items = 0;
    std::int16_t severity = 0;
};

struct Suspect {                // 48 bytes; a role in the master plan
    std::int16_t person = -1;
    std::int16_t importance = 0;
    FixedString<32> name;
    std::uint8_t info = 0;
    std::int8_t f25 = 0;
    std::uint16_t flags = 0;    // 100h: not shown on the master plan
    std::uint16_t found = 0;
    std::int16_t type = 0;
    std::int16_t rank = 0;
    std::int16_t f2e = 0;
};

struct SuspectState {           // 6 bytes
    std::int16_t time = 0, progress = 0, f4 = 0;
};

struct Org {                    // 36 bytes; 16 organizations
    FixedString<6> abbrev;
    FixedString<20> name;
    std::int16_t home = 0;      // city code of the headquarters
    std::int16_t strength = 0, range = 0;
    std::uint16_t flags = 0;    // 1 = known; in worldN.dta the leader code
    std::int8_t nationality = 0, f23 = 0;
};

struct Item {                   // 18 bytes; 4 objects the plot moves
    FixedString<16> name;
    std::int8_t icon = 0;       // on icons.pic
    std::int8_t holder = -1;
};

// The header fields save_env/load_env keep in the env block (ENV offsets 20h-53h).
struct WorldHeader {
    std::int16_t hereCity = 0, hereOrg = 0, options = 0, msgDay = 0;
    std::int16_t org2 = 0, org3 = 0, city1 = 0, city2 = 0;
    std::int16_t clockBase = 0, numPersons = 0, numSites = 0, gameTime = 0;
    std::int16_t var38 = 0, difficulty = 0, randSeed = 0, numSuspects = 0;
    std::int16_t numPlots = 0, jailbreakPerson = 0, videoMode = 0, numMsgs = 0;
    std::int16_t var48 = 0, doubleAgents = 0, seenOrgs = 0, skillLevels = 0;
    std::int16_t var50 = 0, var52 = 0;
};

struct World {
    WorldHeader hdr;
    std::array<std::uint8_t, 0x28E> playerBlock{};   // FINAL's career data (plot_num ...)
    std::vector<Suspect> suspects;
    std::vector<SuspectState> suspectState;
    std::vector<Plot> plots;
    std::array<City, 16> cities{};
    std::array<Org, 16> orgs{};
    std::array<Person, 64> persons{};
    std::array<Site, 256> sites{};
    std::array<Clue, 80> clues{};
    std::array<Msg, 128> msgs{};
    std::array<Item, 4> items{};
    std::array<std::int16_t, 32> cityClueCount{};

    // A saved game (the original's save files): the 3F0h-byte env block, then the
    // tables in save_env's order.
    static World loadSave(std::span<const std::uint8_t> bytes);
    std::vector<std::uint8_t> save() const;

    // Scenario files: crimeN.dta (suspects, plot steps, items) and worldN.dta
    // (cities, organizations), as FINAL's load_crime_file/load_world_file read them.
    void loadCrime(const GameData& data, int n);
    void loadWorld(const GameData& data, int n);

    // The env block (3F0h bytes from ENV+10h) other than the header fields, kept
    // so that a load/save round trip doesn't change it.
    std::array<std::uint8_t, 0x3F0> env{};
};

}  // namespace redux
