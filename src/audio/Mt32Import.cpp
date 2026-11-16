// Importing the player's MT-32 ROMs (Synth.h, importMt32Roms): from a folder, a
// zip or a 7z (archives inside a folder or inside an archive too, one level), the
// files are told by their contents (libmt32emu's ROM list), the best control +
// PCM pair is picked as findMt32Roms does, and only that pair is copied.
#include <mt32emu/mt32emu.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <stdexcept>
#include <system_error>

#include "audio/Synth.h"
#include "formats/Archive.h"

namespace fs = std::filesystem;

namespace reaction::audio {
namespace {

using archive::Bytes;

constexpr std::uintmax_t kMaxArchive = 64u * 1024 * 1024;   // ROM archives are a few MB

bool romSize(std::uintmax_t n) {
    return n == 32768 || n == 65536 || n == 131072 || n == 262144 || n == 524288 || n == 1048576;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool archiveName(const std::string& name) {
    const std::string n = lower(name);
    auto ends = [&](const char* e) { return n.size() >= std::char_traits<char>::length(e) &&
                                            n.compare(n.size() - std::char_traits<char>::length(e), std::string::npos, e) == 0; };
    return ends(".zip") || ends(".7z");
}

Bytes readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("can't read " + p.filename().string());
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The ROMs found so far, one per libmt32emu ROM (the first copy found).
struct Collector {
    std::map<const MT32Emu::ROMInfo*, Bytes> roms;

    void file(Bytes data) {
        if (!romSize(data.size())) return;
        MT32Emu::ArrayFile f(data.data(), data.size());
        if (const MT32Emu::ROMInfo* info = MT32Emu::ROMInfo::getROMInfo(&f)) roms.try_emplace(info, std::move(data));
    }

    // A zip or 7z; `nested`: archives inside it are opened too.
    bool archive(Bytes data, bool nested) {
        auto shared = std::make_shared<const Bytes>(std::move(data));
        archive::Entries entries;
        if (archive::is7z(*shared)) entries = archive::sevenZipEntries(shared);
        else if (archive::hasZip(*shared)) entries = archive::zipEntries(shared);
        else return false;
        for (const auto& e : entries) {
            Bytes b = e.read();
            if (nested && archiveName(e.path) && b.size() <= kMaxArchive) {
                try {
                    if (archive(std::move(b), false)) continue;
                } catch (const std::exception&) {
                }
                continue;
            }
            file(std::move(b));
        }
        return true;
    }

    void folder(const fs::path& root) {
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        if (ec) throw std::runtime_error("can't read that folder");
        std::size_t seen = 0;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            if (++seen > 20000) throw std::runtime_error("that folder holds too many files: choose the ROMs' own folder");
            if (it.depth() > 6) {
                it.disable_recursion_pending();
                continue;
            }
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            const auto size = it->file_size(e2);
            if (e2) continue;
            try {
                if (romSize(size)) file(readFile(it->path()));
                else if (archiveName(it->path().filename().string()) && size <= kMaxArchive)
                    archive(readFile(it->path()), true);
            } catch (const std::exception&) {
                // (an unreadable file or a broken archive: skipped)
            }
        }
    }
};

}  // namespace

const std::vector<std::string>& mt32RomExtensions() {
    static const std::vector<std::string> v = {".zip", ".7z", ".rom", ".bin"};
    return v;
}

Mt32ImportResult importMt32Roms(const fs::path& source, const fs::path& dest) {
    Mt32ImportResult r;
    try {
        std::error_code ec;
        Collector c;
        if (fs::is_directory(source, ec)) {
            c.folder(source);
        } else if (fs::is_regular_file(source, ec)) {
            const auto size = fs::file_size(source, ec);
            if (!ec && romSize(size)) {
                // One ROM file: it and the ones next to it.
                for (const auto& e : fs::directory_iterator(source.parent_path(), ec)) {
                    std::error_code e2;
                    if (e.is_regular_file(e2) && romSize(e.file_size(e2))) c.file(readFile(e.path()));
                }
            } else if (!ec && size <= kMaxArchive) {
                if (!c.archive(readFile(source), true))
                    throw std::runtime_error("not a zip or 7z archive, a ROM file or a folder");
            } else {
                throw std::runtime_error("too big to be an archive of MT-32 ROMs");
            }
        } else {
            throw std::runtime_error("can't find " + source.filename().string());
        }
        if (c.roms.empty()) throw std::runtime_error("no MT-32 or CM-32L ROMs in it");

        // Every ROM found, named after libmt32emu's id, into a new folder; the
        // best pair is then picked there and the rest removed.
        const fs::path tmp = fs::path(dest).concat(".new"), old = fs::path(dest).concat(".old");
        fs::remove_all(tmp, ec);
        fs::create_directories(tmp, ec);
        if (ec) throw std::runtime_error("can't create " + tmp.string());
        for (const auto& [info, data] : c.roms) {
            std::ofstream out(tmp / (std::string(info->shortName) + ".rom"), std::ios::binary);
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!out) throw std::runtime_error("can't write the ROMs into " + tmp.string());
        }
        auto roms = findMt32Roms({tmp});
        if (!roms) {
            fs::remove_all(tmp, ec);
            std::string found;
            for (const auto& [info, data] : c.roms) found += std::string(found.empty() ? "" : ", ") + info->description;
            throw std::runtime_error("no complete control + PCM ROM pair in it (found: " + found + ")");
        }
        std::vector<fs::path> keep = roms->control.files;
        keep.insert(keep.end(), roms->pcm.files.begin(), roms->pcm.files.end());
        for (const auto& e : fs::directory_iterator(tmp, ec))
            if (std::find(keep.begin(), keep.end(), e.path()) == keep.end()) fs::remove(e.path(), ec);

        fs::remove_all(old, ec);
        const bool hadOld = fs::exists(dest, ec);
        if (hadOld) fs::rename(dest, old, ec);
        ec.clear();
        fs::rename(tmp, dest, ec);
        if (ec) {
            if (hadOld) fs::rename(old, dest, ec);
            fs::remove_all(tmp, ec);
            throw std::runtime_error("can't replace " + dest.string());
        }
        fs::remove_all(old, ec);
        r.roms = findMt32Roms({dest});
        r.ok = r.roms.has_value();
        if (!r.ok) r.error = "the ROMs could not be copied";
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

}  // namespace reaction::audio
