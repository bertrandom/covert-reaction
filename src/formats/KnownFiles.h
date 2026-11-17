// The game's files as the known releases ship them (GOG's, which eXoDOS's
// matches, and the 1990 floppy disks, whose FINAL.EXE still has the copy
// protection): each file's size and CRC-32, to tell a damaged copy (a bad disk
// image, a broken download) from a good one. A file that matches no release is
// reported, not refused: there may be releases this table doesn't know.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace reaction {

struct GameFileCheck {
    std::vector<std::string> damaged;   // a known file whose content matches no release
    std::vector<std::string> missing;   // a known file that isn't there
    std::size_t checked = 0;            // known files found and compared
    bool ok() const { return damaged.empty() && missing.empty(); }
    // For the player: "AD.PIC, AFRICA.PIC and 18 other files don't match ...", or
    // empty when ok().
    std::string describe() const;
};

// Whether `name` (upper case, e.g. "FACES.PIC") with this content is one of a
// known release's files; false for a name the table doesn't have.
bool isKnownGameFile(const std::string& name, std::span<const std::uint8_t> data);
// Checks a game folder (names matched without regard to case).
GameFileCheck checkGameFiles(const std::filesystem::path& dir);

}  // namespace reaction
