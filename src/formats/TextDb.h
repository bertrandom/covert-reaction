// The game's keyed text files: TEXT.DTA (messages), CLUES.TXT, PLOT.TXT, PROSE.DTA.
//
// Layout (read by lookup_text in the original's MSGTEXT.C): a "*KEY" line starts
// an entry; further "*" lines right after it are aliases; the entry's text is
// every following line up to the next line that starts with "*". The original reads
// lines with fscanf("%[^\n]\n"), so blank lines and leading blanks are skipped.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/GameData.h"

namespace redux {

// A placeholder and its replacement, e.g. {"$VICTIM", "Max Remington"}.
using Substitution = std::pair<std::string, std::string>;

class TextDb {
public:
    static TextDb parse(std::span<const std::uint8_t> bytes);
    static TextDb load(const GameData& data, std::string_view file) { return parse(data.read(file)); }

    // The entry's text: its lines joined with a space after each (as the original
    // builds text_buf), with substitutions applied line by line: for each
    // placeholder in order, its first occurrence in the line is replaced
    // (text_substitute). nullopt if the key isn't there.
    std::optional<std::string> lookup(std::string_view key,
                                      std::span<const Substitution> subst = {}) const;

    // What the game shows for a missing key.
    static std::string missing(std::string_view key) {
        return "This information requires security clearance: " + std::string(key);
    }

    // Keys in file order (the "*KEY" lines).
    std::vector<std::string> keys() const;

private:
    std::vector<std::string> lines_;
};

}  // namespace redux
