// The MT-32: libmt32emu (Munt, LGPL-2.1+, https://github.com/munt/munt), with
// the player's own control and PCM ROMs (copyrighted: never bundled).
#include <mt32emu/mt32emu.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>
#include <system_error>

#include "audio/Synth.h"

namespace fs = std::filesystem;

namespace reaction::audio {
namespace {

// Preferred control ROMs, best first: Covert Action (1990) was made for the
// original MT-32; the CM-32L / LAPC-I has extra sounds and different timbres.
constexpr std::array<const char*, 12> kControlOrder = {
    "ctrl_mt32_1_07", "ctrl_mt32_1_06", "ctrl_mt32_1_05", "ctrl_mt32_1_04", "ctrl_mt32_bluer",
    "ctrl_mt32_2_07", "ctrl_mt32_2_06", "ctrl_mt32_2_04", "ctrl_mt32_2_03",
    "ctrl_cm32l_1_02", "ctrl_cm32l_1_00", "ctrl_cm32ln_1_00",
};

bool romSize(std::uintmax_t n) {
    return n == 32768 || n == 65536 || n == 131072 || n == 262144 || n == 524288 || n == 1048576;
}

struct Found {
    fs::path file;
    const MT32Emu::ROMInfo* info;
};

// The full ROM `id`, from one file or from its two halves.
std::optional<Mt32Rom> assemble(const std::string& id, const std::vector<Found>& roms) {
    for (const auto& r : roms)
        if (r.info->pairType == MT32Emu::ROMInfo::Full && id == r.info->shortName)
            return Mt32Rom{{r.file}, id, r.info->description};
    // Halves: "<id>_a" + "<id>_b" (control, interleaved) or "<id>_l" + "<id>_h" (PCM).
    const Found* first = nullptr;
    const Found* second = nullptr;
    for (const auto& r : roms) {
        const std::string name = r.info->shortName;
        if (name.size() != id.size() + 2 || name.compare(0, id.size(), id) != 0) continue;
        if (r.info->pairType == MT32Emu::ROMInfo::Mux0 || r.info->pairType == MT32Emu::ROMInfo::FirstHalf)
            first = &r;
        if (r.info->pairType == MT32Emu::ROMInfo::Mux1 || r.info->pairType == MT32Emu::ROMInfo::SecondHalf)
            second = &r;
    }
    if (first && second) return Mt32Rom{{first->file, second->file}, id, first->info->description};
    return std::nullopt;
}

// libmt32emu prints its debug messages and the LCD text on stdout by default.
// REACTION_MT32_DEBUG=1 in the environment prints, on stderr, what the emulated
// MT-32 reports instead: notes it can't play for lack of partials, MIDI queue
// overflows, program changes.
class QuietReports final : public MT32Emu::ReportHandler3 {
public:
    QuietReports() : debug_(std::getenv("REACTION_MT32_DEBUG") != nullptr) {}
    void printDebug(const char*, va_list) override {}
    void showLCDMessage(const char*) override {}
    bool onMIDIQueueOverflow() override {
        if (debug_) std::fprintf(stderr, "mt32: MIDI queue overflow\n");
        return false;
    }
    void onNoteOnIgnored(MT32Emu::Bit32u needed, MT32Emu::Bit32u free) override {
        if (debug_) std::fprintf(stderr, "mt32: note on ignored (%u partials needed, %u free)\n", needed, free);
    }
    void onPlayingPolySilenced(MT32Emu::Bit32u needed, MT32Emu::Bit32u free) override {
        if (debug_) std::fprintf(stderr, "mt32: playing note silenced (%u partials needed, %u free)\n", needed, free);
    }
    void onProgramChanged(MT32Emu::Bit8u part, const char* group, const char* patch) override {
        if (debug_) std::fprintf(stderr, "mt32: part %u program %s %s\n", part + 1u, group, patch);
    }

private:
    bool debug_;
};

class Mt32Synth final : public Synth {
public:
    Mt32Synth(const Mt32Roms& roms, int rate) {
        synth_.setReportHandler3(&reports_);
        control_ = load(roms.control);
        pcm_ = load(roms.pcm);
        if (!control_ || !pcm_ || !control_->getROMInfo() || !pcm_->getROMInfo())
            throw std::runtime_error("MT-32: the ROMs could not be loaded");
        const auto mode = MT32Emu::SampleRateConverter::getBestAnalogOutputMode(rate);
        if (!synth_.open(*control_, *pcm_, mode)) throw std::runtime_error("MT-32: the emulator would not start");
        // An MPU-401 in UART mode and a MIDI cable: every message takes its
        // transmission time (31250 baud), as the driver's bytes did.
        synth_.setMIDIDelayMode(MT32Emu::MIDIDelayMode_DELAY_ALL);
        // The LA32 adds the partials of the second half of each group of eight
        // (4-7, 12-15, ...) in opposite phase, so a timbre whose two partials are
        // nearly identical cancels out when it is given one of each, as the real
        // MT-32 does (libmt32emu's "semi-confirmed" emulation; it depends on which
        // partials happen to be free). Covert Action's gunshot is such a timbre: in
        // a break-in, once the score has used partials, some shots are silent.
        // The Options menu's "MT-32 clean mixing" (setCleanMixing) mixes every
        // partial in phase instead; REACTION_MT32_NICE_MIXING=1 forces it on.
        if (const char* e = std::getenv("REACTION_MT32_NICE_MIXING"); e && *e && *e != '0') forceClean_ = true;
        synth_.setNicePartialMixingEnabled(forceClean_);
        src_ = std::make_unique<MT32Emu::SampleRateConverter>(synth_, rate,
                                                              MT32Emu::SamplerateConversionQuality_GOOD);
        parser_ = std::make_unique<MT32Emu::DefaultMidiStreamParser>(synth_);
    }

    ~Mt32Synth() override {
        parser_.reset();
        src_.reset();
        synth_.close();
        if (control_) MT32Emu::ROMImage::freeROMImage(control_);
        if (pcm_) MT32Emu::ROMImage::freeROMImage(pcm_);
    }

    void midi(std::uint8_t byte) override {
        const double t = src_->convertOutputToSynthTimestamp(static_cast<double>(rendered_));
        parser_->setTimestamp(static_cast<MT32Emu::Bit32u>(t));
        parser_->parseStream(&byte, 1);
    }

    void setCleanMixing(bool on) override { synth_.setNicePartialMixingEnabled(on || forceClean_); }
    void render(std::int16_t* out, int frames) override {
        src_->getOutputSamples(out, static_cast<unsigned>(frames));
        rendered_ += static_cast<std::uint64_t>(frames);
    }

private:
    bool forceClean_ = false;
    const MT32Emu::ROMImage* load(const Mt32Rom& rom) {
        if (rom.files.empty() || rom.files.size() > 2) return nullptr;
        for (const auto& f : rom.files) {
            auto s = std::make_unique<MT32Emu::FileStream>();
            if (!s->open(f.string().c_str())) return nullptr;
            files_.push_back(std::move(s));
        }
        const std::size_t n = files_.size();
        if (rom.files.size() == 1) return MT32Emu::ROMImage::makeROMImage(files_[n - 1].get());
        return MT32Emu::ROMImage::makeROMImage(files_[n - 2].get(), files_[n - 1].get());
    }

    QuietReports reports_;
    MT32Emu::Synth synth_;
    std::vector<std::unique_ptr<MT32Emu::FileStream>> files_;   // the ROM images point into these
    const MT32Emu::ROMImage* control_ = nullptr;
    const MT32Emu::ROMImage* pcm_ = nullptr;
    std::unique_ptr<MT32Emu::SampleRateConverter> src_;
    std::unique_ptr<MT32Emu::DefaultMidiStreamParser> parser_;
    std::uint64_t rendered_ = 0;   // output frames so far
};

}  // namespace

std::string Mt32Roms::describe() const { return control.description + " + " + pcm.description; }

std::optional<Mt32Roms> findMt32Roms(const std::vector<fs::path>& dirs, std::vector<std::string>* log) {
    std::vector<Found> roms;
    std::vector<fs::path> seen;
    for (const auto& dir : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        const fs::path canon = fs::weakly_canonical(dir, ec);
        if (std::find(seen.begin(), seen.end(), canon) != seen.end()) continue;
        seen.push_back(canon);
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && romSize(e.file_size(ec))) files.push_back(e.path());
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            MT32Emu::FileStream s;
            if (!s.open(f.string().c_str())) continue;
            if (const MT32Emu::ROMInfo* info = MT32Emu::ROMInfo::getROMInfo(&s)) {
                // The same ROM in two places: keep the first.
                if (std::none_of(roms.begin(), roms.end(), [&](const Found& r) { return r.info == info; }))
                    roms.push_back({f, info});
                if (log) log->push_back(std::string(info->description) + " (" + info->shortName + "): " + f.string());
            }
        }
    }
    for (const char* ctrl : kControlOrder) {
        auto control = assemble(ctrl, roms);
        if (!control) continue;
        const bool cm32l = std::strncmp(ctrl, "ctrl_cm32l", 10) == 0;
        auto pcm = assemble(cm32l ? "pcm_cm32l" : "pcm_mt32", roms);
        if (pcm) return Mt32Roms{*control, *pcm};
    }
    return std::nullopt;
}

std::unique_ptr<Synth> makeMt32Synth(const Mt32Roms& roms, int sampleRate) {
    return std::make_unique<Mt32Synth>(roms, sampleRate);
}

}  // namespace reaction::audio
