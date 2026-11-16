// A minimal reader for Inno Setup installers (GOG's Windows release of the game
// is one), after innoextract's documentation and source (https://constexpr.org/
// innoextract/, zlib license), reimplemented: the setup loader's offset table,
// the LZMA-compressed setup headers (only as far as the file entries), the data
// entries, and the LZMA / LZMA2 / zlib chunks with the files, including the
// CALL/JMP address filter on executables. Installers whose data is in separate
// setup-*.bin files, or encrypted, aren't supported, nor setup data versions
// before 5.5.0 or after 6.4.
// Parts are adapted from innoextract (the CALL/JMP filter, GOG Galaxy's argument
// parsing), whose notice follows:
//   Copyright (C) 2011-2020 Daniel Scharrer
//   This software is provided 'as-is', without any express or implied warranty.
//   In no event will the author(s) be held liable for any damages arising from
//   the use of this software. Permission is granted to anyone to use this
//   software for any purpose, including commercial applications, and to alter it
//   and redistribute it freely, subject to the following restrictions:
//   1. The origin of this software must not be misrepresented; you must not claim
//      that you wrote the original software. If you use this software in a
//      product, an acknowledgment in the product documentation would be
//      appreciated but is not required.
//   2. Altered source versions must be plainly marked as such, and must not be
//      misrepresented as being the original software.
//   3. This notice may not be removed or altered from any source distribution.
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
