#include "game/SaveGame.h"

#include <cctype>
#include <cerrno>
#include <fstream>
#include <iterator>
#include <system_error>

#include "game/NewChar.h"

namespace reaction::savegame {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kEnvSize = 0x3F0;

// The bytes of a (possibly short) file, as read() leaves the buffers: what the
// file has, the rest as it was (zero here).
int word(const std::vector<std::uint8_t>& f, std::size_t off) {
    const int lo = off < f.size() ? f[off] : 0;
    const int hi = off + 1 < f.size() ? f[off + 1] : 0;
    return static_cast<std::int16_t>(lo | hi << 8);
}

std::string str(const std::vector<std::uint8_t>& f, std::size_t off, std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n && off + i < f.size() && f[off + i]; ++i) s += static_cast<char>(f[off + i]);
    return s;
}

}  // namespace

std::string slotFileName(int slot) { return "cv" + std::to_string(slot) + ".sve"; }

std::string slotListing(const std::optional<std::vector<std::uint8_t>>& file) {
    if (!file) return " (EMPTY)\n \n \n";
    const auto& f = *file;
    // save_header: the player block at 3F0h; save_level: the difficulty at 2Ah
    // (ENV+3Ah).
    const std::size_t pb = kEnvSize;
    const int month = word(f, pb + 0x16), time = word(f, pb + 0x18), rank = word(f, pb + 0x26);
    const int level = word(f, 0x2A);
    std::string t = " Max '" + str(f, pb + 0x7A, 16) + "' Remington: 00" + std::to_string(rank) + " rank\n ... ";
    t += str(f, pb + 0x1A, 12);
    t += ": ";
    t += kMonthNames[static_cast<std::size_t>((month % 12 + 12) % 12)];
    t += " ";
    t += std::to_string(static_cast<std::int16_t>(time) / 1440 + 1);
    t += ", ";
    t += std::to_string(month / 12 + 1990);
    static const char* const kLevels[] = {" (Local)", " (National)", " (Regional)", " (Global)"};
    if (level >= 0 && level < 4) t += kLevels[level];
    t += ".\n \n";
    return t;
}

std::string slotMenu(bool loading, const std::array<std::optional<std::vector<std::uint8_t>>, kSlots>& files) {
    std::string t = loading ? "Select Load File...\n" : "Select Save File...\n";
    for (const auto& f : files) t += slotListing(f);
    return t;
}

std::vector<std::uint8_t> saveBytes(GameState& g) {
    auto& w = g.world;
    g.career.envVar30Copy = w.hdr.clockBase;
    g.career.curPersonName.set(w.cities[static_cast<std::size_t>(w.hdr.hereCity & 15)].name.str());
    g.storeCareer();
    return w.save();
}

bool loadBytes(GameState& g, std::span<const std::uint8_t> bytes) {
    World w;
    try {
        w = World::loadSave(bytes);
    } catch (const std::exception&) {
        return false;
    }
    if (w.hdr.numSuspects < 0 || w.hdr.numSuspects > 64 || w.hdr.numPlots < 0 || w.hdr.numPlots > 64) return false;
    g.world = std::move(w);
    g.loadCareer();
    g.world.hdr.options = static_cast<std::int16_t>(g.world.hdr.options & ~0x80);
    return true;
}

void beforeCitySave(GameState& g) {
    auto& h = g.world.hdr;
    h.options = static_cast<std::int16_t>(h.options & ~0x40);
    h.var50 = h.difficulty;
}

std::optional<fs::path> findSlot(const fs::path& userDir, const fs::path& gameDir, int slot) {
    std::error_code ec;
    const std::string name = slotFileName(slot);
    auto look = [&](const fs::path& dir) -> std::optional<fs::path> {
        if (dir.empty() || !fs::is_directory(dir, ec)) return std::nullopt;
        if (fs::is_regular_file(dir / name, ec)) return dir / name;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            std::string f = e.path().filename().string();
            if (f.size() != name.size()) continue;
            bool same = true;
            for (std::size_t i = 0; i < f.size() && same; ++i)
                same = std::tolower(static_cast<unsigned char>(f[i])) == name[i];
            if (same && e.is_regular_file(ec)) return e.path();
        }
        return std::nullopt;
    };
    if (auto p = look(userDir)) return p;
    return look(gameDir);
}

std::array<std::optional<std::vector<std::uint8_t>>, kSlots> readSlots(const fs::path& userDir,
                                                                        const fs::path& gameDir) {
    std::array<std::optional<std::vector<std::uint8_t>>, kSlots> out;
    for (int i = 0; i < kSlots; ++i) {
        auto p = findSlot(userDir, gameDir, i);
        if (!p) continue;
        std::ifstream in(*p, std::ios::binary);
        if (!in) continue;
        out[static_cast<std::size_t>(i)] =
            std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    return out;
}

int writeSlot(const fs::path& userDir, int slot, std::span<const std::uint8_t> bytes) {
    std::error_code ec;
    if (userDir.empty()) return -1;
    fs::create_directories(userDir, ec);
    const fs::path path = userDir / slotFileName(slot);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return -1;   // open() failed: "File Error"
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
        const int e = errno;
        if (e == EACCES || e == EPERM || e == EROFS) return 13;
        if (e == ENOSPC) return 28;
        return e ? e : 5;
    }
    return 0;
}

}  // namespace reaction::savegame
