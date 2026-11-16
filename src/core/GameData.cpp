#include "core/GameData.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace reaction {

namespace fs = std::filesystem;

namespace {

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

// Files that every copy of the game has (the data, not the executables, which a
// player may have replaced with rebuilt ones).
constexpr std::string_view kMarkers[] = {"TEXT.DTA", "TITLE2.PAN", "FACES.PIC"};

}  // namespace

GameData::GameData(fs::path dir) : dir_(std::move(dir)) {}

bool GameData::isGameDir(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    GameData d(dir);
    return std::all_of(std::begin(kMarkers), std::end(kMarkers),
                       [&](std::string_view m) { return d.exists(m); });
}

std::vector<std::string> GameData::missingFiles(const fs::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    const bool isDir = fs::is_directory(dir, ec);
    GameData d(dir);
    for (std::string_view m : kMarkers)
        if (!isDir || !d.exists(m)) out.emplace_back(m);
    return out;
}

std::optional<GameData> GameData::locate(const std::optional<fs::path>& explicitDir,
                                         const std::vector<fs::path>& extraCandidates) {
    std::vector<fs::path> candidates;
    if (explicitDir) candidates.push_back(*explicitDir);
    if (const char* env = std::getenv("COVERT_DATA"); env && *env) candidates.emplace_back(env);
    candidates.insert(candidates.end(), extraCandidates.begin(), extraCandidates.end());
    candidates.emplace_back(fs::path("resources") / "Covert");
    for (const auto& c : candidates)
        if (isGameDir(c)) return GameData(c);
    return std::nullopt;
}

std::optional<fs::path> GameData::find(std::string_view name) const {
    std::error_code ec;
    fs::path direct = dir_ / std::string(name);
    if (fs::is_regular_file(direct, ec)) return direct;
    for (const auto& entry : fs::directory_iterator(dir_, ec)) {
        if (entry.is_regular_file(ec) && iequals(entry.path().filename().string(), name))
            return entry.path();
    }
    return std::nullopt;
}

Bytes GameData::read(std::string_view name) const {
    auto path = find(name);
    if (!path) throw std::runtime_error("game file not found: " + std::string(name));
    std::ifstream in(*path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path->string());
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace reaction
