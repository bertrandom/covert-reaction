// Importing the original game from any of its releases into a folder of its own
// (the setup screen puts it in the per-user folder's `data/game`): a C++ port of
// covert-decomp's tools/extract_game.py. The source's format is told from its
// content, not its name:
//  - GOG's Windows installer (Inno Setup .exe)
//  - GOG's macOS installer (.pkg: xar, gzip'd cpio payload)
//  - GOG's Linux installer (.sh: a makeself script with a zip appended)
//  - eXoDOS's archive (.zip), or any zip holding the game
//  - the floppy disks: a .7z of disk images, a folder of images, or one FAT12
//    image; the disks are copied in order as INSTALL.BAT does (copy A:*.*, so a
//    later disk's file replaces an earlier copy)
//  - a folder that already holds the game (an installed copy)
// The game's folder is the one with GAME.EXE and COVERT.EXE (the shallowest);
// only its DOS 8.3 names are kept (upper case, flat), which drops what a repack
// put next to the game (eXoDOS's "Covert Action (1990).bat" and so on).
//
// No SDL, no external tools: zip/gzip/xar with miniz, 7z and LZMA with the LZMA
// SDK (formats/Archive.h, formats/InnoSetup.h).
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace reaction {

enum class GameSource {
    Unknown,      // not one of the formats below (or unreadable)
    Folder,       // a folder (an installed game, or disk images)
    GogWindows,   // Inno Setup installer
    GogMac,       // .pkg (xar)
    GogLinux,     // makeself .sh (a script with a zip appended)
    Zip,          // a zip (eXoDOS's, or any)
    SevenZip,     // a .7z (the floppy sets)
    DiskImage,    // one FAT floppy image
};

// What `source` is, from its first 2 MB and last 64 KB only (cheap enough for a
// file list). A folder is Folder whatever it holds.
GameSource detectGameSource(const std::filesystem::path& source);
// For the player: "GOG's Windows installer", "a floppy disk image", ...
const char* describeGameSource(GameSource kind);
// The file extensions worth offering in a file chooser (lower case, with the
// dot): .exe .pkg .sh .zip .7z .img .ima .dsk .vfd.
const std::vector<std::string>& importExtensions();
// Whether `file`'s extension is one of importExtensions() (ignoring case).
bool hasImportExtension(const std::filesystem::path& file);

// (fraction done in [0, 1], what's happening: "Reading the installer"...)
using ImportProgress = std::function<void(double fraction, const std::string& stage)>;

using GameFiles = std::map<std::string, std::vector<std::uint8_t>>;   // NAME.EXT -> content

// The game's files from `source`, in memory (for tests: reaction-tool import).
// Throws std::runtime_error with a message for the player if there's no game in
// it or its format isn't known. `kind`, if set, receives the format.
GameFiles collectGameFiles(const std::filesystem::path& source, GameSource* kind = nullptr,
                           const ImportProgress& progress = {});

struct ImportResult {
    bool ok = false;
    std::size_t files = 0;   // copied into the destination
    GameSource source = GameSource::Unknown;
    std::string error;       // why not, for the player (when !ok)
};

// Imports the game from `source` (a file or a folder) into `dest` (e.g. the
// per-user folder's `data/game`). The files are written to a new folder next to
// `dest` (`dest` + ".new"), which must then hold the game's data files
// (GameData::isGameDir); only then does it replace `dest`. A failed import
// leaves `dest` as it was. `source` may be `dest` itself, or inside it.
ImportResult importGame(const std::filesystem::path& source, const std::filesystem::path& dest,
                        const ImportProgress& progress = {});

// importGame on a thread of its own, for a UI that keeps drawing meanwhile: poll
// done(), progress() and stage(), then read result(). On the web (no threads)
// the import runs in the constructor, so it's done at once. The destructor waits
// for the import to finish.
class ImportJob {
public:
    ImportJob(std::filesystem::path source, std::filesystem::path dest, bool background = true);
    ~ImportJob();
    ImportJob(const ImportJob&) = delete;
    ImportJob& operator=(const ImportJob&) = delete;

    const std::filesystem::path& source() const { return source_; }
    const std::filesystem::path& dest() const { return dest_; }
    bool done() const { return done_.load(); }
    double progress() const { return progress_.load(); }
    std::string stage() const;
    // Valid once done().
    const ImportResult& result() const { return result_; }

private:
    void run();

    std::filesystem::path source_, dest_;
    std::atomic<bool> done_{false};
    std::atomic<double> progress_{0.0};
    mutable std::mutex stageLock_;
    std::string stage_;
    ImportResult result_;
#ifndef __EMSCRIPTEN__
    std::thread thread_;
#endif
};

}  // namespace reaction
