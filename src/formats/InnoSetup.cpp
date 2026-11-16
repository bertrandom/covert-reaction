#include "formats/InnoSetup.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string_view>

namespace reaction::archive {

namespace {

using Span = std::span<const std::uint8_t>;

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("Inno Setup: " + what); }

// Setup data versions, as innoextract numbers them: a.b.c.d in one word.
constexpr std::uint32_t ver(unsigned a, unsigned b, unsigned c, unsigned d = 0) {
    return a << 24 | b << 16 | c << 8 | d;
}

std::string versionText(std::uint32_t v) {
    std::string s = std::to_string(v >> 24) + "." + std::to_string(v >> 16 & 0xff) + "." + std::to_string(v >> 8 & 0xff);
    if (v & 0xff) s += "." + std::to_string(v & 0xff);
    return s;
}

class Reader {
public:
    explicit Reader(Span d) : d_(d) {}

    std::uint8_t u8() {
        need(1);
        return d_[p_++];
    }
    std::uint16_t u16() { return static_cast<std::uint16_t>(u8() | u8() << 8); }
    std::uint32_t u32() {
        need(4);
        std::uint32_t v = 0;
        for (int i = 3; i >= 0; --i) v = v << 8 | d_[p_ + static_cast<std::size_t>(i)];
        p_ += 4;
        return v;
    }
    std::uint64_t u64() {
        const std::uint64_t lo = u32();
        return lo | static_cast<std::uint64_t>(u32()) << 32;
    }
    // A string: its length (u32) then its bytes (UTF-16LE in Unicode installers).
    Span bytes() {
        const std::uint32_t n = u32();
        need(n);
        const Span s = d_.subspan(p_, n);
        p_ += n;
        return s;
    }
    void strings(int n) {
        for (int i = 0; i < n; ++i) (void)bytes();
    }
    void skip(std::size_t n) {
        need(n);
        p_ += n;
    }
    // A Delphi set of `n` flags: one bit each, in bytes, 3 bytes padded to 4.
    void flags(int n) {
        const std::size_t b = static_cast<std::size_t>((n + 7) / 8);
        skip(b == 3 ? 4 : b);
    }
    std::size_t pos() const { return p_; }

private:
    void need(std::size_t n) const {
        if (n > d_.size() - p_) fail("the setup headers end early (an unexpected format?)");
    }
    Span d_;
    std::size_t p_ = 0;
};

// Reads a set's flags one by one, as innoextract's stored_flag_reader.
class FlagReader {
public:
    explicit FlagReader(Reader& r) : r_(r) {}
    bool next() {
        if (pos_ == 0) {
            ++bytes_;
            buf_ = r_.u8();
        }
        const bool v = (buf_ >> pos_) & 1;
        pos_ = (pos_ + 1) % 8;
        return v;
    }
    void end() {
        if (bytes_ == 3) (void)r_.u8();
    }

private:
    Reader& r_;
    int pos_ = 0, bytes_ = 0;
    std::uint8_t buf_ = 0;
};

std::string toUtf8(Span s, bool utf16) {
    std::string out;
    auto put = [&](unsigned c) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xc0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xe0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
    };
    if (!utf16) {
        for (std::uint8_t b : s) put(b);   // (Windows-1252, as Latin-1)
        return out;
    }
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) {
        unsigned c = static_cast<unsigned>(s[i] | s[i + 1] << 8);
        if (c >= 0xd800 && c < 0xdc00 && i + 3 < s.size()) {
            const unsigned lo = static_cast<unsigned>(s[i + 2] | s[i + 3] << 8);
            if (lo >= 0xdc00 && lo < 0xe000) {
                c = 0x10000 + ((c - 0xd800) << 10) + (lo - 0xdc00);
                i += 2;
            }
        }
        put(c);
    }
    return out;
}

std::uint32_t le32(Span d, std::size_t p) {
    return static_cast<std::uint32_t>(d[p]) | static_cast<std::uint32_t>(d[p + 1]) << 8 |
           static_cast<std::uint32_t>(d[p + 2]) << 16 | static_cast<std::uint32_t>(d[p + 3]) << 24;
}

// The setup loader's table (in the executable's resources): where the headers
// and the data are.
struct Offsets {
    std::uint32_t header = 0, data = 0;
};

Offsets findOffsets(Span f) {
    // Setup loaders from 5.1.5 on (innoextract: loader/offsets.cpp).
    static const std::uint8_t kMagics[][12] = {
        {'r', 'D', 'l', 'P', 't', 'S', 0xcd, 0xe6, 0xd7, 0x7b, 0x0b, 0x2a},
        {'n', 'S', '5', 'W', '7', 'd', 'T', 0x83, 0xaa, 0x1b, 0x0f, 0x6a},
    };
    for (const auto& magic : kMagics) {
        auto it = f.begin();
        while ((it = std::search(it, f.end(), std::begin(magic), std::end(magic))) != f.end()) {
            const std::size_t p = static_cast<std::size_t>(it - f.begin());
            ++it;
            // magic, revision, ?, exe offset, exe size, exe CRC, header offset, data offset, CRC
            if (p + 12 + 8 * 4 > f.size()) break;
            if (crc32(f.subspan(p, 12 + 7 * 4)) != le32(f, p + 12 + 7 * 4)) continue;
            return {le32(f, p + 12 + 5 * 4), le32(f, p + 12 + 6 * 4)};
        }
    }
    fail("no setup loader table found (an older Inno Setup, before 5.1.5?)");
}

// A compressed header block (innoextract: stream/block.cpp): its data, and where
// the next one starts.
Bytes readBlock(Span f, std::size_t& pos) {
    if (pos + 9 > f.size()) fail("the setup headers are cut short");
    const std::uint32_t crc = le32(f, pos);
    if (archive::crc32(f.subspan(pos + 4, 5)) != crc) fail("bad header block checksum");
    const std::uint32_t stored = le32(f, pos + 4);
    const bool compressed = f[pos + 8] != 0;
    std::size_t p = pos + 9;
    if (stored > f.size() - p) fail("the setup headers are cut short");
    const std::size_t end = p + stored;
    // 4 KiB sub-blocks, each after its CRC32.
    Bytes raw;
    raw.reserve(stored);
    while (p < end) {
        if (end - p < 5) fail("bad header block");
        const std::uint32_t c = le32(f, p);
        const std::size_t n = std::min<std::size_t>(4096, end - p - 4);
        const Span chunk = f.subspan(p + 4, n);
        if (crc32(chunk) != c) fail("bad header block checksum");
        raw.insert(raw.end(), chunk.begin(), chunk.end());
        p += 4 + n;
    }
    pos = end;
    return compressed ? lzmaDecode(raw) : raw;
}

// Inno Setup's CALL/JMP filter on executables (5.2.0 on; 5.3.9 on flips the high
// byte too): innoextract's inno_exe_decoder_5200, on a whole file.
void unfilterCalls(Bytes& d, bool flipHighByte) {
    const std::size_t n = d.size();
    std::size_t i = 0;
    while (i < n) {
        const std::uint8_t b = d[i++];
        if (b != 0xe8 && b != 0xe9) continue;
        const std::size_t blockLeft = 0x10000 - ((i - 1) % 0x10000);
        if (blockLeft < 5) continue;   // (instructions across 64 KiB blocks are left alone)
        if (i + 4 > n) break;
        if (d[i + 3] == 0x00 || d[i + 3] == 0xff) {
            const std::uint32_t addr = static_cast<std::uint32_t>(i + 4) & 0xffffff;
            std::uint32_t rel = static_cast<std::uint32_t>(d[i] | d[i + 1] << 8 | d[i + 2] << 16);
            rel -= addr;
            d[i] = static_cast<std::uint8_t>(rel);
            d[i + 1] = static_cast<std::uint8_t>(rel >> 8);
            d[i + 2] = static_cast<std::uint8_t>(rel >> 16);
            if (flipHighByte && (rel & 0x800000)) d[i + 3] = static_cast<std::uint8_t>(~d[i + 3]);
        }
        i += 4;
    }
}

enum Compression { kStored, kZlib, kBzip2, kLzma1, kLzma2 };

struct DataEntry {
    std::uint32_t chunkOffset = 0;
    std::uint64_t chunkSize = 0, fileOffset = 0, fileSize = 0;
    bool compressed = false, encrypted = false, callFilter = false;
};

// The installer's data, shared by the entries' readers. The last chunk unpacked
// is kept: files are read in the installer's order, so each chunk is unpacked once.
struct Installer {
    Shared file;
    std::uint32_t version = 0;
    std::uint32_t dataOffset = 0;
    Compression compression = kStored;
    std::vector<DataEntry> data;
    std::map<std::uint32_t, std::uint64_t> chunkNeeds;   // chunk offset -> bytes needed
    std::mutex lock;
    std::uint32_t cachedChunk = 0xffffffff;
    Bytes cached;

    // `galaxyPart`: a part of a GOG Galaxy file, deflated (zlib) instead of filtered.
    Bytes read(std::size_t location, const std::string& name, bool galaxyPart = false) {
        const DataEntry& e = data.at(location);
        if (e.encrypted) fail(name + " is encrypted");
        std::lock_guard<std::mutex> g(lock);
        if (cachedChunk != e.chunkOffset) {
            cached.clear();
            cachedChunk = 0xffffffff;
            const Span f(*file);
            const std::uint64_t at = std::uint64_t(dataOffset) + e.chunkOffset;
            if (at + 4 > f.size() || std::memcmp(f.data() + at, "zlb\x1a", 4) != 0) fail("bad chunk for " + name);
            if (e.chunkSize > f.size() - at - 4) fail("chunk cut short for " + name);
            const Span in = f.subspan(static_cast<std::size_t>(at + 4), static_cast<std::size_t>(e.chunkSize));
            const std::size_t need = static_cast<std::size_t>(chunkNeeds[e.chunkOffset]);
            if (!e.compressed || compression == kStored) cached.assign(in.begin(), in.end());
            else if (compression == kLzma1) cached = lzmaDecode(in, need);
            else if (compression == kLzma2) cached = lzma2Decode(in, need);
            else if (compression == kZlib) cached = inflate(in, true);
            else fail("bzip2-compressed data isn't supported");
            cachedChunk = e.chunkOffset;
        }
        if (e.fileOffset > cached.size() || e.fileSize > cached.size() - e.fileOffset)
            fail(name + " is cut short");
        Bytes out(cached.begin() + static_cast<std::ptrdiff_t>(e.fileOffset),
                  cached.begin() + static_cast<std::ptrdiff_t>(e.fileOffset + e.fileSize));
        if (galaxyPart) return inflate(out, true);
        if (e.callFilter && version >= ver(5, 2, 0)) unfilterCalls(out, version >= ver(5, 3, 9));
        return out;
    }
};

// GOG Galaxy installers (GOG's "v2" ones) install each file from parts under
// {tmp}, named after their MD5, deflated: the first part's entry calls
// before_install('<md5>', '<name>', <parts>) and each part's after_install('<md5>',
// <size>, <unpacked size>) (innoextract: cli/goggalaxy.cpp). The arguments of
// `code` if it calls `name`, else none.
std::vector<std::string> parseCall(const std::string& code, std::string_view name) {
    std::vector<std::string> args;
    static const char kSpace[] = " \t\r\n";
    std::size_t p = code.find_first_not_of(kSpace);
    if (p == std::string::npos || code.compare(p, name.size(), name) != 0) return {};
    p = code.find_first_not_of(kSpace, p + name.size());
    if (p == std::string::npos || code[p] != '(') return {};
    ++p;
    for (;;) {
        p = code.find_first_not_of(kSpace, p);
        if (p == std::string::npos) return {};
        std::string arg;
        if (code[p] == '\'') {   // a quoted string ('' for a quote)
            for (++p;;) {
                const std::size_t q = code.find('\'', p);
                if (q == std::string::npos) return {};
                arg += code.substr(p, q - p);
                p = q + 1;
                if (p < code.size() && code[p] == '\'') {
                    arg += '\'';
                    ++p;
                } else {
                    break;
                }
            }
        } else {
            const std::size_t e = code.find_first_of(" \t\r\n(),'", p);
            if (e == std::string::npos) return {};
            arg = code.substr(p, e - p);
            p = e;
        }
        args.push_back(std::move(arg));
        p = code.find_first_not_of(kSpace, p);
        if (p == std::string::npos) return {};
        if (code[p] == ')') return args;
        if (code[p] != ',') return {};
        ++p;
    }
}

}  // namespace

bool isInnoSetup(Span head) {
    if (head.size() < 2 || head[0] != 'M' || head[1] != 'Z') return false;
    static const std::string_view kMark = "Inno Setup";
    const Span h = head.first(std::min<std::size_t>(head.size(), 0x200000));
    return std::search(h.begin(), h.end(), kMark.begin(), kMark.end()) != h.end();
}

Entries innoEntries(Shared file) {
    const Span f(*file);
    const Offsets off = findOffsets(f);
    if (off.header == 0 || off.header + 64 > f.size()) fail("bad setup loader table");

    // The setup data version: "Inno Setup Setup Data (5.6.2) (u)".
    const std::string vs(reinterpret_cast<const char*>(f.data() + off.header), 64);
    const std::string_view kPrefix = "Inno Setup Setup Data (";
    if (vs.compare(0, kPrefix.size(), kPrefix) != 0) fail("unknown setup data: " + vs.substr(0, vs.find('\0')));
    unsigned part[4] = {0, 0, 0, 0};
    std::size_t p = kPrefix.size();
    for (int i = 0; i < 4 && p < vs.size(); ++i) {
        while (p < vs.size() && vs[p] >= '0' && vs[p] <= '9') part[i] = part[i] * 10 + unsigned(vs[p++] - '0');
        if (p >= vs.size() || vs[p] != '.') break;
        ++p;
    }
    std::uint32_t v = ver(part[0], part[1], part[2], part[3]);
    if (v == ver(5, 5, 8)) v = ver(5, 5, 7);   // (an unofficial 5.5.7)
    const bool unicode = vs.find("(u)") != std::string::npos || vs.find("(U)") != std::string::npos ||
                         v >= ver(6, 0, 0);
    if (v < ver(5, 5, 0) || v >= ver(6, 5, 0))
        fail("setup data version " + versionText(v) + " isn't supported");
    if (!off.data) fail("the data is in separate setup-*.bin files, which aren't supported");

    std::size_t pos = off.header + 64;
    const Bytes headers = readBlock(f, pos);
    const Bytes dataHeaders = readBlock(f, pos);

    // ---- The setup header (innoextract: setup/header.cpp), versions 5.5.0 on.
    Reader r(headers);
    int strings = 27;   // app_name ... close_applications_filter
    if (v >= ver(5, 5, 6)) strings += 1;   // setup_mutex
    if (v >= ver(5, 6, 1)) strings += 2;   // changes_environment, changes_associations
    if (v >= ver(6, 3, 0)) strings += 2;   // architecture expressions
    strings += 4;                          // license, info before / after, compiled code
    r.strings(strings);
    if (!unicode) r.skip(32);   // lead bytes
    const std::uint32_t nLanguages = r.u32(), nMessages = r.u32(), nPermissions = r.u32(), nTypes = r.u32(),
                        nComponents = r.u32(), nTasks = r.u32(), nDirectories = r.u32(), nFiles = r.u32(),
                        nData = r.u32();
    r.skip(7 * 4);    // icons, ini, registry, delete, uninstall delete, run, uninstall run
    r.skip(20);       // Windows version range
    if (v < ver(6, 4, 0, 1)) r.skip(8);   // back colours
    if (v < ver(5, 5, 7)) r.skip(4);      // image back colour
    if (v >= ver(6, 0, 0)) r.skip(9);     // wizard style, resize percentages
    if (v >= ver(5, 5, 7)) r.skip(1);     // image alpha format
    if (v >= ver(6, 4, 0)) r.skip(4 + 44);   // password hash, salt
    else r.skip(20 + 8);
    r.skip(8 + 4);   // extra disk space, slices per disk
    r.skip(3);       // uninstall log mode, dir exists warning, privileges required
    if (v >= ver(5, 7, 0)) r.flags(2);   // privileges required overrides
    r.skip(2);       // show language dialog, language detection
    const std::uint8_t compression = r.u8();
    if (v < ver(6, 3, 0)) r.skip(2);   // architectures allowed / installed in 64-bit mode
    r.skip(2);       // disable dir page, disable program group page
    r.skip(8);       // uninstall display size
    {
        int n = 39;   // the options every 5.5.0+ has
        if (v < ver(6, 4, 0, 1)) n += 5;   // window flags, back colour horizontal
        if (v < ver(5, 6, 1)) n += 2;      // changes associations, changes environment
        if (!unicode) n += 1;              // show undisplayable languages
        if (v >= ver(5, 5, 7)) n += 1;     // force close applications
        if (v >= ver(6, 0, 0)) n += 3;     // app name has consts, use previous privileges, wizard resizable
        if (v >= ver(6, 3, 0)) n += 1;     // uninstall logging
        r.flags(n);
    }

    // ---- The entries before the files (setup/*.cpp), skipped.
    for (std::uint32_t i = 0; i < nLanguages; ++i) {
        r.strings(10);
        r.skip(4);                // language id
        if (!unicode) r.skip(4);  // codepage
        r.skip(4 * 4 + 1);        // font sizes, right to left
    }
    for (std::uint32_t i = 0; i < nMessages; ++i) {
        r.strings(2);
        r.skip(4);
    }
    for (std::uint32_t i = 0; i < nPermissions; ++i) r.strings(1);
    for (std::uint32_t i = 0; i < nTypes; ++i) {
        r.strings(4);
        r.skip(20);
        r.flags(1);
        r.skip(1 + 8);
    }
    for (std::uint32_t i = 0; i < nComponents; ++i) {
        r.strings(5);
        r.skip(8 + 4 + 1 + 20);
        r.flags(5);
        r.skip(8);
    }
    for (std::uint32_t i = 0; i < nTasks; ++i) {
        r.strings(6);
        r.skip(4 + 1 + 20);
        r.flags(5);
    }
    for (std::uint32_t i = 0; i < nDirectories; ++i) {
        r.strings(1 + 6);
        r.skip(4 + 20 + 2);
        r.flags(5);
    }

    // ---- The files: where each goes, and which data entry holds it.
    struct FileEntry {
        std::string dest;
        std::uint32_t location;
        std::string afterInstall, beforeInstall;
    };
    std::vector<FileEntry> files;
    for (std::uint32_t i = 0; i < nFiles; ++i) {
        (void)r.bytes();   // source
        const std::string dest = toUtf8(r.bytes(), unicode);
        r.strings(2 + 4);   // font name, assembly name; components, tasks, languages, check
        const std::string after = toUtf8(r.bytes(), unicode), before = toUtf8(r.bytes(), unicode);
        r.skip(20);         // Windows versions
        const std::uint32_t location = r.u32();
        r.skip(4 + 8 + 2);  // attributes, external size, permission
        r.flags(32);
        r.skip(1);          // type
        files.push_back({dest, location, after, before});
    }

    // ---- The data entries (setup/data.cpp), from the second block.
    auto inst = std::make_shared<Installer>();
    inst->file = file;
    inst->version = v;
    inst->dataOffset = off.data;
    if (compression > kLzma2) fail("unknown compression method");
    inst->compression = static_cast<Compression>(compression);
    Reader dr(dataHeaders);
    for (std::uint32_t i = 0; i < nData; ++i) {
        DataEntry e;
        const std::uint32_t firstSlice = dr.u32(), lastSlice = dr.u32();
        if (firstSlice != 0 || lastSlice != 0) fail("multi-part installers aren't supported");
        e.chunkOffset = dr.u32();
        e.fileOffset = dr.u64();
        e.fileSize = dr.u64();
        e.chunkSize = dr.u64();
        dr.skip(v >= ver(6, 4, 0) ? 32 : 20);   // SHA-256 / SHA-1
        dr.skip(8 + 8);                         // file time, file version
        FlagReader fl(dr);
        (void)fl.next();   // version info valid
        (void)fl.next();   // version info not valid
        (void)fl.next();   // time stamp in UTC
        (void)fl.next();   // is uninstaller exe
        e.callFilter = fl.next();
        (void)fl.next();   // touch
        e.encrypted = fl.next();
        e.compressed = fl.next();
        (void)fl.next();   // solid break
        if (v >= ver(5, 5, 7) && v < ver(6, 3, 0)) {
            (void)fl.next();   // sign
            (void)fl.next();   // sign once
        }
        fl.end();
        if (v >= ver(6, 3, 0)) dr.skip(1);   // sign mode
        std::uint64_t& need = inst->chunkNeeds[e.chunkOffset];
        need = std::max(need, e.fileOffset + e.fileSize);
        inst->data.push_back(e);
    }

    Entries out;
    auto clean = [](std::string p) {
        std::replace(p.begin(), p.end(), '\\', '/');
        return p;
    };
    for (std::size_t i = 0; i < files.size(); ++i) {
        const FileEntry& fe = files[i];
        if (fe.location >= inst->data.size()) continue;   // (not from the installer's data)
        const auto start = parseCall(fe.beforeInstall, "before_install").empty()
                               ? parseCall(fe.beforeInstall, "before_install_dependency")
                               : parseCall(fe.beforeInstall, "before_install");
        if (start.size() >= 2 && !start[1].empty()) {
            // A GOG Galaxy file: this entry and the next ones are its parts.
            std::size_t parts = 1;
            if (start.size() >= 3) parts = std::max<std::size_t>(1, std::strtoul(start[2].c_str(), nullptr, 10));
            std::vector<std::size_t> locations;
            for (std::size_t k = i; k < files.size() && locations.size() < parts; ++k) {
                if (files[k].location >= inst->data.size()) break;
                locations.push_back(files[k].location);
            }
            if (locations.size() != parts) fail("incomplete GOG Galaxy file " + start[1]);
            i += parts - 1;
            const std::string path = "{app}/" + clean(start[1]);
            out.push_back({path, [inst, locations, path] {
                               Bytes all;
                               for (std::size_t loc : locations) {
                                   const Bytes part = inst->read(loc, path, true);
                                   all.insert(all.end(), part.begin(), part.end());
                               }
                               return all;
                           }});
            continue;
        }
        const std::string path = clean(fe.dest);
        const std::size_t location = fe.location;
        out.push_back({path, [inst, location, path] { return inst->read(location, path); }});
    }
    return out;
}

}  // namespace reaction::archive
