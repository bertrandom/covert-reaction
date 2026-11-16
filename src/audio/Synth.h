// The devices the drivers talk to, emulated: they take a driver's output
// (DeviceSink) and render 16-bit stereo samples. Events take effect at the
// current render position: the engine (Audio.h) interleaves the driver's timer
// ticks with rendering, so the timing is the driver's.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "audio/Driver.h"

namespace redux::audio {

class Synth : public DeviceSink {
public:
    // Renders `frames` stereo frames (interleaved L R) at the synth's sample rate.
    virtual void render(std::int16_t* out, int frames) = 0;
};

// AdLib: an OPL2 (emulated by Nuked-OPL3 in its OPL2-compatible mode).
std::unique_ptr<Synth> makeOplSynth(int sampleRate);
// PC speaker: PIT channel 2's square wave, gated by port 61h.
std::unique_ptr<Synth> makeSpeakerSynth(int sampleRate);
// No device: silence.
std::unique_ptr<Synth> makeSilentSynth();

// MT-32 / CM-32L ROMs, identified by their contents (libmt32emu's list of known
// ROMs, SHA-1), whatever the files are called: MT32_CONTROL.ROM / MT32_PCM.ROM,
// MAME's mt32_ctrl_1_07.rom / mt32_pcm.rom, CM32L_CONTROL.ROM, split halves...
struct Mt32Rom {
    std::vector<std::filesystem::path> files;   // one full image, or two halves
    std::string id;                             // libmt32emu's short name, e.g. "ctrl_mt32_1_07"
    std::string description;                    // e.g. "MT-32 Control v1.07"
};
struct Mt32Roms {
    Mt32Rom control, pcm;
    std::string describe() const;   // "MT-32 Control v1.07 + MT-32 PCM ROM"
};

// Looks through `dirs` (non-recursive) for a usable control + PCM pair. The
// preferred machine is the original MT-32 (control 1.07, then other 1.0x,
// then 2.0x), then the CM-32L / LAPC-I. `log` gets one line per ROM found.
std::optional<Mt32Roms> findMt32Roms(const std::vector<std::filesystem::path>& dirs,
                                     std::vector<std::string>* log = nullptr);

// MT-32: libmt32emu (Munt). Throws std::runtime_error if the ROMs won't load.
std::unique_ptr<Synth> makeMt32Synth(const Mt32Roms& roms, int sampleRate);

}  // namespace redux::audio
