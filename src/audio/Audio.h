// The sound engine: one of the original's drivers (Driver.h) and the device it
// drives (Synth.h), run sample-accurately: render() interleaves the driver's
// timer interrupts (60.88 Hz, or 300 Hz for the noise effects) with the synth's
// samples. No SDL here: the app's audio callback calls render(), reaction-tool
// calls it to write WAV files.
//
// Threads: play() and select() may be called from any thread (the game's,
// the city loop's GameThread); render() from one (the audio callback). Without
// threads (the web build) everything runs on the main thread.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "audio/Driver.h"
#include "audio/Synth.h"
#include "core/GameData.h"

namespace reaction::audio {

// Where to look for MT-32 ROMs: `explicitDir` (--mt32-roms), COVERT_MT32_ROMS,
// the game directory and its MT32 / ROMs subfolders, a sibling MT32 (/roms)
// folder, then `extra` (e.g. the per-user folder and its MT32 subfolder).
std::vector<std::filesystem::path> mt32RomDirs(const std::optional<std::filesystem::path>& explicitDir,
                                               const std::filesystem::path& gameDir,
                                               const std::vector<std::filesystem::path>& extra = {});

// "auto", "mt32", "adlib", "speaker", "none" (anything else: auto).
std::optional<Device> parseDevice(const std::string& setting);   // nullopt = auto

class AudioEngine {
public:
    AudioEngine(const GameData& data, std::vector<std::filesystem::path> romDirs, int sampleRate);
    ~AudioEngine();

    // Switches to the device a setting asks for: "auto" is the MT-32 if its ROMs
    // are found, else the AdLib; a device that can't be used (no ROMs, a driver
    // file missing) falls back to the next one (MT-32, AdLib, speaker, none).
    // The new driver starts as at COVERT's start-up (init: for the MT-32, the
    // reset and the timbre upload). Nothing changes if it's the current device.
    void select(const std::string& setting);

    // play_sound(n, a, b) (the caller checks options & 4, as the programs do).
    void play(int n, int a = 0, int b = 0);
    // The Options menu's MT-32 clean mixing (any thread; applied on the audio thread).
    void setMt32CleanMixing(bool on) { cleanMixing_ = on; }

    // Renders interleaved stereo 16-bit frames at sampleRate().
    void render(std::int16_t* out, int frames);

    // Before the first render() only (the audio device may not give the rate asked for).
    void setSampleRate(int rate);
    int sampleRate() const { return rate_; }
    Device device() const;
    std::string status() const;   // what is playing, and why (for the log)

private:
    struct Cmd {
        int n, a, b;
    };
    struct Unit {
        Device device = Device::None;
        std::unique_ptr<SoundDriver> driver;
        std::unique_ptr<Synth> synth;
    };
    Unit build(std::optional<Device> want, std::string& why);

    const GameData& data_;
    std::vector<std::filesystem::path> romDirs_;
    std::optional<Mt32Roms> roms_;
    bool romsSearched_ = false;
    int rate_;
    double clocksPerFrame_;

    mutable std::mutex mu_;
    std::vector<Cmd> queue_;           // guarded by mu_
    std::optional<Unit> pending_;      // guarded by mu_
    Device selected_ = Device::None;   // guarded by mu_
    std::string setting_, status_;     // guarded by mu_

    Unit cur_;                         // the audio thread's
    std::atomic<bool> cleanMixing_{false};
    int appliedMixing_ = -1;           // the audio thread's: what cur_.synth has
    double untilTick_ = 0;             // PIT clocks to the next timer interrupt
};

}  // namespace reaction::audio
