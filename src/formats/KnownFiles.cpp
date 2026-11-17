#include "formats/KnownFiles.h"

#include <fstream>
#include <iterator>
#include <string_view>

#include "core/GameData.h"
#include "formats/Archive.h"

namespace reaction {

namespace {

struct Known {
    std::string_view name;
    std::uint32_t size;
    std::uint32_t crc;
};

constexpr Known kKnown[] = {
#include "formats/KnownFiles.inc"
};

}  // namespace

bool isGameDataFile(const std::string& name) {
    if (name == "FAME.DTA") return true;
    for (const Known& k : kKnown)
        if (k.name == name) return true;
    return false;
}

bool isKnownGameFile(const std::string& name, std::span<const std::uint8_t> data) {
    std::uint32_t crc = 0;
    bool computed = false;
    for (const Known& k : kKnown) {
        if (k.name != name || k.size != data.size()) continue;
        if (!computed) crc = archive::crc32(data), computed = true;
        if (crc == k.crc) return true;
    }
    return false;
}

GameFileCheck checkGameFiles(const std::filesystem::path& dir) {
    GameFileCheck out;
    const GameData d(dir);
    std::string_view last;
    for (const Known& k : kKnown) {
        if (k.name == last) continue;   // (a second version of the same file)
        last = k.name;
        const std::string name(k.name);
        const auto path = d.find(name);
        if (!path) {
            out.missing.push_back(name);
            continue;
        }
        std::ifstream in(*path, std::ios::binary);
        const std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        ++out.checked;
        if (!isKnownGameFile(name, data)) out.damaged.push_back(name);
    }
    return out;
}

std::string GameFileCheck::describe() const {
    if (ok()) return {};
    auto list = [](const std::vector<std::string>& v) {
        std::string s;
        const std::size_t shown = v.size() > 4 ? 3 : v.size();
        for (std::size_t i = 0; i < shown; ++i) s += (i ? (i + 1 == shown && shown == v.size() ? " and " : ", ") : "") + v[i];
        if (shown < v.size()) s += " and " + std::to_string(v.size() - shown) + " other files";
        return s;
    };
    std::string s;
    if (!damaged.empty())
        s = list(damaged) + (damaged.size() == 1 ? " doesn't" : " don't") +
            " match any known release of the game: the copy may be damaged (e.g. a bad disk image).";
    if (!missing.empty())
        s += std::string(s.empty() ? "" : " ") + "Missing: " + list(missing) + ".";
    return s;
}

}  // namespace reaction
