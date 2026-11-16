// Where Covert Reaction writes its own files (the Hall of Fame, later saved games): the
// per-user data folder (SDL_GetPrefPath: ~/Library/Application Support/Covert Reaction
// on macOS, %APPDATA%\Covert Reaction on Windows, ~/.local/share/Covert Reaction on
// Linux), never the game directory, which may be read-only. Files from the old
// CovertRedux/CovertRedux folder are copied over on the first run.
// COVERT_REACTION_USER_DIR in the environment replaces that folder.
#pragma once

#include <filesystem>
#include <string_view>

namespace reaction {

// The per-user folder (empty if SDL can't tell).
std::filesystem::path userDataDir();
// A file in it.
std::filesystem::path userFile(std::string_view name);

}  // namespace reaction
