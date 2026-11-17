// reaction-tool import SOURCE OUTDIR [--compare DIR] [--quiet]
// What the setup screen's "Choose game files..." does (formats/GameImport.h):
// the game from any of its releases into OUTDIR (replaced only if the import
// works). --compare prints which files match, differ or are missing against
// DIR, as covert-decomp's extract_game.py does.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <thread>
#include <chrono>

#include "Tool.h"
#include "formats/GameImport.h"

using namespace reaction;
namespace fs = std::filesystem;

namespace {

std::map<std::string, std::string> readDir(const fs::path& dir) {
    std::map<std::string, std::string> out;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        std::string name = e.path().filename().string();
        for (auto& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        std::ifstream in(e.path(), std::ios::binary);
        out[name] = std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    return out;
}

void compare(const fs::path& outDir, const fs::path& refDir) {
    const auto files = readDir(outDir), ref = readDir(refDir);
    std::string same, diff, missing, extra;
    int nSame = 0, nDiff = 0, nMissing = 0, nExtra = 0;
    for (const auto& [n, d] : files) {
        auto it = ref.find(n);
        if (it == ref.end()) extra += " " + n, ++nExtra;
        else if (it->second == d) ++nSame;
        else diff += " " + n, ++nDiff;
    }
    for (const auto& [n, d] : ref)
        if (!files.count(n)) missing += " " + n, ++nMissing;
    std::printf("  vs %s: %d identical, %d differ, %d missing, %d extra\n", refDir.string().c_str(), nSame, nDiff,
                nMissing, nExtra);
    if (nDiff) std::printf("    differ:%s\n", diff.c_str());
    if (nMissing) std::printf("    missing:%s\n", missing.c_str());
    if (nExtra) std::printf("    extra:%s\n", extra.c_str());
}

}  // namespace

REACTION_COMMAND_NO_DATA(
    "import", "import SOURCE OUTDIR [--compare DIR] [--quiet]: the game from an installer, archive, disk image or folder",
    [](const GameData&, const std::vector<std::string>& a) {
        std::vector<std::string> pos;
        std::string ref;
        bool quiet = false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] == "--compare" && i + 1 < a.size()) ref = a[++i];
            else if (a[i] == "--quiet") quiet = true;
            else pos.push_back(a[i]);
        }
        if (pos.size() != 2) return 2;
        std::printf("%s: %s\n", pos[0].c_str(), describeGameSource(detectGameSource(pos[0])));
        // (As the setup screen runs it: on a thread, polled.)
        ImportJob job(pos[0], pos[1]);
        std::string last;
        while (!job.done()) {
            if (const std::string s = job.stage(); !quiet && s != last && !s.empty())
                std::fprintf(stderr, "  %3d%%  %s\n", static_cast<int>(job.progress() * 100), s.c_str()), last = s;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        const ImportResult& r = job.result();
        if (!r.ok) {
            std::fprintf(stderr, "%s: %s\n", pos[0].c_str(), r.error.c_str());
            return 1;
        }
        std::printf("%s (%s): %zu files -> %s\n", pos[0].c_str(), describeGameSource(r.source), r.files,
                    pos[1].c_str());
        if (r.check.ok()) std::printf("  all %zu known files match a known release\n", r.check.checked);
        else std::printf("  check: %s\n", r.check.describe().c_str());
        if (!r.check.damaged.empty()) {
            std::printf("    damaged:");
            for (const auto& f : r.check.damaged) std::printf(" %s", f.c_str());
            std::printf("\n");
        }
        if (!ref.empty()) compare(pos[1], ref);
        return 0;
    });

#include "formats/Archive.h"
#include "formats/InnoSetup.h"

REACTION_COMMAND_NO_DATA("archive-ls", "archive-ls FILE [OUTDIR]: list (or unpack into OUTDIR) the files of an installer or archive (Inno Setup, xar, 7z, zip, FAT image)",
                         [](const GameData&, const std::vector<std::string>& a) {
                             if (a.empty() || a.size() > 2) return 2;
                             std::ifstream in(a[0], std::ios::binary);
                             auto data = std::make_shared<const archive::Bytes>(std::istreambuf_iterator<char>(in),
                                                                                std::istreambuf_iterator<char>());
                             archive::Entries entries;
                             if (archive::isXar(*data)) entries = archive::xarEntries(data);
                             else if (archive::is7z(*data)) entries = archive::sevenZipEntries(data);
                             else if (archive::isInnoSetup(*data)) entries = archive::innoEntries(data);
                             else if (archive::isFatImage(*data)) entries = archive::fatEntries(data);
                             else entries = archive::zipEntries(data);
                             for (const auto& e : entries) {
                                 std::printf("%s\n", e.path.c_str());
                                 if (a.size() < 2) continue;
                                 const fs::path out = fs::path(a[1]) / e.path;
                                 fs::create_directories(out.parent_path());
                                 const archive::Bytes b = e.read();
                                 std::ofstream(out, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
                             }
                             return 0;
                         });

#include "audio/Synth.h"

REACTION_COMMAND_NO_DATA("import-roms", "import-roms SOURCE OUTDIR: the best MT-32 control + PCM ROM pair from a zip, 7z, folder or ROM file",
                         [](const GameData&, const std::vector<std::string>& a) {
                             if (a.size() != 2) return 2;
                             const audio::Mt32ImportResult r = audio::importMt32Roms(a[0], a[1]);
                             if (!r.ok) {
                                 std::fprintf(stderr, "%s: %s\n", a[0].c_str(), r.error.c_str());
                                 return 1;
                             }
                             std::printf("%s: %s -> %s\n", a[0].c_str(), r.roms->describe().c_str(), a[1].c_str());
                             for (const auto* rom : {&r.roms->control, &r.roms->pcm})
                                 for (const auto& f : rom->files) std::printf("  %s\n", f.filename().string().c_str());
                             return 0;
                         });
