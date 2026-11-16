// Finding an installed copy of the game without being told where it is, and the
// file-system side of the folder browser shown when that fails (PickerScene).
//
// The search tries a fixed list of likely install places (home, Documents,
// /Applications, Program Files, GOG and Steam folders, Wine and DOSBox drives, ...)
// for folders with likely names ("Covert Action", COVERT, ...), going at most two
// levels into a matching folder or a *.app bundle. It never sweeps the disk.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reaction::game_search {

namespace fs = std::filesystem;

struct Places {
    fs::path home;                  // the user's home folder (empty: unknown)
    std::vector<fs::path> appDirs;  // the program's folder, the folder holding the .app, ...
};

// The home folder from the environment (HOME; USERPROFILE on Windows).
fs::path homeDir();

// The folders the search looks into, in order (only those that exist).
std::vector<fs::path> searchBases(const Places& places);

// The first game folder found in or just below the search bases.
std::optional<fs::path> search(const Places& places);

// ---- The folder browser ----------------------------------------------------

struct Entry {
    std::string name;   // as UTF-8
    fs::path path;
};

// The subfolders of `dir` (hidden ones skipped), sorted by name ignoring case.
std::vector<Entry> subfolders(const fs::path& dir);

// Windows: the drives that exist (C:\, D:\, ...), the browser's top level.
// Elsewhere: empty (the top is "/").
std::vector<Entry> drives();

// `p` made absolute and tidy (no "..", no trailing separator).
fs::path tidy(const fs::path& p);
// True if `dir` is a file-system root ("/", "C:\").
bool isRoot(const fs::path& dir);

// Paths <-> UTF-8 (settings.ini keeps paths as UTF-8 on every system).
std::string toUtf8(const fs::path& p);
fs::path fromUtf8(std::string_view s);

}  // namespace reaction::game_search
