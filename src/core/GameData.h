// Access to the original game's data files (an installed Covert Action directory).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reaction {

using Bytes = std::vector<std::uint8_t>;

class GameData {
public:
    // Looks for the game directory, in order: `explicitDir` (command line), the
    // COVERT_DATA environment variable, then `extraCandidates` (e.g. next to the
    // executable, inside the app bundle) and ./resources/Covert.
    static std::optional<GameData> locate(const std::optional<std::filesystem::path>& explicitDir,
                                          const std::vector<std::filesystem::path>& extraCandidates);

    explicit GameData(std::filesystem::path dir);

    const std::filesystem::path& dir() const { return dir_; }

    // File names are matched case-insensitively: the game asks for "camera.pic",
    // the files are CAMERA.PIC, and Linux file systems are case-sensitive.
    std::optional<std::filesystem::path> find(std::string_view name) const;
    bool exists(std::string_view name) const { return find(name).has_value(); }

    // Reads a whole file. Throws std::runtime_error if it's missing.
    Bytes read(std::string_view name) const;

    // True if `dir` looks like a Covert Action directory.
    static bool isGameDir(const std::filesystem::path& dir);
    // The marker files (TEXT.DTA, TITLE2.PAN, FACES.PIC) that `dir` lacks:
    // empty for a game directory, all of them for a missing folder.
    static std::vector<std::string> missingFiles(const std::filesystem::path& dir);

private:
    std::filesystem::path dir_;
};

}  // namespace reaction
