// macOS: asking for the game's folder with the system's own open panel
// (NSOpenPanel) instead of the in-game browser. The panel runs outside the app,
// so it needs none of macOS's folder permissions (Desktop, Documents,
// Downloads...): the folder the player picks is granted to the app.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace reaction {

#ifdef __APPLE__
// Shows the panel (after SDL_Init(SDL_INIT_VIDEO), which sets up the
// application) opening in `start` if it's set. The chosen folder, or nullopt if
// the player cancelled.
std::optional<std::filesystem::path> macChooseFolder(const std::filesystem::path& start, const std::string& message);

// A two-button alert: true for `primary`, false for `secondary`.
bool macAlert(const std::string& title, const std::string& text, const std::string& primary,
              const std::string& secondary);
#endif

}  // namespace reaction
