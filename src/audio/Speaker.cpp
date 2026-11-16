// ISOUND.CA ("CovertActIBM10-12-90"), the PC speaker driver, ported from
// covert-decomp src/DRIVERS/ISOUND.ASM (DATA offsets from its MASM listing).
//
// An effect is a list of records ending with a 0 word. A tone record is
// (priority, ticks, noise mask, divisor, step): every tick the divisor moves by
// step and the speaker plays divisor + (random & mask); a mask of 0 is a rest
// (3 words). A sweep record is (priority, ticks, divisor, other divisor, step):
// the divisor moves by step every tick, and with a warble period (cx = 1) it
// alternates with the other divisor on every fast tick. A new effect replaces the
// current one unless that one's priority is higher. While anything plays the
// timer runs at 300 Hz (divisor 0F88h): the sequencer every 5th tick, the noise
// (or warble) step on the others; it goes back to 60 Hz when the effect ends.
#include <stdexcept>

#include "audio/Driver.h"

namespace redux::audio {
namespace {

enum : std::uint16_t {
    kTickDiv = 0x0F91, kPriority = 0x0F93, kToneRepeat = 0x0F95, kToneSeq = 0x0F99,
    kTonePtr = 0x0F9B, kSweepRepeat = 0x0F9D, kSweepSeq = 0x0FA1, kSweepPtr = 0x0FA3,
    kSweepCount = 0x0FA5, kToneCount = 0x0FA7, kSweepDiv = 0x0FA9, kToneDiv = 0x0FAB,
    kToneMask = 0x0FAD, kDivStep = 0x0FAF, kWarblePeriod = 0x0FB1, kWarbleCount = 0x0FB3,
    kParam26 = 0x0FB5, kParam27 = 0x0FB7, kRandIndex = 0x0FB9, kRandSeed = 0x0FBB,
    kRepeatRandom = 0x0FBD, kTimerMode = 0x0FC3, kNoiseSeed = 0x0FC6, kSoundOn = 0x0FC9,
    kNoiseOn = 0x0FCA,
    // records
    kSilence = 0x0450, kRandRecord = 0x0432, kRandPitches = 0x0466, kParamPitches = 0x0452,
    kFx26Record = 0x03A6, kFx27Record = 0x03B2,
};

class Speaker final : public SoundDriver {
public:
    explicit Speaker(const DriverImage& img) : m_(img) {}

    void init() override {   // isnd_init: reset_all, sound_on
        stopSound();
        timerSlow();
        m_.b(kSoundOn) = 0xFF;
    }
    void shutdown() override { stopSound(); }
    std::uint16_t timerDivisor() const override { return fast_ ? kFastTimer : kSlowTimer; }

    void tick() override {
        if (fast_) {   // fast_timer_isr
            if (--m_.b(kTickDiv) == 0) {
                m_.b(kTickDiv) = 5;
                soundTick();
            } else {
                noiseTick();
            }
        } else {       // slow_timer_isr
            soundTick();
            if (--m_.b(kTickDiv) == 0) m_.b(kTickDiv) = 3;
        }
    }

    void play(int n, int a, int b) override;

private:
    Mem m_;
    bool fast_ = false;
    std::uint16_t tickCount_ = 0;

    std::uint16_t W(std::uint16_t a) const { return m_.w(a); }
    void setW(std::uint16_t a, std::uint16_t v) { m_.setW(a, v); }
    void setW(std::uint16_t a, int v) { m_.setW(a, static_cast<std::uint16_t>(v)); }

    void divisor(std::uint16_t d) { if (sink_) sink_->speakerDivisor(d); }   // set_divisor
    void speakerOn() { if (sink_) sink_->speakerGate(true); }               // out 43h,B6h; port 61h |= 3
    void speakerOff() { if (sink_) sink_->speakerGate(false); }

    void timerFast() {
        if (m_.b(kTimerMode) == 1) return;
        m_.b(kTimerMode) = 1;
        fast_ = true;   // (chain_count = 10h: the BIOS handler every 16th tick)
    }
    void timerSlow() {
        if (m_.b(kTimerMode) == 0xFF) return;
        m_.b(kTimerMode) = 0xFF;
        m_.b(kTickDiv) = 3;
        fast_ = false;
    }

    std::uint16_t noise() {
        setW(kNoiseSeed, ror3(static_cast<std::uint16_t>(0x9248 + W(kNoiseSeed))));
        return W(kNoiseSeed);
    }

    void stopSound() {
        for (std::uint16_t a : {kParam26, kParam27, kSweepDiv, kDivStep, kSweepCount, kToneCount,
                                kPriority, kToneRepeat, kSweepRepeat, kWarblePeriod, kWarbleCount})
            setW(a, 0);
        setW(kTonePtr, kSilence);
        setW(kSweepPtr, kSilence);
        speakerOff();
    }

    void noiseTick() {   // isnd_noise_tick
        if (m_.b(kNoiseOn) != 0xFF) return;
        if (W(kWarblePeriod) != 0) {   // warble_step
            if (W(kSweepDiv) == 0) return;
            setW(kWarbleCount, W(kWarbleCount) - 1);
            if (static_cast<std::int16_t>(W(kWarbleCount)) > 0) return;
            setW(kWarbleCount, W(kWarblePeriod));
            setW(kToneMask, -W(kToneMask));
            setW(kSweepDiv, W(kSweepDiv) + W(kToneMask));
            divisor(W(kSweepDiv));
            return;
        }
        divisor(static_cast<std::uint16_t>((noise() & W(kToneMask)) + W(kToneDiv)));
    }

    void nextTone(std::uint16_t bx) {
        setW(kNoiseSeed, 0);
        setW(kPriority, W(bx));
        setW(kToneCount, W(bx + 2));
        setW(kToneMask, W(bx + 4));
        bx = static_cast<std::uint16_t>(bx + 6);
        if (W(kToneMask) == 0) {
            setW(kTonePtr, bx);
            speakerOff();
            return;
        }
        setW(kToneDiv, W(bx));
        setW(kDivStep, W(bx + 2));
        setW(kTonePtr, bx + 4);
        timerFast();
        speakerOn();
    }

    void nextSweep(std::uint16_t bx) {
        setW(kPriority, W(bx));
        setW(kSweepCount, W(bx + 2));
        setW(kSweepDiv, W(bx + 4));
        bx = static_cast<std::uint16_t>(bx + 6);
        if (W(kSweepDiv) == 0) {
            setW(kSweepPtr, bx);
            speakerOff();
            return;
        }
        setW(kToneMask, W(kSweepDiv) - W(bx));
        setW(kDivStep, W(bx + 2));
        setW(kSweepPtr, bx + 4);
        speakerOn();
        divisor(W(kSweepDiv));
        if (W(kWarblePeriod) != 0) timerFast();
    }

    void playTones(std::uint16_t bx, std::uint16_t repeat = 0) {   // play_tones(_n)
        if (W(kPriority) > W(bx)) return;
        setW(kWarblePeriod, 0);
        setW(kSweepCount, 0);
        setW(kSweepRepeat, 0);
        setW(kToneRepeat, repeat);
        setW(kSweepSeq, kSilence);
        setW(kToneSeq, bx);
        nextTone(bx);
    }

    void playSweep(std::uint16_t bx, std::uint16_t warble) {
        if (W(kPriority) > W(bx)) return;
        setW(kWarblePeriod, warble);
        setW(kToneCount, 0);
        setW(kToneRepeat, 0);
        setW(kSweepRepeat, 0);
        setW(kToneSeq, kSilence);
        setW(kSweepSeq, bx);
        nextSweep(bx);
    }

    void endOfEffect() {   // L_030A
        if (W(kRepeatRandom) != 0) {
            pickRandom31();
            nextTone(kRandRecord);
            return;
        }
        stopSound();
        timerSlow();
    }

    void pickRandom31() {
        setW(kRandSeed, ror3(static_cast<std::uint16_t>(0x4567 + W(kRandSeed))));
        setW(kRandIndex, (W(kRandSeed) & 4) >> 2);
        const std::uint16_t si = static_cast<std::uint16_t>(kRandPitches + W(kRandIndex) * 4);
        setW(kRandRecord + 4, W(si));
        setW(kRandRecord + 0x0C, W(si + 2));
    }

    void setParam(std::uint16_t record, std::uint16_t param) {   // set_param26/27
        const std::uint16_t si = static_cast<std::uint16_t>(kParamPitches + W(param) * 4);
        setW(record + 2, W(si));
        setW(record + 6, W(si + 2));
    }

    void soundTick();
};

void Speaker::soundTick() {
    ++tickCount_;
    if (m_.b(kSoundOn) != 0xFF) return;
    if ((W(kSweepCount) | W(kSweepRepeat)) != 0) {   // L_0321: a sweep plays
        setW(kSweepCount, W(kSweepCount) - 1);
        if (W(kSweepCount) != 0) {
            if (W(kSweepDiv) == 0) return;
            setW(kSweepDiv, W(kSweepDiv) + W(kDivStep));
            speakerOn();
            divisor(W(kSweepDiv));
            return;
        }
        if (W(W(kSweepPtr)) != 0) {
            nextSweep(W(kSweepPtr));
            return;
        }
        if (W(kSweepRepeat) != 0) {
            setW(kSweepRepeat, W(kSweepRepeat) - 1);
            nextSweep(W(kSweepSeq));
            return;
        }
        setW(kWarblePeriod, 0);
        endOfEffect();
        return;
    }
    if ((W(kToneCount) | W(kToneRepeat)) == 0) {
        endOfEffect();
        return;
    }
    setW(kToneCount, W(kToneCount) - 1);
    if (W(kToneCount) == 0) {
        if (W(W(kTonePtr)) != 0) {
            nextTone(W(kTonePtr));
        } else if (W(kToneRepeat) != 0) {
            setW(kToneRepeat, W(kToneRepeat) - 1);
            nextTone(W(kToneSeq));
        } else {
            endOfEffect();
        }
        return;
    }
    if (W(kToneMask) == 0) return;   // a rest
    if (W(kToneCount) == 1 && W(W(kTonePtr)) == 0 && W(kToneRepeat) == 0) {
        // the effect's last tick: full-range noise
        setW(kDivStep, 0);
        setW(kNoiseSeed, 0);
        setW(kToneDiv, 0xFFFF);
        setW(kToneMask, 0xFFFF);
    }
    setW(kToneDiv, W(kToneDiv) + W(kDivStep));
    divisor(static_cast<std::uint16_t>((noise() & W(kToneMask)) + W(kToneDiv)));
}

void Speaker::play(int n, int a, int /*b*/) {   // isnd_play_sound
    if (n == 0x1F) {
        setW(kRepeatRandom, 1);
    } else {
        setW(kRepeatRandom, 0);
        if (n == 0x1A) {
            setW(kParam26, a);
            setW(kParam27, 0);
            setParam(kFx26Record, kParam26);
        } else if (n == 0x1B) {
            setW(kParam27, a);
            setW(kParam26, 0);
            setParam(kFx27Record, kParam27);
        }
    }
    if (n > 0x2E || n < 0) return;
    // fx_table: the record offsets of play_tones / play_sweep effects.
    switch (n) {
    case 0: case 1: stopSound(); break;
    case 2: playTones(0x046E, 4); break;     // title theme, 4 times over
    case 3: playTones(0x008A); break;
    case 4: playTones(0x00A6); break;
    case 5: playSweep(0x00C2, 1); break;
    case 6: playTones(0x0242, 7); break;
    case 7: playTones(0x024E); break;
    case 8: playTones(0x025A); break;
    case 9: playTones(0x0266); break;
    case 10: playTones(0x0282); break;
    case 11: playTones(0x02AC); break;
    case 12: playTones(0x02B8); break;
    case 13: playSweep(0x02C4, 1); break;
    case 14: playSweep(0x02D0, 1); break;
    case 15: playSweep(0x02DC, 1); break;
    case 16: playTones(0x02E8); break;
    case 17: playTones(0x02F4); break;
    case 18: playSweep(0x0300, 1); break;
    case 19: playSweep(0x030C, 1); break;
    case 20: playSweep(0x0318, 1); break;
    case 21: playTones(0x0324); break;
    case 22: playTones(0x0330, 2); break;
    case 23: playTones(0x033C); break;
    case 24: playTones(0x0348); break;
    case 25: playTones(0x039A); break;
    case 26: case 27: break;                 // only set their records (never played)
    case 28: playTones(0x03BE); break;       // (the original then waits for the end)
    case 29: playTones(0x03CA); break;
    case 30: playSweep(0x0426, 1); break;
    case 31: playTones(kRandRecord); break;
    case 32: playTones(0x0444); break;
    case 33: playTones(0x005C); break;
    case 34: playTones(0x0050); break;
    case 35:
        playTones(0x0068, 0xFF);
        setW(kToneSeq, 0x0072);              // then w_D0072, repeated
        break;
    case 36: playTones(0x007E); break;
    case 37:   // pause
        m_.b(kSoundOn) = 0;
        m_.b(kNoiseOn) = 0;
        speakerOff();
        break;
    case 38:   // resume
        m_.b(kSoundOn) = 0xFF;
        m_.b(kNoiseOn) = 0xFF;
        if (sink_) sink_->speakerGate(true);
        break;
    case 45: playSweep(0x00CE, 1); break;
    case 46: playTones(0x01AA); break;
    default: break;                          // 39-44: nothing
    }
}

}  // namespace

std::unique_ptr<SoundDriver> makeSpeaker(const DriverImage& image) {
    if (image.ident.rfind("CovertActIBM", 0) != 0 || image.data.size() < 0x0FCB)
        throw std::runtime_error("ISOUND.CA: not the Covert Action PC speaker driver");
    return std::make_unique<Speaker>(image);
}

}  // namespace redux::audio
