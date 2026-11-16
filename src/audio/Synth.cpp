#include "audio/Synth.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include "opl3.h"
}

namespace redux::audio {
namespace {

std::int16_t clamp16(double v) {
    return static_cast<std::int16_t>(std::clamp(v, -32768.0, 32767.0));
}

class OplSynth final : public Synth {
public:
    explicit OplSynth(int rate) { OPL3_Reset(&chip_, static_cast<std::uint32_t>(rate)); }

    // Buffered: a write takes effect a couple of chip samples after the previous
    // one, as the card's I/O delays spread a burst of writes.
    void opl(std::uint8_t reg, std::uint8_t value) override { OPL3_WriteRegBuffered(&chip_, reg, value); }

    void render(std::int16_t* out, int frames) override {
        for (int i = 0; i < frames; ++i) {
            std::int16_t s[2];
            OPL3_GenerateResampled(&chip_, s);
            out[2 * i] = clamp16(s[0] * kGain);
            out[2 * i + 1] = clamp16(s[1] * kGain);
        }
    }

private:
    static constexpr double kGain = 1.5;
    opl3_chip chip_{};
};

// PIT channel 2 in mode 3: a square wave of `divisor` PIT clocks, high for the
// first half. Each output sample is the wave's average over the sample (a box
// filter, against aliasing), then a DC blocker (the speaker has no DC).
class SpeakerSynth final : public Synth {
public:
    explicit SpeakerSynth(int rate) : clocksPerSample_(kPitHz / rate) {}

    void speakerDivisor(std::uint16_t d) override {
        divisor_ = d == 0 ? 65536.0 : std::max<double>(d, 2);
        phase_ = std::fmod(phase_, divisor_);
    }
    void speakerGate(bool on) override {
        if (on && !gate_) phase_ = 0;   // the counter restarts on the gate's rising edge
        gate_ = on;
    }

    void render(std::int16_t* out, int frames) override {
        for (int i = 0; i < frames; ++i) {
            double x = 0;
            if (gate_) {
                double high = 0, left = clocksPerSample_;
                while (left > 0) {
                    const double half = divisor_ / 2;
                    const bool isHigh = phase_ < half;
                    const double step = std::min(left, (isHigh ? half : divisor_) - phase_);
                    if (isHigh) high += step;
                    phase_ += step;
                    left -= step;
                    if (phase_ >= divisor_) phase_ -= divisor_;
                }
                x = (2 * high / clocksPerSample_ - 1) * kAmplitude;
            }
            const double y = x - lastX_ + 0.999 * lastY_;
            lastX_ = x;
            lastY_ = y;
            out[2 * i] = out[2 * i + 1] = clamp16(y);
        }
    }

private:
    static constexpr double kAmplitude = 4000;
    double clocksPerSample_;
    double divisor_ = 65536, phase_ = 0, lastX_ = 0, lastY_ = 0;
    bool gate_ = false;
};

class SilentSynth final : public Synth {
public:
    void render(std::int16_t* out, int frames) override { std::fill(out, out + 2 * frames, std::int16_t(0)); }
};

}  // namespace

std::unique_ptr<Synth> makeOplSynth(int sampleRate) { return std::make_unique<OplSynth>(sampleRate); }
std::unique_ptr<Synth> makeSpeakerSynth(int sampleRate) { return std::make_unique<SpeakerSynth>(sampleRate); }
std::unique_ptr<Synth> makeSilentSynth() { return std::make_unique<SilentSynth>(); }

}  // namespace redux::audio
