#include "app/UserFiles.h"

#include <SDL.h>

#include <cstdlib>

namespace redux {

std::filesystem::path userDataDir() {
    static const std::filesystem::path dir = [] {
        std::filesystem::path p;
        // COVERT_REDUX_USER_DIR overrides it (tests keep their files apart).
        if (const char* env = std::getenv("COVERT_REDUX_USER_DIR"); env && *env) return std::filesystem::path(env);
        // The same folder main.cpp looks in for a Covert/ copy of the game.
        if (char* pref = SDL_GetPrefPath("CovertRedux", "CovertRedux")) {
            p = pref;
            SDL_free(pref);
        }
        return p;
    }();
    return dir;
}

std::filesystem::path userFile(std::string_view name) {
    const auto dir = userDataDir();
    return dir.empty() ? std::filesystem::path() : dir / std::string(name);
}

}  // namespace redux
