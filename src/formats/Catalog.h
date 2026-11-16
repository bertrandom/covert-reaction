// .CAT catalogs: several files in one (covert-decomp MPSLIB/FILEIO.C open_catalog,
// catalog_find, open_resource). FINAL3.CAT and FINAL4.CAT hold the vacation
// pictures.
//
// Layout: word count, then count entries of 18h bytes: name[12] (compared with
// strnicmp over 12 characters), 8 bytes the game doesn't read, long offset of
// the file's data in the catalog.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace redux {

// The offset of `name` in the catalog, nullopt if it isn't listed.
std::optional<std::size_t> catalogFind(std::span<const std::uint8_t> cat, std::string_view name);

}  // namespace redux
