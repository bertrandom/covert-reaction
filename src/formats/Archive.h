// Readers for the archives the original game ships in (formats/GameImport.h puts
// them together): zip (also appended to a script or an installer), gzip, xar
// (macOS .pkg), cpio, FAT12/16 floppy images and 7z, plus raw deflate, zlib and
// LZMA / LZMA2 streams. Everything works in memory on a whole file; errors throw
// std::runtime_error.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace reaction::archive {

using Bytes = std::vector<std::uint8_t>;
using Shared = std::shared_ptr<const Bytes>;   // a whole file, shared by its entries' readers

// One file of an archive: its path ('/'-separated, as stored) and how to read it,
// so a caller can pick files by name before decompressing anything.
struct Entry {
    std::string path;
    std::function<Bytes()> read;
};
using Entries = std::vector<Entry>;

// ---- Streams -----------------------------------------------------------------

// Deflate data: raw (zip) or with zlib's header (`zlib`). `size`: the expected
// size, if known (0: grow as needed).
Bytes inflate(std::span<const std::uint8_t> in, bool zlib, std::size_t size = 0);
// A gzip file (its first member).
Bytes gunzip(std::span<const std::uint8_t> in);
// LZMA with its 5 property bytes first (lc/lp/pb, dictionary size), no size field
// and no end mark needed: decodes until `in` ends, or `limit` bytes are out.
Bytes lzmaDecode(std::span<const std::uint8_t> in, std::size_t limit = SIZE_MAX);
// LZMA2 with its 1 property byte (dictionary size) first.
Bytes lzma2Decode(std::span<const std::uint8_t> in, std::size_t limit = SIZE_MAX);

std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc = 0);

// ---- Recognizing a format from the first bytes -------------------------------

bool isGzip(std::span<const std::uint8_t> head);
bool isXar(std::span<const std::uint8_t> head);
bool is7z(std::span<const std::uint8_t> head);
// A FAT floppy image: the boot sector's 55 AA and 512 or 1024 bytes per sector.
bool isFatImage(std::span<const std::uint8_t> head);
// A zip anywhere in `file` (an end-of-central-directory record near its end).
bool hasZip(std::span<const std::uint8_t> file);

// ---- Archives ------------------------------------------------------------------

// The files (not folders) of a zip, which may follow something else (a makeself
// script, a self-extractor): stored and deflated members.
Entries zipEntries(Shared file);
// A xar archive (macOS .pkg): its files, with gzip'd (zlib) data unpacked.
Entries xarEntries(Shared file);
// A cpio archive (odc "070707" or newc "070701"/"070702"): its regular files.
Entries cpioEntries(Shared file);
// A FAT12/16 floppy image: root and subfolders ("DIR/NAME.EXT").
Entries fatEntries(Shared image);
// A 7z archive (any of the LZMA SDK's methods: LZMA, LZMA2, PPMd, BCJ...).
Entries sevenZipEntries(Shared file);

}  // namespace reaction::archive
