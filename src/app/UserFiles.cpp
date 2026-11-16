#include "app/UserFiles.h"

#include <SDL.h>

#include <cstdlib>
#include <system_error>

namespace reaction {

namespace {

// Before it was renamed, the app kept its files in <base>/CovertRedux/CovertRedux
// (SDL_GetPrefPath("CovertRedux", "CovertRedux")). On the first run of Covert
// Reaction, while its folder is still empty, those files are copied over (the old
// folder is left as it was).
void migrateFromCovertRedux(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path old = dir.parent_path() / "CovertRedux" / "CovertRedux";
    if (!fs::is_directory(old, ec)) return;
    if (fs::exists(dir, ec) && !fs::is_empty(dir, ec)) return;
    fs::create_directories(dir, ec);
    fs::copy(old, dir, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
}

}  // namespace

std::filesystem::path importedGameDir() {
    const auto dir = userDataDir();
    return dir.empty() ? std::filesystem::path() : dir / "data" / "game";
}

std::filesystem::path importedRomDir() {
    const auto dir = userDataDir();
    return dir.empty() ? std::filesystem::path() : dir / "data" / "mt32";
}

std::filesystem::path userDataDir() {
    static const std::filesystem::path dir = [] {
        std::filesystem::path p;
        // COVERT_REACTION_USER_DIR overrides it (tests keep their files apart).
        if (const char* env = std::getenv("COVERT_REACTION_USER_DIR"); env && *env) return std::filesystem::path(env);
        // The same folder main.cpp looks in for a Covert/ copy of the game. No
        // organisation: one folder named after the app.
        if (char* pref = SDL_GetPrefPath("", "Covert Reaction")) {
            p = std::filesystem::path(pref).parent_path();   // (drop the trailing '/')
            SDL_free(pref);
        }
        if (!p.empty()) migrateFromCovertRedux(p);
        return p;
    }();
    return dir;
}

std::filesystem::path userFile(std::string_view name) {
    const auto dir = userDataDir();
    return dir.empty() ? std::filesystem::path() : dir / std::string(name);
}

}  // namespace reaction
