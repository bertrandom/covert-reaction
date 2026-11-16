#include "game/Fame.h"

#include <fstream>
#include <iterator>

#include "formats/Catalog.h"
#include "formats/Pic.h"
#include "game/NewChar.h"

namespace redux {

namespace {

std::int16_t s16(std::span<const std::uint8_t> b, std::size_t at) {
    return static_cast<std::int16_t>(b[at] | b[at + 1] << 8);
}

void put16(std::vector<std::uint8_t>& out, std::int16_t v) {
    const auto u = static_cast<std::uint16_t>(v);
    out.push_back(static_cast<std::uint8_t>(u & 0xFF));
    out.push_back(static_cast<std::uint8_t>(u >> 8));
}

constexpr int kVacations[21][5] = {
    {0, 1, -1, -1, -1}, {0, 2, -1, -1, -1}, {1, 4, -1, -1, -1}, {1, 3, -1, -1, -1}, {1, 2, -1, -1, -1},
    {1, 1, -1, -1, -1}, {1, 2, 1, -1, -1},  {2, 4, -1, -1, -1}, {2, 1, -1, -1, -1}, {2, 3, -1, -1, -1},
    {2, 4, 1, -1, -1},  {2, 2, -1, -1, -1}, {2, 1, 2, -1, -1},  {2, 4, 3, 2, -1},   {3, 4, -1, -1, -1},
    {3, 2, -1, -1, -1}, {3, 3, -1, -1, -1}, {3, 2, 4, -1, -1},  {3, 1, -1, -1, -1}, {3, 2, 3, 4, -1},
    {3, 4, 2, 1, 3},
};

constexpr const char* kVacationPrefix[] = {"lau", "off", "bch", "cas"};

}  // namespace

void HallOfFame::clear() {
    for (auto& e : entries) e.score = -1;
}

void HallOfFame::parse(std::span<const std::uint8_t> bytes) {
    // Lay the table out as bytes, copy the file over it, read it back.
    std::vector<std::uint8_t> raw = serialize();
    for (std::size_t i = 0; i < raw.size() && i < bytes.size(); ++i) raw[i] = bytes[i];
    std::span<const std::uint8_t> b(raw);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const std::size_t o = i * kEntrySize;
        auto& e = entries[i];
        e.score = s16(b, o);
        for (std::size_t k = 0; k < 32; ++k) e.name.raw[k] = static_cast<char>(b[o + 2 + k]);
        e.difficulty = s16(b, o + 0x22);
        e.plot = s16(b, o + 0x24);
        e.episode = s16(b, o + 0x26);
        e.points = s16(b, o + 0x28);
        e.month = s16(b, o + 0x2A);
    }
}

std::vector<std::uint8_t> HallOfFame::serialize() const {
    std::vector<std::uint8_t> out;
    out.reserve(entries.size() * kEntrySize);
    for (const auto& e : entries) {
        put16(out, e.score);
        for (char ch : e.name.raw) out.push_back(static_cast<std::uint8_t>(ch));
        put16(out, e.difficulty);
        put16(out, e.plot);
        put16(out, e.episode);
        put16(out, e.points);
        put16(out, e.month);
    }
    return out;
}

void HallOfFame::load(const std::filesystem::path& userFile, const std::filesystem::path& gameFile) {
    clear();
    std::error_code ec;
    const auto& path = std::filesystem::exists(userFile, ec) ? userFile : gameFile;
    if (path.empty()) return;
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    parse(bytes);
}

bool HallOfFame::save(const std::filesystem::path& userFile) const {
    std::error_code ec;
    if (userFile.has_parent_path()) std::filesystem::create_directories(userFile.parent_path(), ec);
    std::ofstream out(userFile, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const auto bytes = serialize();
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

int HallOfFame::insert(int points, int difficulty, bool maxine, const std::string& codename, int plot,
                       int episode, int month) {
    const int score2 = points * (difficulty + 1);
    for (int i = 0; i < kEntries; ++i) {
        if (entries[static_cast<std::size_t>(i)].score < score2 || i == kEntries - 1) {
            for (int k = kEntries - 1; k > i; --k)
                entries[static_cast<std::size_t>(k)] = entries[static_cast<std::size_t>(k) - 1];
            auto& e = entries[static_cast<std::size_t>(i)];
            e.score = static_cast<std::int16_t>(score2);
            e.name.set(codename);
            e.difficulty = static_cast<std::int16_t>(difficulty | (maxine ? 0x80 : 0));
            e.plot = static_cast<std::int16_t>(plot);
            e.episode = static_cast<std::int16_t>(episode);
            e.points = static_cast<std::int16_t>(points);
            e.month = static_cast<std::int16_t>(month);
            return i;
        }
    }
    return -1;
}

std::array<std::string, 3> fameLines(const FameEntry& e, int i) {
    std::array<std::string, 3> l;
    const int m = e.month;
    l[0] = std::to_string(i + 1) + ". Max '" + e.name.str() + "' Remington,  Case #" + std::to_string(m + 1) +
           " " + kMonthNames[static_cast<std::size_t>(((m % 12) + 12) % 12)] + " " + std::to_string(m / 12 + 1990);
    // plot_defs[plot].crime[episode] and roles: the file is trusted by the
    // original; out-of-range values show as "?" here.
    const bool okPlot = e.plot >= 0 && e.plot < 16 && e.episode >= 0 && e.episode < 8;
    const int crime = okPlot ? kPlotDefs[static_cast<std::size_t>(e.plot)].crime[static_cast<std::size_t>(e.episode)] : -1;
    l[1] = std::string(crime >= 0 && crime < 13 ? kCrimeNames[static_cast<std::size_t>(crime)] : "?") + "/" +
           (okPlot ? kPlotDefs[static_cast<std::size_t>(e.plot)].roles[static_cast<std::size_t>(e.episode)][0] : "?");
    l[2] = "EP: " + std::to_string(e.points) + "    \x80--- SCORE: " + std::to_string(e.score) + " ---";
    return l;
}

Vacation vacationFor(int n) {
    const auto& v = kVacations[n < 0 ? 0 : n > 20 ? 20 : n];
    return {v[0], {v[1], v[2], v[3], v[4]}};
}

const char* vacationCaption(int kind) {
    switch (kind) {
    case 0: return "After a difficult few days it's time to head for the LaundroMat.";
    case 1: return "Word of your latest case seems to have reached the secretarial pool.";
    case 2: return "After a successful case, you deserve some time at the beach!";
    default: return "You're feeling invincible, time to try your luck at the Casino!";
    }
}

VacationPictures loadVacation(const GameData& data, const Vacation& v, bool maxine, bool drawExtra) {
    const auto cat = data.read(v.kind <= 1 ? "final4.cat" : "final3.cat");
    Image page(320, 200, 0);  // page 2
    auto load = [&](const std::string& name) {
        // open_resource: the catalog's copy, else a loose file.
        Picture pic;
        if (auto off = catalogFind(cat, name)) pic = decodePicture(cat, *off);
        else pic = decodePicture(data.read(name));
        page.blit(pic.image, 0, 0, pic.image.width(), pic.image.height(), 0, 0);
        return page;
    };
    const std::string prefix = kVacationPrefix[v.kind];
    VacationPictures p;
    p.back = load(prefix + "back.pic");
    p.hand = load(prefix + "hand" + (maxine ? "f.pic" : "m.pic"));
    if (drawExtra) p.extra = load("laudude1.pic");
    for (int f : v.frames)
        if (f != -1) p.frames.push_back(load(prefix + (maxine ? "dude" : "babe") + std::to_string(f) + ".pic"));
    return p;
}

}  // namespace redux
