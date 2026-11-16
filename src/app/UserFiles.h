// Where Covert Reaction writes its own files (the Hall of Fame, later saved games): the
// per-user data folder (SDL_GetPrefPath: ~/Library/Application Support/Covert Reaction
// on macOS, %APPDATA%\Covert Reaction on Windows, ~/.local/share/Covert Reaction on
// Linux), never the game directory, which may be read-only. Files from the old
// CovertRedux/CovertRedux folder are copied over on the first run.
// Layout: settings.ini, the saved games and the Hall of Fame at the top, the
// imported game in data/game and the imported MT-32 ROMs in data/mt32.
// COVERT_REACTION_USER_DIR in the environment replaces that folder.
#pragma once

#include <filesystem>
#include <string_view>

namespace reaction {

// The per-user folder (empty if SDL can't tell).
std::filesystem::path userDataDir();
// A file in it.
std::filesystem::path userFile(std::string_view name);
// What the setup screen copies in: the game's files (data/game) and the MT-32
// ROMs (data/mt32). Empty if there's no per-user folder.
std::filesystem::path importedGameDir();
std::filesystem::path importedRomDir();

}  // namespace reaction
