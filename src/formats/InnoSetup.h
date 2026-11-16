// A minimal reader for Inno Setup installers (GOG's Windows release of the game
// is one), after innoextract's documentation and source (https://constexpr.org/
// innoextract/, zlib license), reimplemented: the setup loader's offset table,
// the LZMA-compressed setup headers (only as far as the file entries), the data
// entries, and the LZMA / LZMA2 / zlib chunks with the files, including the
// CALL/JMP address filter on executables. Installers whose data is in separate
// setup-*.bin files, or encrypted, aren't supported, nor setup data versions
// before 5.5.0 or after 6.4.
#pragma once

#include <span>

#include "formats/Archive.h"

namespace reaction::archive {

// "Inno Setup" in an "MZ" executable's first 2 MB (as covert-decomp's
// extract_game.py tells it).
bool isInnoSetup(std::span<const std::uint8_t> head);

// The files the installer installs, by destination ("{app}/GAME.EXE",
// "{tmp}/..."), with '/' for '\'.
Entries innoEntries(Shared file);

}  // namespace reaction::archive
