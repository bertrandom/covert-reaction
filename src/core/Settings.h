// The redux's own options (not the original's): chosen in the main menu's Options
// window and kept in settings.ini in the per-user folder, one "key = value" per
// line. Missing files, keys or bad values give the defaults.
#pragma once

#include <filesystem>
#include <string>

namespace redux {

struct Settings {
    // 7 8 9 / U I O / J K L act as the keypad in TAC and BUG, and BUG also takes
    // the number row (TAC takes it anyway, as the original does).
    bool keypadKeys = true;
    // The leader line-up after the briefing counts: a wrong answer means a
    // practice mission, as with the original's unpatched check.
    bool copyProtection = false;
    // The sound device: auto (MT-32 if its ROMs are found, else AdLib), mt32,
    // adlib, speaker or none.
    std::string sound = "auto";
    // Desktop only: start in full screen (Alt+Enter also changes it).
    bool fullscreen = false;

    static Settings load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;   // false if it can't be written
};

}  // namespace redux
