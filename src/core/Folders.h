// The file-system side of the folder browser shown when the game files aren't
// found (PickerScene): listing folders and drives, tidying and converting paths.

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace reaction::folders {

namespace fs = std::filesystem;

// The home folder from the environment (HOME; USERPROFILE on Windows).
fs::path homeDir();

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

}  // namespace reaction::folders
