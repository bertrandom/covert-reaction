#include "core/Settings.h"

#include <fstream>
#include <system_error>

namespace reaction {

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

void readBool(const std::string& v, bool& out) {
    if (v == "on" || v == "true" || v == "1" || v == "yes") out = true;
    else if (v == "off" || v == "false" || v == "0" || v == "no") out = false;
}

const char* onOff(bool b) { return b ? "on" : "off"; }

}  // namespace

Settings Settings::load(const std::filesystem::path& file) {
    Settings s;
    if (file.empty()) return s;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "keypad_keys") readBool(value, s.keypadKeys);
        else if (key == "copy_protection") readBool(value, s.copyProtection);
        else if (key == "fullscreen") readBool(value, s.fullscreen);
        else if (key == "sound" && (value == "auto" || value == "mt32" || value == "adlib" ||
                                    value == "speaker" || value == "none"))
            s.sound = value;
    }
    return s;
}

bool Settings::save(const std::filesystem::path& file) const {
    if (file.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    out << "# Covert Reaction settings (the main menu's Options)\n"
        << "keypad_keys = " << onOff(keypadKeys) << "\n"
        << "copy_protection = " << onOff(copyProtection) << "\n"
        << "sound = " << sound << "\n"
        << "fullscreen = " << onOff(fullscreen) << "\n";
    return static_cast<bool>(out);
}

}  // namespace reaction
