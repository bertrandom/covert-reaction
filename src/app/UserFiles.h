// Where the redux writes its own files (the Hall of Fame, later saved games): the
// per-user data folder (SDL_GetPrefPath: ~/Library/Application Support/CovertRedux
// on macOS, %APPDATA%\CovertRedux\CovertRedux on Windows, ~/.local/share/CovertRedux/
// CovertRedux on Linux), never the game directory, which may be read-only.
// COVERT_REDUX_USER_DIR in the environment replaces that folder.
#pragma once

#include <filesystem>
#include <string_view>

namespace redux {

// The per-user folder (empty if SDL can't tell).
std::filesystem::path userDataDir();
// A file in it.
std::filesystem::path userFile(std::string_view name);

}  // namespace redux
