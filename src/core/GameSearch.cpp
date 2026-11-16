#include "core/GameSearch.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <system_error>

#include "core/GameData.h"

namespace reaction::game_search {

namespace {

// A folder name in lower case, letters and digits only: "Sid Meier's Covert
// Action" -> "sidmeierscovertaction".
std::string normalized(std::string_view name) {
    std::string out;
    for (char ch : name) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    }
    return out;
}

// Names the game's folder may have: Covert, COVERT, COVERTAC (the DOS install's
// default), Covert Action, CovertAction, Sid Meier's Covert Action (Classic) ...
// ("Covert Reaction", this program, isn't one).
bool gameFolderName(std::string_view name) {
    const std::string n = normalized(name);
    return n == "covert" || n == "covertac" || n.find("covertaction") != std::string::npos;
}

// Inside a game's folder or app bundle the files may sit one level down.
bool innerFolderName(std::string_view name) {
    if (gameFolderName(name)) return true;
    const std::string n = normalized(name);
    return n == "game" || n == "data" || n == "drivec";
}

std::string lower(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool isDir(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}

[[maybe_unused]] std::string envString(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// `dir` if it holds the game, else the first match in its subfolders with
// likely names, down to `depth` levels.
std::optional<fs::path> probe(const fs::path& dir, int depth) {
    if (GameData::isGameDir(dir)) return dir;
    if (depth <= 0) return std::nullopt;
    for (const auto& e : subfolders(dir))
        if (innerFolderName(e.name))
            if (auto found = probe(e.path, depth - 1)) return found;
    return std::nullopt;
}

// Where to look, and how.
struct Base {
    fs::path dir;
    bool inside = false;   // false: the game in `dir` or a likely-named subfolder;
                           // true (an app bundle's Resources): anywhere 2 levels down
};

std::vector<Base> bases(const Places& places) {
    std::vector<Base> out;
    std::set<fs::path> seen;
    auto add = [&](const fs::path& p, bool inside = false) {
        if (p.empty()) return;
        const fs::path t = tidy(p);
        if (!isDir(t) || !seen.insert(t).second) return;
        out.push_back({t, inside});
    };
    const fs::path& home = places.home;
    auto inHome = [&](const fs::path& rel) { if (!home.empty()) add(home / rel); };

    // Next to the program (or inside its folder: the program dropped into the game's).
    for (const auto& d : places.appDirs) add(d);

#ifdef _WIN32
    inHome("");
    inHome("Documents");
    inHome("Games");
    inHome("Desktop");
    inHome("Downloads");
    for (const char* var : {"ProgramFiles(x86)", "ProgramFiles", "ProgramW6432"}) {
        const std::string pf = envString(var);
        if (pf.empty()) continue;
        add(fs::path(pf));
        add(fs::path(pf) / "GOG Galaxy" / "Games");
        add(fs::path(pf) / "GOG.com");
        add(fs::path(pf) / "Steam" / "steamapps" / "common");
    }
    for (const char* drive : {"C:\\", "D:\\"}) {
        const fs::path d(drive);
        add(d / "GOG Games");
        add(d / "GOG Galaxy" / "Games");
        add(d / "Games");
        add(d / "SteamLibrary" / "steamapps" / "common");
        add(d / "DOSBox");
        add(d / "DOS");
        add(d / "dosgames");
        add(d);
    }
#else
    // A Wine-style C: drive: the game installed by a Windows installer under Wine,
    // CrossOver or similar.
    auto addDriveC = [&](const fs::path& c) {
        if (!isDir(c)) return;
        add(c);
        add(c / "GOG Games");
        add(c / "Games");
        add(c / "Program Files");
        add(c / "Program Files (x86)");
        add(c / "Program Files (x86)" / "GOG Galaxy" / "Games");
        add(c / "Program Files" / "GOG Galaxy" / "Games");
        add(c / "Program Files (x86)" / "Steam" / "steamapps" / "common");
    };

    inHome("");
    inHome("Documents");
    inHome("Games");
    inHome("Games/GOG");
    inHome("Games/Heroic");
    inHome("GOG Games");
    inHome("Desktop");
    inHome("Downloads");
    // DOSBox's habits: a folder mounted as C:, often holding the game's own folder.
    for (const char* d : {"dosbox", "DOSBox", "dosbox/c", "DOSBox/C", "dos", "DOS", "dosgames",
                          "DOSGames", ".dosbox", "c", "C"})
        inHome(d);
#ifdef __APPLE__
    // GOG's macOS release is an app bundle (its DOSBox and the game inside).
    for (const fs::path& apps : {fs::path("/Applications"), home.empty() ? fs::path() : home / "Applications"}) {
        add(apps);
        for (const auto& e : subfolders(apps)) {
            const std::string n = e.name;
            if (n.size() > 4 && lower(n.substr(n.size() - 4)) == ".app" && gameFolderName(n.substr(0, n.size() - 4)))
                add(e.path / "Contents" / "Resources", true);
        }
    }
    inHome("Library/Application Support/Steam/steamapps/common");
    if (!home.empty())
        for (const auto& bottle : subfolders(home / "Library/Application Support/CrossOver/Bottles"))
            addDriveC(bottle.path / "drive_c");
#else
    inHome(".local/share");
    inHome(".local/share/games");
    add("/usr/share/games");
    add("/usr/local/share/games");
    add("/usr/games");
    add("/opt");
    add("/opt/games");
    inHome(".steam/steam/steamapps/common");
    inHome(".local/share/Steam/steamapps/common");
    inHome(".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common");
#endif
    if (!home.empty()) {
        addDriveC(home / ".wine" / "drive_c");
        for (const auto& bottle : subfolders(home / ".cxoffice")) addDriveC(bottle.path / "drive_c");
    }
#endif
    return out;
}

}  // namespace

fs::path homeDir() {
#ifdef _WIN32
    if (const char* p = std::getenv("USERPROFILE"); p && *p) return fs::path(p);
    const std::string drive = envString("HOMEDRIVE"), path = envString("HOMEPATH");
    if (!drive.empty() && !path.empty()) return fs::path(drive + path);
    return {};
#else
    const char* p = std::getenv("HOME");
    return p && *p ? fs::path(p) : fs::path();
#endif
}

std::vector<fs::path> searchBases(const Places& places) {
    std::vector<fs::path> out;
    for (const auto& b : bases(places)) out.push_back(b.dir);
    return out;
}

std::optional<fs::path> search(const Places& places) {
    for (const auto& b : bases(places)) {
        if (b.inside) {
            if (auto found = probe(b.dir, 2)) return found;
            continue;
        }
        if (GameData::isGameDir(b.dir)) return b.dir;
        for (const auto& e : subfolders(b.dir))
            if (gameFolderName(e.name))
                if (auto found = probe(e.path, 1)) return found;
    }
    return std::nullopt;
}

std::vector<Entry> subfolders(const fs::path& dir) {
    std::vector<Entry> out;
    if (dir.empty()) return out;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code ec2;
        if (!it->is_directory(ec2)) continue;
        std::string name = toUtf8(it->path().filename());
        if (name.empty() || name[0] == '.') continue;
#ifdef _WIN32
        if (name[0] == '$' || name == "System Volume Information") continue;
#endif
        out.push_back({std::move(name), it->path()});
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        const std::string la = lower(a.name), lb = lower(b.name);
        return la != lb ? la < lb : a.name < b.name;
    });
    return out;
}

std::vector<Entry> drives() {
    std::vector<Entry> out;
#ifdef _WIN32
    // (Not A: and B:: asking an empty floppy drive can hang.)
    for (char d = 'C'; d <= 'Z'; ++d) {
        const std::string root = std::string(1, d) + ":\\";
        if (isDir(fs::path(root))) out.push_back({root, fs::path(root)});
    }
#endif
    return out;
}

fs::path tidy(const fs::path& p) {
    std::error_code ec;
    fs::path a = fs::absolute(p, ec);
    if (ec) a = p;
    a = a.lexically_normal();
    if (a.has_relative_path() && !a.has_filename()) a = a.parent_path();
    return a;
}

bool isRoot(const fs::path& dir) { return !dir.empty() && !dir.has_relative_path(); }

std::string toUtf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

fs::path fromUtf8(std::string_view s) { return fs::path(std::u8string(s.begin(), s.end())); }

}  // namespace reaction::game_search
