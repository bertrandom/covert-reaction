#include "formats/GameImport.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include "core/GameData.h"
#include "formats/Archive.h"
#include "formats/InnoSetup.h"

namespace reaction {

namespace fs = std::filesystem;
using archive::Bytes;
using archive::Entries;
using archive::Entry;
using archive::Shared;

namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error(what); }

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string utf8(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// A DOS 8.3 name, as extract_game.py's DOS_NAME (already upper case).

Bytes readFile(const fs::path& p, std::size_t limit = SIZE_MAX) {
    std::ifstream in(p, std::ios::binary);
    if (!in) fail("can't open " + utf8(p));
    Bytes b;
    if (limit == SIZE_MAX) {
        b.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    } else {
        b.resize(limit);
        in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(limit));
        b.resize(static_cast<std::size_t>(in.gcount()));
    }
    return b;
}

void report(const ImportProgress& progress, double f, const std::string& stage) {
    if (progress) progress(std::clamp(f, 0.0, 1.0), stage);
}

// Reads `picked` into `out` (progress from `from` to `to`).
void readAll(const std::vector<std::pair<std::string, const Entry*>>& picked, GameFiles& out,
             const ImportProgress& progress, double from, double to) {
    const double n = static_cast<double>(std::max<std::size_t>(picked.size(), 1));
    std::size_t i = 0;
    for (const auto& [name, e] : picked) {
        report(progress, from + (to - from) * static_cast<double>(i++) / n, "Unpacking " + name);
        out[name] = e->read();
    }
}

// extract_game.py's game_dir: the shallowest folder with GAME.EXE and COVERT.EXE
// (or the data files), and of its files those Covert Reaction reads. False if
// there's none.
bool gameDir(const Entries& files, GameFiles& out, const ImportProgress& progress, double from, double to) {
    std::map<std::string, std::map<std::string, const Entry*>> dirs;
    std::vector<std::string> order;   // (the folders as found: a tie goes to the first)
    for (const auto& e : files) {
        const std::size_t slash = e.path.rfind('/');
        const std::string d = slash == std::string::npos ? std::string() : e.path.substr(0, slash);
        const std::string name = upper(slash == std::string::npos ? e.path : e.path.substr(slash + 1));
        if (!dirs.count(d)) order.push_back(d);
        dirs[d][name] = &e;
    }
    const std::string* best = nullptr;
    for (const auto& d : order) {
        const auto& names = dirs[d];
        // An installed game (its programs), or a copy Covert Reaction made (its
        // data files only: the setup screen's data/game, copied elsewhere).
        const bool game = (names.count("GAME.EXE") && names.count("COVERT.EXE")) ||
                          (names.count("TEXT.DTA") && names.count("TITLE2.PAN") && names.count("FACES.PIC"));
        if (game && (!best || d.size() < best->size())) best = &d;
    }
    if (!best) return false;
    std::vector<std::pair<std::string, const Entry*>> picked;
    for (const auto& [name, e] : dirs[*best])
        if (isGameDataFile(name)) picked.emplace_back(name, e);   // (what the port reads, no more)
    readAll(picked, out, progress, from, to);
    return true;
}

// A number in a disk image's name (disk1, disk2...), for their order.
std::pair<unsigned long long, std::string> diskOrder(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    unsigned long long n = 0;
    for (char c : base)
        if (c >= '0' && c <= '9') n = n > 1000000000000ULL ? n : n * 10 + static_cast<unsigned>(c - '0');
    return {n, path};
}

// extract_game.py's from_floppies: the disks' root files, in the disks' order.
GameFiles fromFloppies(std::vector<std::pair<std::string, Shared>> images, const ImportProgress& progress,
                       double from, double to) {
    std::sort(images.begin(), images.end(),
              [](const auto& a, const auto& b) { return diskOrder(a.first) < diskOrder(b.first); });
    GameFiles out;
    double i = 0;
    for (const auto& [name, img] : images) {
        report(progress, from + (to - from) * i++ / static_cast<double>(images.size()), "Reading the disk " + name);
        for (const auto& e : archive::fatEntries(img))
            if (e.path.find('/') == std::string::npos && isGameDataFile(upper(e.path))) out[upper(e.path)] = e.read();
    }
    return out;
}

// The disk images among `files` (read only to tell).
std::vector<std::pair<std::string, Shared>> imagesIn(const Entries& files) {
    std::vector<std::pair<std::string, Shared>> out;
    for (const auto& e : files) {
        auto b = std::make_shared<const Bytes>(e.read());
        if (archive::isFatImage(*b)) out.emplace_back(e.path, std::move(b));
    }
    return out;
}

// A folder's files (as extract_game.py's read_tree), read when asked for.
Entries folderEntries(const fs::path& root) {
    Entries out;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) fail("can't read the folder " + utf8(root));
    std::size_t seen = 0;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (++seen > 100000) fail("that folder holds too many files: choose the game's own folder");
        if (it.depth() > 12) {
            it.disable_recursion_pending();
            continue;
        }
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        const fs::path p = it->path();
        std::string rel = utf8(p.lexically_relative(root));
        std::replace(rel.begin(), rel.end(), '\\', '/');
        out.push_back({rel, [p] { return readFile(p); }});
    }
    return out;
}

GameFiles collect(const fs::path& source, GameSource& kind, const ImportProgress& progress) {
    std::error_code ec;
    if (fs::is_directory(source, ec)) {
        kind = GameSource::Folder;
        report(progress, 0.0, "Looking at the folder");
        const Entries files = folderEntries(source);
        GameFiles out;
        if (gameDir(files, out, progress, 0.05, 0.9)) return out;
        // Disk images? (Only files small enough to be one are read.)
        Entries small;
        for (const auto& e : files) {
            const fs::path p = source / fs::path(std::u8string(e.path.begin(), e.path.end()));
            std::error_code ec2;
            const auto size = fs::file_size(p, ec2);
            if (!ec2 && size >= 512 && size <= 4u * 1024 * 1024 && archive::isFatImage(readFile(p, 512)))
                small.push_back(e);
        }
        if (auto images = imagesIn(small); !images.empty()) return fromFloppies(std::move(images), progress, 0.1, 0.9);
        fail("no Covert Action files in that folder (the folder with GAME.EXE and COVERT.EXE, or disk images)");
    }
    if (!fs::is_regular_file(source, ec)) fail("can't find " + utf8(source));
    report(progress, 0.0, "Reading the file");
    const Shared data = std::make_shared<const Bytes>(readFile(source));
    const Bytes& d = *data;
    GameFiles out;
    if (archive::isXar(d)) {
        kind = GameSource::GogMac;
        report(progress, 0.05, "Unpacking the package");
        Entries files;
        for (const auto& e : archive::xarEntries(data)) {
            Bytes raw = e.read();   // (Payload, Scripts: gzip'd cpio)
            if (archive::isGzip(raw)) raw = archive::gunzip(raw);
            if (raw.size() >= 5 && std::memcmp(raw.data(), "07070", 5) == 0) {
                for (auto& c : archive::cpioEntries(std::make_shared<const Bytes>(std::move(raw)))) {
                    c.path = e.path + "/" + c.path;
                    files.push_back(std::move(c));
                }
            } else {
                auto b = std::make_shared<const Bytes>(std::move(raw));
                files.push_back({e.path, [b] { return *b; }});
            }
        }
        if (gameDir(files, out, progress, 0.4, 0.9)) return out;
    } else if (archive::is7z(d)) {
        kind = GameSource::SevenZip;
        report(progress, 0.05, "Opening the 7z archive");
        const Entries files = archive::sevenZipEntries(data);
        if (gameDir(files, out, progress, 0.1, 0.9)) return out;
        if (auto images = imagesIn(files); !images.empty()) return fromFloppies(std::move(images), progress, 0.5, 0.9);
    } else if (archive::isInnoSetup(d)) {
        kind = GameSource::GogWindows;
        report(progress, 0.05, "Reading the installer");
        const Entries files = archive::innoEntries(data);
        if (gameDir(files, out, progress, 0.1, 0.9)) return out;
    } else if (archive::isFatImage(d)) {
        kind = GameSource::DiskImage;
        return fromFloppies({{utf8(source.filename()), data}}, progress, 0.1, 0.9);
    } else if (archive::hasZip(d)) {
        kind = d.size() >= 2 && d[0] == '#' && d[1] == '!' ? GameSource::GogLinux : GameSource::Zip;
        report(progress, 0.05, "Opening the archive");
        const Entries files = archive::zipEntries(data);
        if (gameDir(files, out, progress, 0.1, 0.9)) return out;
    } else {
        kind = GameSource::Unknown;
        fail("not a file of the game's releases (GOG's installers, eXoDOS's zip, the floppy disks)");
    }
    fail(std::string(describeGameSource(kind)) + ", but without Covert Action's files in it");
}

}  // namespace

GameSource detectGameSource(const fs::path& source) {
    std::error_code ec;
    if (fs::is_directory(source, ec)) return GameSource::Folder;
    if (!fs::is_regular_file(source, ec)) return GameSource::Unknown;
    try {
        const Bytes head = readFile(source, 0x200000);
        if (archive::isXar(head)) return GameSource::GogMac;
        if (archive::is7z(head)) return GameSource::SevenZip;
        if (archive::isInnoSetup(head)) return GameSource::GogWindows;
        if (archive::isFatImage(head)) return GameSource::DiskImage;
        // A zip's directory is at its end.
        const auto size = fs::file_size(source, ec);
        if (ec) return GameSource::Unknown;
        Bytes tail;
        if (size <= head.size()) {
            tail = head;
        } else {
            std::ifstream in(source, std::ios::binary);
            const std::uint64_t n = std::min<std::uint64_t>(size, 65536 + 22);
            in.seekg(static_cast<std::streamoff>(size - n));
            tail.resize(static_cast<std::size_t>(n));
            in.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(n));
            tail.resize(static_cast<std::size_t>(in.gcount()));
        }
        if (archive::hasZip(tail))
            return head.size() >= 2 && head[0] == '#' && head[1] == '!' ? GameSource::GogLinux : GameSource::Zip;
    } catch (const std::exception&) {
    }
    return GameSource::Unknown;
}

const char* describeGameSource(GameSource kind) {
    switch (kind) {
    case GameSource::Folder: return "a folder";
    case GameSource::GogWindows: return "GOG's Windows installer";
    case GameSource::GogMac: return "GOG's macOS installer";
    case GameSource::GogLinux: return "GOG's Linux installer";
    case GameSource::Zip: return "a zip archive";
    case GameSource::SevenZip: return "a 7z archive";
    case GameSource::DiskImage: return "a floppy disk image";
    case GameSource::Unknown: break;
    }
    return "an unknown file";
}

const std::vector<std::string>& importExtensions() {
    static const std::vector<std::string> kExt = {".exe", ".pkg", ".sh", ".zip", ".7z", ".img", ".ima", ".dsk", ".vfd"};
    return kExt;
}

bool hasImportExtension(const fs::path& file) {
    const std::string ext = upper(utf8(file.extension()));
    for (const auto& e : importExtensions())
        if (upper(e) == ext) return true;
    return false;
}

GameFiles collectGameFiles(const fs::path& source, GameSource* kind, const ImportProgress& progress) {
    GameSource k = GameSource::Unknown;
    try {
        GameFiles out = collect(source, k, progress);
        if (kind) *kind = k;
        return out;
    } catch (...) {
        if (kind) *kind = k;
        throw;
    }
}

ImportResult importGame(const fs::path& source, const fs::path& dest, const ImportProgress& progress) {
    ImportResult res;
    const fs::path tmp = fs::path(dest).concat(".new"), old = fs::path(dest).concat(".old");
    std::error_code ec;
    try {
        const GameFiles files = collectGameFiles(source, &res.source, progress);
        report(progress, 0.9, "Copying the files");
        if (dest.has_parent_path()) fs::create_directories(dest.parent_path(), ec);
        fs::remove_all(tmp, ec);
        if (!fs::create_directories(tmp, ec) || ec) fail("can't create the folder " + utf8(tmp));
        for (const auto& [name, data] : files) {
            std::ofstream out(tmp / name, std::ios::binary);
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!out) fail("can't write " + utf8(tmp / name));
        }
        // Every file the port reads must be there; one that doesn't match a known
        // release is only reported (res.check).
        res.check = checkGameFiles(tmp);
        if (!res.check.missing.empty()) {
            std::string m;
            for (const auto& f : res.check.missing) m += " " + f;
            fail("the game's files are incomplete (missing:" + m + ")");
        }
        // The swap: the old folder goes only once the new one is complete.
        fs::remove_all(old, ec);
        const bool hadOld = fs::exists(dest, ec);
        if (hadOld) {
            fs::rename(dest, old, ec);
            if (ec) fail("can't replace " + utf8(dest) + ": " + ec.message());
        }
        fs::rename(tmp, dest, ec);
        if (ec) {
            const std::string why = ec.message();
            if (hadOld) fs::rename(old, dest, ec);
            fail("can't create " + utf8(dest) + ": " + why);
        }
        fs::remove_all(old, ec);
        res.ok = true;
        res.files = files.size();
        report(progress, 1.0, "Done");
    } catch (const std::exception& e) {
        fs::remove_all(tmp, ec);
        res.ok = false;
        res.error = e.what();
    }
    return res;
}

ImportJob::ImportJob(fs::path source, fs::path dest, [[maybe_unused]] bool background)
    : source_(std::move(source)), dest_(std::move(dest)) {
#ifndef __EMSCRIPTEN__
    if (background) {
        thread_ = std::thread([this] { run(); });
        return;
    }
#endif
    run();
}

ImportJob::~ImportJob() {
#ifndef __EMSCRIPTEN__
    if (thread_.joinable()) thread_.join();
#endif
}

std::string ImportJob::stage() const {
    std::lock_guard<std::mutex> g(stageLock_);
    return stage_;
}

void ImportJob::run() {
    ImportResult r = importGame(source_, dest_, [this](double f, const std::string& s) {
        progress_.store(f);
        std::lock_guard<std::mutex> g(stageLock_);
        stage_ = s;
    });
    result_ = std::move(r);
    done_.store(true);
}

}  // namespace reaction
