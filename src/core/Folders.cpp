#include "core/Folders.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>


namespace reaction::folders {

namespace {

std::string lower(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

[[maybe_unused]] bool isDir(const fs::path& p) {   // (Windows: drives())
    std::error_code ec;
    return !p.empty() && fs::is_directory(p, ec);
}

[[maybe_unused]] std::string envString(const char* name) {   // (Windows: homeDir())
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
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

bool needsPermission([[maybe_unused]] const fs::path& dir) {
#ifdef __APPLE__
    const fs::path d = dir.lexically_normal();
    if (d.parent_path() == fs::path("/Volumes")) return true;
    const fs::path home = homeDir();
    if (home.empty() || d.parent_path() != home.lexically_normal()) return false;
    static const char* const kProtected[] = {"Desktop", "Documents", "Downloads", "Pictures",
                                             "Movies", "Music", "Library"};
    for (const char* name : kProtected)
        if (d.filename() == name) return true;
#endif
    return false;
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

std::vector<Entry> filesWithExtensions(const fs::path& dir, const std::vector<std::string>& extensions) {
    std::vector<Entry> out;
    if (dir.empty()) return out;
    std::error_code ec;
    fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        std::string name = toUtf8(it->path().filename());
        if (name.empty() || name[0] == '.') continue;
        const std::string ext = lower(toUtf8(it->path().extension()));
        if (std::find(extensions.begin(), extensions.end(), ext) == extensions.end()) continue;
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

}  // namespace reaction::folders
