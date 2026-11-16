// The Hall of Fame (covert-decomp FINAL/FAME.C clear_hall_of_fame,
// load_hall_of_fame, save_hall_of_fame, hall_of_fame_insert; the screen,
// show_hall_of_fame, is src/app/FameScene.cpp) and the vacation snapshots it
// shows (ENDCASE.C vacation_snapshot's picture list).
//
// FAME.DTA is the six entries, 2Ch bytes each, as in memory (struct Fame):
//   +00 score (-1 = empty)  +02 code name[32]  +22 difficulty (bit 7: Maxine)
//   +24 plot  +26 episode  +28 efficiency points  +2A cur_month
// The original reads and writes it in the game directory, and only saves when
// the file exists and is writable. Covert Reaction saves to a per-user file instead
// and reads the game directory's copy only when it has none yet; the paths are
// the caller's (src/app knows where they are).
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "core/GameData.h"
#include "core/Image.h"
#include "game/World.h"

namespace reaction {

struct FameEntry {
    std::int16_t score = -1;
    FixedString<32> name;
    std::int16_t difficulty = 0;
    std::int16_t plot = 0;
    std::int16_t episode = 0;
    std::int16_t points = 0;
    std::int16_t month = 0;
};

class HallOfFame {
public:
    static constexpr int kEntries = 6;
    static constexpr std::size_t kEntrySize = 0x2C;

    std::array<FameEntry, kEntries> entries{};

    void clear();                                   // clear_hall_of_fame
    // read(): as many bytes as the file has (up to 6 entries) over a cleared table.
    void parse(std::span<const std::uint8_t> bytes);
    std::vector<std::uint8_t> serialize() const;

    // load_hall_of_fame: `userFile` if it exists, else `gameFile` (FAME.DTA, may
    // be empty), else an empty table.
    void load(const std::filesystem::path& userFile, const std::filesystem::path& gameFile);
    // save_hall_of_fame, to the per-user file (its directory is created). False on error.
    bool save(const std::filesystem::path& userFile) const;

    // hall_of_fame_insert's placement: score = points * (difficulty + 1), entered
    // before the first lower score (or as the 6th, which only that screen shows).
    // Returns the index.
    int insert(int points, int difficulty, bool maxine, const std::string& codename, int plot, int episode,
               int month);
};

// show_hall_of_fame's line texts for entry i (non-empty): the name and date, the
// crime / first role, and "EP: n    \x80--- SCORE: n ---".
std::array<std::string, 3> fameLines(const FameEntry& e, int i);

// The vacation snapshot for a score / 150 (0..20): ENDCASE.C vacations and
// vacation_prefix.
struct Vacation {
    int kind = 0;                       // 0 LaundroMat, 1 office, 2 beach, 3 casino
    std::array<int, 4> frames{};        // dude/babe frame numbers, -1 none
};
Vacation vacationFor(int n);
// vacation_snapshot's caption (show_text), unwrapped.
const char* vacationCaption(int kind);

// The pictures of a vacation snapshot as vacation_snapshot loads them from
// final3.cat / final4.cat. The pictures are decoded over one page in turn
// (load_picture_swapped onto page 2) and each grabbed whole, so they are kept
// 320x200; colour 0 is transparent when they are layered.
struct VacationPictures {
    Image back;                         // <prefix>back.pic
    Image hand;                         // <prefix>hand{f,m}.pic (vacation_frames[0])
    Image extra;                        // laudude1.pic, if drawn (empty() otherwise)
    std::vector<Image> frames;          // <prefix>{dude,babe}N.pic, in order
};
// `maxine`: options bit 2 (she gets the dudes). `drawExtra`: kind <= 1 &&
// random(3), the caller draws it in the original's order.
VacationPictures loadVacation(const GameData& data, const Vacation& v, bool maxine, bool drawExtra);

}  // namespace reaction
