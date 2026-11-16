// The system's own file dialogs, for the setup screen (SetupScene):
//  - macOS: NSOpenPanel (app/MacFolderPanel.h), which takes a file or a folder
//    in one panel
//  - Windows: the common item dialog (IFileOpenDialog), a file with a filter for
//    the game's releases or, separately, a folder (FOS_PICKFOLDERS)
//  - Linux: zenity, else kdialog (kdialog first on KDE), run as a subprocess; a
//    file or a folder, separately
// Without any (Linux without zenity or kdialog, the web), kind() is None and
// the setup screen opens its in-game browser (PickerScene) instead.
//
// For tests: COVERT_TEST_PICK=PATH makes the next dialog return PATH without
// showing anything ("PATH1|PATH2|...": one per dialog, in order; an empty one
// is a cancel). COVERT_FILE_DIALOG=builtin pretends there's no dialog (the
// in-game browser), COVERT_FILE_DIALOG=split pretends a file and a folder need
// two dialogs (as on Windows and Linux).
#pragma once

#include <SDL.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace reaction::dialogs {

enum class Kind { None, MacPanel, Windows, Zenity, KDialog };

enum class Pick { FileOrFolder, File, Folder };

struct Request {
    Pick pick = Pick::Folder;
    std::string title;                     // the dialog's title
    std::string message;                   // a line of explanation (where the dialog shows one)
    std::filesystem::path start;           // the folder it opens in (empty: the dialog's choice)
    std::vector<std::string> extensions;   // for files: ".exe", ".7z"... (lower case)
    std::string filterName;                // their name in the filter ("Covert Action releases")
};

// This system's dialogs (looked up once).
Kind kind();
const char* describe(Kind k);   // "zenity", "IFileOpenDialog"...
// Whether one dialog can take a file or a folder (else the caller offers both).
bool takesFileOrFolder(Kind k);

// COVERT_TEST_PICK's next path, if one is left: true and `out` set (empty:
// cancelled). False: there's none, show a real dialog.
bool takeTestPick(std::optional<std::filesystem::path>& out);

// Shows the dialog (modal; `parent` is the window it belongs to). The path
// chosen, or nullopt if cancelled (or the dialog failed: logged to stderr).
std::optional<std::filesystem::path> choose(const Request& request, SDL_Window* parent);

}  // namespace reaction::dialogs
