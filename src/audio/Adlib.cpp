// ASOUND.CA ("Covert Adlib10-18-90"), the AdLib (OPL2) driver, ported from
// covert-decomp src/DRIVERS/ASOUND.ASM. Offsets in comments and names are the
// DATA-segment offsets of the original (w_D2560 = DATA:2560h, etc.); sub_XXXX are
// CODE offsets.
//
// The music and effect player (sub_0048 to sub_0B64) is C compiler output: seven
// voices (blocks of 1Ah bytes at 2322h), voice i on OPL channel i (the current
// channel is b_D23FC). Each voice steps through a byte stream of notes (note,
// duration in ticks; note 0 or duration 0 is a rest / key off) and commands
// F7h-FFh (transpose, volume sweep, volume, pitch bend, gate, instrument, jump,
// loops). An instrument is a 2Ch-byte record at DATA:0050h + 2Ch*n: two operators
// of 16h bytes (modulator, carrier), loaded into the operators of the channel
// through b_D2046 (channel -> its two operators) and b_D2058 (operator -> register
// offset). The modulator half also carries a "noise" burst (count, mask, base,
// step at +0Eh..+15h), kept per channel at b_D2500: a note on such a channel
// starts a burst on one of two noise slots, which switches the timer to 300 Hz
// (timer_fast). The fast handler then writes a random frequency (base + random &
// mask) on each slot's channel on every tick; the sequencer runs every 5th tick;
// the base moves by step every sequencer tick and the burst ends after count
// sequencer ticks. When no burst is left the sequencer switches back to 60 Hz.
//
// The rest is assembly: register access (opl_write, mirrored in opl_shadow at
// DATA:2400h), the timer handlers, start_voice, the car engines of effects 26/27
// and the held tone of 35/36 (OPL channels 7 and 8, pitch slides in sub_140A),
// pause/resume (37/38) and the generated "tension" score of effect 44 (sub_1991,
// on voices 0-2; the same design as RSOUND's).
//
// Not emulated (no effect on what reaches the chip):
//   - calibrate_delays / pit_elapsed and the delay loops in opl_write;
//   - detect_adlib's status reads: the card is assumed present (its register
//     writes, timer reset / start / reset, are made in order); asnd_init returns
//     the detection result (0) in AX, init() returns nothing;
//   - asnd_init stores its stack argument in w_D2270 (never read): init() takes
//     no argument and leaves it alone;
//   - the int 8 vector, PIC EOI and BIOS chaining (chain_count is kept anyway),
//     the private stack sound_tick and sub_135E switch to (DATA:226Ah);
//   - dead code: L_0214 (relative volume), the unreferenced raw-frequency routine
//     after sub_009A (ASM line 898), and L_0D8E (a fade-out reset that sets every
//     voice to fx_1_data and busy-waits until they have all ended, L_0E08).
//     play_sound itself never waits.
#include <stdexcept>

#include "audio/Driver.h"

namespace reaction::audio {
namespace {

// DATA offsets.
enum : std::uint16_t {
    kInstruments = 0x0050,  // instrument records, 2Ch bytes (two operators of 16h)
    kFxStop = 0x0788,       // fx_1_data: no sweep, no bend, then key off and end
    kGenRest = 0x0F18,      // w_D0F18: a rest of b_D0F19 ticks, then FDh (jump)
    kGenRestLen = 0x0F19,   // b_D0F19
    kGenChain = 0x12C6,     // w_D12C6: voice 1's stream after the rest (level 5)
    kGenBass = 0x1332,      // w_D1332: the bass ostinato on voice 0
    kNoiseSeed = 0x2026,    // w_D2026: the fast handler's random()
    kFreqs = 0x202A,        // w_D202A: F-numbers of the 12 notes of an octave
    kAmDepth = 0x2042,      // b_D2042: BDh bit 7
    kVibDepth = 0x2043,     // b_D2043: BDh bit 6
    kKeySplit = 0x2044,     // b_D2044: 08h bit 6 (note select)
    kChanOps = 0x2046,      // b_D2046: channel -> modulator, carrier operator
    kOpRegs = 0x2058,       // b_D2058: operator -> register offset
    kShutRet = 0x226C,      // w_D226C: asnd_shutdown's return value
    kGenCount = 0x2278, kGenPeriod = 0x227A, kGenPrev = 0x227C,
    kGenLevel = 0x227E,     // w_D227E: play_sound(44, a, b): a
    kGenSpace = 0x2280,     // w_D2280: b
    kGenOn = 0x2282,        // b_D2282
    kRand = 0x2283,         // w_D2283: random()
    kEngWhich = 0x2285,     // b_D2285: FFh = fx_26, 0 = fx_27
    kEngOn = 0x2286,        // b_D2286: engine playing: 1 = fx_26, 2 = fx_27
    kEngSpeed = 0x2287,     // b_D2287: its speed (0 = slide down and off)
    kEngSpeed26 = 0x2288, kEngSpeed27 = 0x2289,
    kSlide = 0x228A,        // w_D228A: slide step on channel 8 (+-4, 0 = none)
    kFreqHi = 0x228C, kFreqLo = 0x228D,     // channel 8's block/F-number
    kFreq7Hi = 0x228E, kFreq7Lo = 0x228F,   // channel 7's (fx_35)
    kTarget = 0x2290,       // w_D2290: slide target
    kHeld = 0x2292,         // b_D2292: 14h while fx_35's tone holds
    kSavedA = 0x2293, kSavedB = 0x2294,     // noise counts saved by fx_37
    kEngFreqs = 0x2295,     // w_D2295: channel 8 block/F-number per speed
    kGenFrag1 = 0x22A5,     // w_D22A5: voice 2's phrases at level 1
    kGenFrag = 0x22B5,      // w_D22B5: at levels 2-5, 8 per level
    kGenBassStreams = 0x2305,
    // BSS
    kNoiseBaseA = 0x2316, kNoiseBaseB = 0x2318, kNoiseStepA = 0x231A, kNoiseStepB = 0x231C,
    kEnabled = 0x2320,      // b_D2320: sequencer on
    kVoices = 0x2322,       // 8 voice blocks of 1Ah bytes (the 8th is never played)
    kNoiseCountA = 0x23F2, kNoiseCountB = 0x23F3, kNoiseMaskA = 0x23F4, kNoiseMaskB = 0x23F6,
    kChannel = 0x23FC,      // b_D23FC: channel of the voice being played
    kNoiseChA = 0x23FD, kNoiseChB = 0x23FE,
    kShadow = 0x2400,       // opl_shadow: every register written
    kNoiseParams = 0x2500,  // b_D2500: per channel, 8 bytes (count, -, mask, base, step)
    kRegTemp = 0x2558,      // w_D2558: channel (C0h register) of the instrument load
    kOpTemp = 0x255E,       // w_D255E: operator register offset
    kCurVoice = 0x2560,     // w_D2560: voice being played
    kCurInstr = 0x2562,     // w_D2562: operator record being loaded
    kDataEnd = 0x230D,      // end of the initialised DATA
};
constexpr std::uint16_t kVoiceSize = 0x1A;
constexpr std::uint16_t voice(int i) { return static_cast<std::uint16_t>(kVoices + i * kVoiceSize); }

// Voice fields.
enum : std::uint16_t {
    vDur = 0x00, vBend = 0x01, vVolStep = 0x02, vNote = 0x03, vInstr = 0x04, vVol = 0x05,
    vGate = 0x06, vGateCount = 0x07, vVolCount = 0x08, vVolRate = 0x09, vStart = 0x0A,
    vPtr = 0x0C, vMark1 = 0x0E, vMark2 = 0x10, vCount1 = 0x12, vCount2 = 0x14, vChain = 0x16,
    vTranspose = 0x18,
};

// Operator record fields (sub_043C..sub_05D8, sub_061C).
enum : std::uint16_t {
    oAttack = 0x00, oDecay = 0x01, oSustain = 0x02, oRelease = 0x03, oEgType = 0x04,
    oKsr = 0x05, oLevel = 0x06, oKsl = 0x07, oWave = 0x08, oMult = 0x09, oFeedback = 0x0A,
    oAm = 0x0B, oVib = 0x0C, oConnection = 0x0D, oNoiseCount = 0x0E, oNoiseMask = 0x10,
    oNoiseBase = 0x12, oNoiseStep = 0x14,
};

// timer_mode (cs:0BB8h)
enum : std::uint8_t { kUnhooked = 0, kModeSlow = 1, kModeFast = 0xFF };

class Adlib final : public SoundDriver {
public:
    explicit Adlib(const DriverImage& img) : m_(img) {}

    void init() override {   // asnd_init
        hookTimer();
        detectAdlib();
    }

    void shutdown() override {   // asnd_shutdown
        fx0();
        unhookTimer();
        setW(kShutRet, 0);   // (returned in AX, then cleared)
    }

    std::uint16_t timerDivisor() const override {
        return timerMode_ == kModeFast ? kFastTimer : kSlowTimer;
    }

    void tick() override {
        if (timerMode_ == kModeFast) fastIsr();
        else if (timerMode_ == kModeSlow) slowIsr();
        // unhooked: the BIOS handler runs, not the driver
    }

    void play(int n, int a, int b) override;   // asnd_play_sound

private:
    Mem m_;
    // CODE-segment variables
    std::uint8_t timerMode_ = kUnhooked;   // timer_mode
    std::uint8_t chainCount_ = 0;          // chain_count
    std::uint8_t tickDiv_ = 0;             // tick_div
    std::uint8_t timerHooked_ = 0;         // timer_hooked
    std::uint16_t tickCount_ = 0;          // tick_count (slot 105)

    std::uint8_t& B(std::uint16_t a) { return m_.b(a); }
    std::uint16_t W(std::uint16_t a) const { return m_.w(a); }
    void setW(std::uint16_t a, std::uint16_t v) { m_.setW(a, v); }
    static std::uint16_t at(std::uint16_t base, int off) { return static_cast<std::uint16_t>(base + off); }
    std::uint8_t& shadow(int reg) { return B(at(kShadow, reg)); }

    // ---- register access ------------------------------------------------------
    void opl(std::uint8_t reg, std::uint8_t value) {   // opl_write: AL = register, BL = value
        shadow(reg) = value;
        // (delay w_D2274) out 388h, reg; (delay w_D2276) out 389h, value
        if (sink_) sink_->opl(reg, value);
    }
    void oplC(int reg, int value) {   // opl_write_c(reg, value): AL, BL of the C arguments
        opl(static_cast<std::uint8_t>(reg), static_cast<std::uint8_t>(value));
    }
    void oplFreq(std::uint8_t ch, std::uint16_t bx) {   // opl_write_freq: AL = channel, BX = block/F-number
        opl(static_cast<std::uint8_t>(ch + 0xA0), static_cast<std::uint8_t>(bx));
        opl(static_cast<std::uint8_t>(ch + 0xB0), static_cast<std::uint8_t>((bx >> 8) | 0x20));
    }

    // ---- timer ---------------------------------------------------------------
    void hookTimer() {
        if (timerHooked_) return;
        timerHooked_ = 0xFF;
        timerMode_ = kUnhooked;
        timerSlow();
    }
    void unhookTimer() {   // the old vector back, PIT divisor 0 (18.2 Hz)
        timerMode_ = kUnhooked;
        timerHooked_ = 0;
    }
    void timerSlow() {   // slow_timer_isr at 4C90h
        if (timerMode_ == kModeSlow) return;
        timerMode_ = kModeSlow;
        tickDiv_ = 3;
    }
    void timerFast() {   // fast_timer_isr at 0F88h (tick_div is left as it is)
        if (timerMode_ == kModeFast) return;
        timerMode_ = kModeFast;
        chainCount_ = 0x10;
    }

    void slowIsr() {   // slow_timer_isr
        soundTick();
        if (--tickDiv_ == 0) tickDiv_ = 3;   // and on to the BIOS handler (else EOI)
    }

    void fastIsr() {   // fast_timer_isr
        setW(kNoiseSeed, ror3(static_cast<std::uint16_t>(0x9248 + W(kNoiseSeed))));
        if (B(kNoiseCountA) != 0)
            oplFreq(B(kNoiseChA),
                    static_cast<std::uint16_t>(((W(kNoiseSeed) ^ 0xFFFF) & W(kNoiseMaskA)) + W(kNoiseBaseA)));
        if (B(kNoiseCountB) != 0)
            oplFreq(B(kNoiseChB), static_cast<std::uint16_t>((W(kNoiseSeed) & W(kNoiseMaskB)) + W(kNoiseBaseB)));
        if (--tickDiv_ == 0) {
            tickDiv_ = 5;
            soundTick();
        }
        if (--chainCount_ == 0) chainCount_ = 0x10;   // the BIOS handler every 16th tick (else EOI)
    }

    void soundTick();   // sound_tick

    // ---- set-up -------------------------------------------------------------
    void detectAdlib() {   // detect_adlib (calibrate_delays first)
        opl(4, 0x60);      // reset both timers
        opl(4, 0x80);      // reset the IRQ
        // (in 388h: status)
        opl(2, 0xFF);      // timer 1 count
        opl(4, 0x21);      // start timer 1
        // (200 reads of 388h, then the status: assumed C0h, the card is there)
        opl(4, 0x60);
        opl(4, 0x80);
        reset();           // sub_0CED, whatever the result
    }

    void reset() {   // sub_0CED
        setW(kGenLevel, 0);
        setW(kGenPrev, 0);
        setW(kGenSpace, 0);
        B(kEnabled) = 0;
        for (int i = 0; i < 8; ++i)
            for (int k = 0; k < 3; ++k) B(at(voice(i), k)) = 0;
        B(kNoiseCountA) = 0;
        B(kNoiseCountB) = 0;
        for (std::uint16_t a : {kNoiseMaskA, kNoiseMaskB, kNoiseBaseA, kNoiseBaseB, kNoiseStepA, kNoiseStepB})
            setW(a, 0);
        B(kEnabled) = 0xFF;
        for (int reg = 0xFF; reg != 0; --reg) oplC(reg, 0);   // registers FFh..01h
        opl(1, 0x20);   // waveform select enable
    }

    void fx0() {   // fx_0
        B(kEngOn) = 0;
        B(kEngSpeed) = 0;
        B(kEngSpeed26) = 0;
        B(kEngSpeed27) = 0;
        reset();
    }

    void startVoice(int i, std::uint16_t data, std::uint16_t chain = 0) {   // start_voice
        const std::uint16_t v = voice(i);
        const std::uint8_t on = B(kEnabled);
        B(kEnabled) = 0;
        B(at(v, vBend)) = 0;
        B(at(v, vVolRate)) = 0xFF;
        B(at(v, vVolStep)) = 0;
        B(at(v, vTranspose)) = 0;
        for (int f : {vStart, vPtr, vMark1, vMark2}) setW(at(v, f), data);
        setW(at(v, vCount1), 0);
        setW(at(v, vCount2), 0);
        B(at(v, vGate)) = 0;
        setW(at(v, vChain), chain);
        B(at(v, vDur)) = 1;
        B(kEnabled) = on;
    }

    // ---- channel operations of the sequencer (C code) -----------------------
    std::uint16_t chanOp(int k) {   // register offset of the channel's operator k (0, 1)
        const std::int16_t op = sx(B(at(kChanOps, B(kChannel) * 2 + k)));
        return static_cast<std::uint16_t>(sx(B(at(kOpRegs, op))));
    }

    void setVolume(int v) {   // sub_0048: the carrier's level, KSL kept
        const int reg = chanOp(1) + 0x40;
        const int val = (shadow(reg) & 0xC0) | (0x3F - (v & 0x3F));
        oplC(reg, val);
    }

    void setNote(std::uint8_t note, std::int16_t transpose) {   // sub_009A
        int reg = B(kChannel) + 0xA0;
        const std::uint8_t pitch = static_cast<std::uint8_t>(note % 12);
        const std::uint8_t octave = static_cast<std::uint8_t>(note / 12);
        const auto f = static_cast<std::uint16_t>(transpose + W(at(kFreqs, pitch * 2)));
        oplC(reg, f & 0xFF);
        reg += 0x10;
        std::uint8_t b = static_cast<std::uint8_t>(shadow(reg) & 0x20);
        b = static_cast<std::uint8_t>(b | ((f >> 8) | (octave << 2)));   // (a high octave spills into bit 5)
        oplC(reg, b);
    }

    void bend(std::int16_t d) {   // sub_018E: block and F-number move together
        const int regA = B(kChannel) + 0xA0;
        const int regB = regA + 0x10;
        auto f = static_cast<std::uint16_t>((shadow(regB) & 0x1F) << 8);
        f = static_cast<std::uint16_t>(f + shadow(regA) + d);
        f &= 0x1FFF;
        oplC(regA, f & 0xFF);
        const auto b = static_cast<std::uint8_t>((shadow(regB) & 0x20) | (f >> 8));
        oplC(regB, b);
    }

    void keyOff() {   // sub_0272
        const int reg = B(kChannel) + 0xB0;
        oplC(reg, shadow(reg) & 0xDF);
    }

    void noteOn(std::uint8_t note, std::uint8_t vol, std::int16_t transpose) {   // sub_029A
        const std::uint8_t ch = B(kChannel);
        const std::uint16_t np = at(kNoiseParams, ch * 8);
        if (B(np) != 0) {   // the instrument has a noise burst: on slot A if it is free, else B
            if (B(kNoiseCountA) == 0) {
                B(kNoiseChA) = ch;
                B(kNoiseCountA) = B(np);
                setW(kNoiseMaskA, W(at(np, 2)));
                setW(kNoiseBaseA, W(at(np, 4)));
                setW(kNoiseStepA, W(at(np, 6)));
            } else {
                B(kNoiseChB) = ch;
                B(kNoiseCountB) = B(np);
                setW(kNoiseMaskB, W(at(np, 2)));
                setW(kNoiseBaseB, W(at(np, 4)));
                setW(kNoiseStepB, W(at(np, 6)));
            }
            timerFast();
        }
        setNote(note, transpose);
        setVolume(vol);
        const int reg = ch + 0xB0;
        oplC(reg, shadow(reg) | 0x20);   // key on
    }

    // sub_0602: one operator of an instrument (w_D2562 = its record, w_D255E = its
    // register offset, w_D2558 = the channel).
    void writeOperator() {
        const std::uint16_t r = W(kCurInstr);
        const int op = W(kOpTemp);
        const auto f = [&](int k) -> std::uint8_t { return B(at(r, k)); };
        // sub_03D6: rhythm register, depth bits
        oplC(0xBD, (shadow(0xBD) & 0x3F) | (B(kAmDepth) ? 0x80 : 0) | (B(kVibDepth) ? 0x40 : 0));
        oplC(0x08, B(kKeySplit) ? 0x40 : 0);                                       // sub_0420
        oplC(op + 0x40, (0x3F - (f(oLevel) & 0x3F)) | f(oKsl) << 6);             // sub_043C
        oplC(W(kRegTemp) + 0xC0, f(oFeedback) << 1 | (f(oConnection) ? 0 : 1));  // sub_0482
        oplC(op + 0x60, f(oAttack) << 4 | (f(oDecay) & 0x0F));                   // sub_04CA
        oplC(op + 0x80, f(oSustain) << 4 | (f(oRelease) & 0x0F));                // sub_050C
        oplC(op + 0x20, (f(oAm) ? 0x80 : 0) + (f(oVib) ? 0x40 : 0) + (f(oEgType) ? 0x20 : 0) +
                            (f(oKsr) ? 0x10 : 0) + (f(oMult) & 0x0F));            // sub_0550
        oplC(op + 0xE0, f(oWave) & 3);                                            // sub_05D8
    }

    void loadInstrument(std::uint8_t n) {   // sub_061C
        const std::uint8_t ch = B(kChannel);
        const int reg = ch + 0xB0;
        setW(kRegTemp, static_cast<std::uint16_t>(reg));
        oplC(reg, shadow(reg) & 0xDF);   // key off
        setW(kRegTemp, ch);
        const auto rec = static_cast<std::uint16_t>(kInstruments + n * 0x2C);
        setW(kCurInstr, rec);
        setW(kOpTemp, chanOp(0));
        writeOperator();   // (C0h is written here with the modulator's bits...)
        const std::uint16_t np = at(kNoiseParams, ch * 8);
        B(np) = B(at(rec, oNoiseCount));
        setW(at(np, 2), W(at(rec, oNoiseMask)));
        setW(at(np, 4), W(at(rec, oNoiseBase)));
        setW(at(np, 6), W(at(rec, oNoiseStep)));
        setW(kCurInstr, at(rec, 0x16));
        setW(kOpTemp, chanOp(1));
        writeOperator();   // (...and again with the carrier's)
    }

    void stepVoice(std::uint16_t v);   // sub_073A

    void sequence() {   // sub_0B64
        if (B(kEnabled) == 0) return;
        B(kChannel) = 0;
        for (int i = 0; i < 7; ++i) {
            setW(kCurVoice, voice(i));
            stepVoice(voice(i));
        }
    }

    // ---- effect 44: the generated score (sub_1991 and helpers) ---------------
    // random: ADC with the caller's carry, then ADC tick_count with the carry out.
    std::uint16_t random(bool cf) {
        std::uint32_t s = std::uint32_t(W(kRand)) + 0x7131 + (cf ? 1 : 0);
        s = (s & 0xFFFF) + tickCount_ + (s >> 16);
        setW(kRand, static_cast<std::uint16_t>(s));
        return static_cast<std::uint16_t>(s);
    }
    void setBassVolume(std::uint8_t al) {   // sub_1984 (the F9h arguments of w_D1332's streams)
        B(0x1335) = al;
        B(0x134D) = al;
        B(0x1365) = al;
        B(0x137D) = al;
    }
    void stopVoice0() { setW(at(voice(0), vPtr), kFxStop); }   // sub_196D
    void stopVoices12() {                                       // sub_1976 (+ sub_1982)
        setW(at(voice(1), vPtr), kFxStop);
        setW(at(voice(2), vPtr), kFxStop);
        setBassVolume(0x21);
    }
    void setRest(std::uint16_t ax) { B(kGenRestLen) = static_cast<std::uint8_t>(ax); }   // sub_1AB7
    void generate();         // sub_1991
    void bassVoice();        // sub_1915
    void phraseVoice2();     // sub_1A48
    void pickPhrase();       // sub_1A5C
    void phraseGap();        // sub_1A91

    // ---- effects 26/27/35/36: channels 7 and 8 -------------------------------
    void engine(std::uint8_t which, int a);   // fx_26 / fx_27
    void engineVoice();                       // sub_135E
    void engineTarget(int a) {                // sub_1346
        setW(kTarget, W(at(kEngFreqs, static_cast<std::uint16_t>(a) * 2)));
    }
    void slideDirection() {   // sub_13E2
        const auto cur = static_cast<std::int16_t>(B(kFreqHi) << 8 | B(kFreqLo));
        const auto t = static_cast<std::int16_t>(W(kTarget));
        setW(kSlide, t > cur ? 4 : t < cur ? 0xFFFC : 0);
    }
    void slide();   // sub_140A
    void engineKeyOff() {   // sub_14AB
        B(kEnabled) = 0;
        opl(0xB8, static_cast<std::uint8_t>(shadow(0xB8) & 0xDF));
        B(kEnabled) = 0xFF;
    }
};

void Adlib::soundTick() {
    if (B(kEnabled) == 0) return;
    ++tickCount_;
    if (B(kGenOn) != 0) generate();
    if (W(kSlide) != 0) slide();
    sequence();
    // The noise bursts: the base moves by its step, the count runs down.
    if ((B(kNoiseCountB) | B(kNoiseCountA)) == 0) {
        timerSlow();
        return;
    }
    if (B(kNoiseCountA) != 0) {
        setW(kNoiseBaseA, static_cast<std::uint16_t>(W(kNoiseBaseA) + W(kNoiseStepA)));
        const std::uint16_t ax = W(kNoiseBaseA);
        if (--B(kNoiseCountA) == 0) {
            // Silence the burst's channel, unless slot B still plays on it. When slot
            // B is idle the original silences channel AL = the low byte of the new
            // base instead of b_D23FD (AX still holds w_D2316): registers
            // A0h+AL and B0h+AL (8-bit) get 0, the noise channel stays keyed on.
            std::uint8_t al = static_cast<std::uint8_t>(ax);
            bool silence = true;
            if (B(kNoiseCountB) != 0) {
                al = B(kNoiseChA);
                silence = al != B(kNoiseChB);
            }
            if (silence) {   // L_0F01
                opl(static_cast<std::uint8_t>(al + 0xA0), 0);
                opl(static_cast<std::uint8_t>(al + 0xB0), 0);
            }
        }
    }
    if (B(kNoiseCountB) != 0) {   // L_0F11
        setW(kNoiseBaseB, static_cast<std::uint16_t>(W(kNoiseBaseB) + W(kNoiseStepB)));
        const std::uint16_t ax = W(kNoiseBaseB);
        if (--B(kNoiseCountB) == 0) {
            // The same the other way round: while slot A still plays, the channel
            // silenced is the low byte of w_D2318 (even when it is slot A's channel).
            std::uint8_t al = static_cast<std::uint8_t>(ax);
            bool silence = true;
            if (B(kNoiseCountA) == 0) {
                al = B(kNoiseChB);
                silence = al != B(kNoiseChA);
            }
            if (silence) {   // L_0F38
                opl(static_cast<std::uint8_t>(al + 0xA0), 0);
                opl(static_cast<std::uint8_t>(al + 0xB0), 0);
            }
        }
    }
}

void Adlib::stepVoice(std::uint16_t v) {
    if (B(v) != 0) {
        if (B(at(v, vGateCount)) != 0 && --B(at(v, vGateCount)) == 0) keyOff();
        if (--B(v) == 0) {
            for (;;) {   // L_0783: commands until a note
                std::uint16_t p = W(at(v, vPtr));
                const std::uint8_t c = B(p);
                if (!(c & 0x80) || sx(c) <= sx(0xF6)) break;   // 00h-F6h: a note
                const auto adv = [&](int k) { setW(at(v, vPtr), at(W(at(v, vPtr)), k)); };
                switch (c) {
                case 0xFF:   // L_07B0: inner loop: FF 00 marks, FF n repeats n more times
                    if (W(at(v, vCount1)) == 0) {
                        ++p;
                        if (B(p) == 0) {
                            adv(2);
                            setW(at(v, vMark1), W(at(v, vPtr)));
                            setW(at(v, vCount1), 0);
                        } else {
                            setW(at(v, vCount1), static_cast<std::uint16_t>(sx(B(p))));
                            setW(at(v, vPtr), W(at(v, vMark1)));
                        }
                    } else {
                        setW(at(v, vCount1), static_cast<std::uint16_t>(W(at(v, vCount1)) - 1));
                        if (W(at(v, vCount1)) == 0) {
                            adv(2);
                            setW(at(v, vMark1), W(at(v, vPtr)));
                        } else {
                            setW(at(v, vPtr), W(at(v, vMark1)));
                        }
                    }
                    break;
                case 0xFE:   // L_0844: outer loop
                    if (W(at(v, vCount2)) == 0) {
                        ++p;
                        if (B(p) == 0) {
                            adv(2);
                            setW(at(v, vMark2), W(at(v, vPtr)));
                            setW(at(v, vMark1), W(at(v, vPtr)));
                            setW(at(v, vCount1), 0);
                            setW(at(v, vCount2), 0);
                        } else {
                            setW(at(v, vCount2), static_cast<std::uint16_t>(sx(B(p))));
                            setW(at(v, vPtr), W(at(v, vMark2)));
                        }
                    } else {
                        setW(at(v, vCount2), static_cast<std::uint16_t>(W(at(v, vCount2)) - 1));
                        if (W(at(v, vCount2)) == 0) {
                            adv(2);
                            setW(at(v, vMark2), W(at(v, vPtr)));
                            setW(at(v, vMark1), W(at(v, vPtr)));
                        } else {
                            setW(at(v, vPtr), W(at(v, vMark2)));
                        }
                    }
                    break;
                case 0xFD:   // L_08FC: from the top, or on to the chained stream
                    if (W(at(v, vChain)) == 0) {
                        setW(at(v, vPtr), W(at(v, vStart)));
                    } else {
                        const std::uint16_t ch = W(at(v, vChain));
                        for (int f : {vStart, vPtr, vMark1, vMark2}) setW(at(v, f), ch);
                    }
                    break;
                case 0xFC:   // L_0952: instrument
                    B(at(v, vInstr)) = B(at(p, 1));
                    adv(2);
                    loadInstrument(B(at(v, vInstr)));
                    break;
                case 0xFB:   // L_097E: gate (ticks cut off the end of each note)
                    B(at(v, vGate)) = B(at(p, 1));
                    adv(2);
                    break;
                case 0xFA:   // L_0998: pitch bend per tick
                    B(at(v, vBend)) = B(at(p, 1));
                    adv(2);
                    break;
                case 0xF9:   // L_09B2: volume
                    B(at(v, vVol)) = B(at(p, 1));
                    adv(2);
                    setVolume(B(at(v, vVol)));
                    break;
                case 0xF8:   // L_09DE: volume sweep: rate, step
                    B(at(v, vVolRate)) = B(at(p, 1));
                    B(at(v, vVolStep)) = B(at(p, 2));
                    B(at(v, vVolCount)) = 1;
                    adv(3);
                    break;
                case 0xF7:   // L_0A10: transpose (F-number offset)
                    B(at(v, vTranspose)) = B(at(p, 1));
                    adv(2);
                    break;
                }
            }
            // L_0A58: a note (or a rest: note 0) and its duration
            const std::uint16_t p = W(at(v, vPtr));
            B(at(v, vNote)) = B(p);
            B(v) = B(at(p, 1));
            setW(at(v, vPtr), at(W(at(v, vPtr)), 2));
            if (B(at(v, vNote)) == 0 || B(v) == 0) {
                keyOff();
            } else {
                B(at(v, vGateCount)) = static_cast<std::uint8_t>(B(v) - sx(B(at(v, vGate))));
                noteOn(B(at(v, vNote)), B(at(v, vVol)), sx(B(at(v, vTranspose))));
            }
        }
    }
    // L_0AEB: the bend and the volume sweep run on every voice, sounding or not.
    if (B(at(v, vBend)) != 0) bend(sx(B(at(v, vBend))));
    if (B(at(v, vVolCount)) != 0 && --B(at(v, vVolCount)) == 0) {
        B(at(v, vVolCount)) = B(at(v, vVolRate));
        if (B(at(v, vVolStep)) != 0) {
            B(at(v, vVol)) = static_cast<std::uint8_t>(B(at(v, vVol)) + B(at(v, vVolStep)));
            setVolume(B(at(v, vVol)));
        }
    }
    ++B(kChannel);
}

void Adlib::generate() {   // sub_1991
    if (W(kGenLevel) == 0 || W(kGenSpace) == 0) {   // L_1951: stop
        B(kGenOn) = 0;
        setW(at(voice(0), vPtr), kFxStop);
        setW(at(voice(1), vPtr), kFxStop);
        setW(at(voice(2), vPtr), kFxStop);
        setW(kGenPrev, 0);
        return;
    }
    random(false);   // CF from CMP w_D2280, 0
    if (W(kGenLevel) >= 4) {   // sub_19D4: plus the bass ostinato on voice 0
        bassVoice();
        if (W(kGenCount) == 0) setW(kGenCount, W(kGenPeriod));
        setW(kGenCount, static_cast<std::uint16_t>(W(kGenCount) - 1));
        if (W(kGenLevel) == 5) {   // L_1A09: and the counter-melody on voice 1
            if (W(kGenPrev) != 5) {
                setBassVolume(0x2F);
                startVoice(1, kGenRest, kGenChain);
                startVoice(2, kGenRest);
                std::uint16_t ax = static_cast<std::uint16_t>(W(kGenCount) - 1);
                if (static_cast<std::int16_t>(ax) < 0) ax = W(kGenPeriod);
                setRest(ax);
            }
            if (B(voice(2)) == 1) pickPhrase();
        } else {
            if (W(kGenPrev) != 4 && W(kGenPrev) == 5) stopVoices12();
            phraseVoice2();
            phraseGap();
        }
    } else {   // sub_19B9: voice 2's phrases only
        if (W(kGenPrev) >= 4) {
            stopVoice0();
            if (W(kGenPrev) >= 4) stopVoices12();
        }
        phraseVoice2();
        phraseGap();
    }
    setW(kGenPrev, W(kGenLevel));
}

void Adlib::bassVoice() {   // sub_1915 (the word at w_D2322: duration and bend step)
    if (W(voice(0)) == 0) {   // L_1936
        setW(kGenPeriod, 0x70);
        setW(kGenCount, 0x72);
        setBassVolume(0x21);
        startVoice(0, kGenBass);
    } else if (W(voice(0)) == 1) {
        setW(at(voice(0), vStart), W(at(kGenBassStreams, W(kRand) & 6)));
    }
}

void Adlib::phraseVoice2() {   // sub_1A48
    if (B(voice(2)) != 0) return;
    startVoice(2, kGenRest);
    pickPhrase();
}

void Adlib::pickPhrase() {   // sub_1A5C: voice 2's next phrase, at random
    const auto lv = static_cast<std::uint16_t>(W(kGenLevel) - 1);
    const auto bx = static_cast<std::uint16_t>(lv << 4);
    const std::uint16_t r = random((lv >> 12) & 1);   // CF = the last bit SHL shifted out
    std::uint16_t a;
    if (W(kGenLevel) == 1) a = at(kGenFrag1, static_cast<std::uint16_t>((r & 0x1E) + bx));
    else a = at(kGenFrag, static_cast<std::uint16_t>((r & 0x0E) + bx));
    setW(at(voice(2), vStart), W(a));   // w_D2360
}

void Adlib::phraseGap() {   // sub_1A91
    std::uint16_t ax = static_cast<std::uint16_t>((W(kRand) & 0xFF) + 1);
    ax = static_cast<std::uint16_t>((ax / W(kGenLevel)) | 1);
    ax = static_cast<std::uint16_t>(W(kGenSpace) * 0x14 + ax);
    if (static_cast<std::int16_t>(ax) > 0xFF) ax = 0xFF;
    setRest(ax);
}

void Adlib::engineVoice() {   // sub_135E: the engine's (or fx_35's) instruments and pitch
    B(kEnabled) = 0;
    B(kChannel) = 8;
    std::uint8_t n;
    if (B(kHeld) == 0x14) {
        loadInstrument(0x19);
        B(kChannel) = 7;
        n = 0x29;
    } else {
        n = B(kEngOn) == 1 ? 0x12 : 0x0B;
    }
    loadInstrument(n);
    // sub_1332: start from the pitch of the current speed
    const std::uint16_t f = W(at(kEngFreqs, B(kEngSpeed) * 2));
    B(kFreqHi) = static_cast<std::uint8_t>(f >> 8);
    B(kFreqLo) = static_cast<std::uint8_t>(f);
    // sub_14C7: channel 8's carrier level
    opl(0x55, static_cast<std::uint8_t>(((shadow(0x55) & 0xC0) | 1) + B(kHeld)));
    // sub_147F: channel 8 on
    opl(0xA8, B(kFreqLo));
    opl(0xB8, static_cast<std::uint8_t>(B(kFreqHi) | 0x20));
    if (B(kHeld) == 0x14) {
        B(kFreq7Hi) = 0x1A;   // sub_1353
        B(kFreq7Lo) = 0x85;
        opl(0x54, static_cast<std::uint8_t>((shadow(0x54) & 0xC0) | 0x17));   // sub_14E0
        opl(0xA7, B(kFreq7Lo));                                               // sub_1495
        opl(0xB7, static_cast<std::uint8_t>(B(kFreq7Hi) | 0x20));
    }
    B(kEnabled) = 0xFF;
}

void Adlib::slide() {   // sub_140A: channel 8's pitch towards w_D2290
    B(kEnabled) = 0;
    auto bx = static_cast<std::uint16_t>(shadow(0xB8) << 8 | shadow(0xA8));
    bx = static_cast<std::uint16_t>((bx + W(kSlide)) & 0x1FFF);
    B(kFreqHi) = static_cast<std::uint8_t>(bx >> 8);
    B(kFreqLo) = static_cast<std::uint8_t>(bx);
    opl(0xA8, static_cast<std::uint8_t>(bx));
    opl(0xB8, static_cast<std::uint8_t>((bx >> 8) | (shadow(0xB8) & 0x20)));
    if (bx == W(kTarget)) {   // (a target off the step's grid is never reached: it wraps)
        setW(kSlide, 0);
        if (B(kEngSpeed) == 0 && static_cast<std::int16_t>(W(kTarget)) <= 0x1DA1)
            opl(0xB8, static_cast<std::uint8_t>(shadow(0xB8) & 0xDF));   // slid down: off
    }
    B(kEnabled) = 0xFF;   // L_1479
}

void Adlib::engine(std::uint8_t which, int a) {   // fx_26 (which = FFh) / fx_27 (0)
    B(kEngWhich) = which;
    const auto al = static_cast<std::uint8_t>(a);
    const auto retarget = [&] {   // sub_1346, sub_13E2, b_D2287 = speed
        engineTarget(a);
        slideDirection();
        B(kEngSpeed) = al;
    };
    const auto start = [&] {   // L_1246: an engine starts (speed 0: nothing)
        if (al == 0) return;
        if (B(kEngWhich) == 0xFF) {
            B(kEngOn) = 1;
            engineVoice();
            B(kEngSpeed26) = al;
        } else {
            B(kEngOn) = 2;
            engineVoice();
            B(kEngSpeed27) = al;
        }
        retarget();
    };
    if (B(kEngOn) == 0) {
        start();
    } else if (B(kEngOn) == 1) {
        if (B(kEngWhich) == 0xFF) {   // engine 26 again: a new speed
            B(kEngSpeed26) = al;
            if (B(kEngSpeed) == 0) return start();
            retarget();
        } else {   // L_12B5: engine 27 takes over, from its last speed
            engineKeyOff();
            B(kEngOn) = 2;
            B(kEngSpeed) = B(kEngSpeed27);
            if ((B(kEngSpeed) | al) == 0) return start();
            engineVoice();
            B(kEngSpeed27) = al;
            retarget();
            B(kEnabled) = 0xFF;
        }
    } else {
        if (B(kEngWhich) == 0xFF) {   // L_12E1: engine 26 takes over
            engineKeyOff();
            B(kEngOn) = 1;
            B(kEngSpeed) = B(kEngSpeed26);
            if ((B(kEngSpeed) | al) == 0) return start();
            engineVoice();
            B(kEngSpeed26) = al;
            retarget();
            B(kEnabled) = 0xFF;
        } else {   // L_1317: engine 27 again
            B(kEngSpeed27) = al;
            if (B(kEngSpeed) == 0) return start();
            retarget();
        }
    }
}

void Adlib::play(int n, int a, int b) {
    // CMP BX, 2Eh / JG; a negative n would index before fx_table (garbage): ignored.
    if (n > 0x2E || n < 0) return;
    switch (n) {
    case 0: fx0(); break;
    case 1:   // stop the music: voices 0-6 end after their current note
        for (int i = 0; i < 7; ++i) setW(at(voice(i), vPtr), kFxStop);
        break;
    case 2:   // title theme
        startVoice(0, 0x079A); startVoice(1, 0x0818); startVoice(2, 0x08CE); startVoice(3, 0x0A08);
        startVoice(4, 0x0A96); startVoice(5, 0x0B36); startVoice(6, 0x0BD2);
        break;
    case 3: startVoice(3, 0x1EA2); break;
    case 4: startVoice(4, 0x1EB0); break;
    case 5: startVoice(3, 0x1ECC); break;
    case 6: startVoice(3, 0x1EBE); break;
    case 7: startVoice(5, 0x1F1A); break;
    case 8: startVoice(6, 0x1F22); break;
    case 9: startVoice(3, 0x1ED4); break;
    case 10: startVoice(3, 0x2016); break;
    case 11: startVoice(3, 0x1F50); break;
    case 12: startVoice(3, 0x1F58); break;
    case 13: startVoice(4, 0x1EDE); break;
    case 14: startVoice(4, 0x1EE6); break;
    case 15: startVoice(4, 0x1F64); break;
    case 16: startVoice(3, 0x1F6C); break;
    case 17: startVoice(3, 0x1F78); break;
    case 18: startVoice(4, 0x1EEE); break;
    case 19: startVoice(5, 0x1F2A); break;
    case 20: startVoice(5, 0x1F32); break;
    case 21: startVoice(5, 0x1F84); break;
    case 22: case 24: case 25: break;   // fx_22, fx_24: RET; fx_25: sound_tick's RET
    case 23: startVoice(4, 0x1F02); break;
    case 26: engine(0xFF, a); break;    // car engines: a = speed (0 = off)
    case 27: engine(0, a); break;
    case 28: startVoice(3, 0x1F8C); break;
    case 29: startVoice(3, 0x1F3C); break;
    case 30: startVoice(4, 0x1F96); break;
    case 31: startVoice(3, 0x1FA2); break;
    case 32: startVoice(4, 0x200E); startVoice(3, 0x200E); break;
    case 33: startVoice(3, 0x1FAE); break;
    case 34: startVoice(4, 0x1FB6); break;
    case 35:   // a held tone on channels 7 and 8, sliding up
        B(kHeld) = 0x14;
        B(kEngSpeed) = 5;
        setW(kTarget, 0x1FF0);
        engineVoice();
        slideDirection();
        break;
    case 36:   // ...released: channel 7 off, channel 8 slides down and off
        setW(kTarget, 0x1DA0);
        slideDirection();
        B(kEnabled) = 0;
        opl(0xB7, static_cast<std::uint8_t>(shadow(0xB7) & 0xDF));
        B(kEnabled) = 0xFF;
        B(kEngSpeed) = 0;
        B(kEngOn) = 0;
        B(kHeld) = 0;
        break;
    case 37:   // pause: bursts saved, sequencer off, every carrier silent
        B(kSavedA) = B(kNoiseCountA);
        B(kNoiseCountA) = 0;
        B(kSavedB) = B(kNoiseCountB);
        B(kNoiseCountB) = 0;
        B(kEnabled) = 0;   // (the timer stays as it is: sound_tick returns at once)
        for (int reg : {0x43, 0x44, 0x45, 0x4B, 0x4C, 0x4D, 0x53, 0x54, 0x55})   // sub_0E28
            oplC(reg, shadow(reg) | 0x3F);
        break;
    case 38: {   // resume: the voices' levels back (sub_0E36), then channels 7/8's
        const int regs[] = {0x43, 0x44, 0x45, 0x4B, 0x4C, 0x4D, 0x53};
        for (int i = 0; i < 7; ++i)
            oplC(regs[i], (shadow(regs[i]) & 0xC0) | (B(at(voice(i), vVol)) ^ 0x3F));
        oplC(0x54, (shadow(0x54) & 0xC0) | (0x28 ^ 0x3F));
        oplC(0x55, (shadow(0x55) & 0xC0) | (0x3E ^ 0x3F));
        B(kNoiseCountA) = B(kSavedA);
        B(kNoiseCountB) = B(kSavedB);
        if ((B(kNoiseCountB) | B(kNoiseCountA)) != 0) timerFast();
        B(kEnabled) = 0xFF;
        break;
    }
    case 39:
        startVoice(0, 0x1A7E); startVoice(1, 0x1AF0); startVoice(2, 0x1B62); startVoice(3, 0x1C22);
        startVoice(4, 0x1C9C); startVoice(5, 0x1D20); startVoice(6, 0x1DA4);
        break;
    case 40:
        startVoice(0, 0x1E28); startVoice(1, 0x1E4C); startVoice(2, 0x1E70); startVoice(3, 0x1E8A);
        startVoice(4, 0x0794); startVoice(5, 0x0796); startVoice(6, 0x0798);
        break;
    case 41:
        startVoice(0, 0x0C2A); startVoice(1, 0x0CFE); startVoice(2, 0x0D4C); startVoice(3, 0x0D8C);
        startVoice(4, 0x0DB8); startVoice(5, 0x0E4E); startVoice(6, 0x0790);
        break;
    case 42:
        startVoice(0, 0x1392); startVoice(1, 0x13F8); startVoice(2, 0x1454); startVoice(3, 0x14A4);
        startVoice(4, 0x14DE); startVoice(5, 0x150C); startVoice(6, 0x0792);
        break;
    case 43:
        startVoice(0, 0x153A); startVoice(1, 0x1582); startVoice(2, 0x1634); startVoice(3, 0x1734);
        startVoice(4, 0x17B0); startVoice(5, 0x18EC); startVoice(6, 0x19CE);
        break;
    case 44:   // generated score: a = intensity 1-5, b = phrase spacing
        setW(kGenLevel, static_cast<std::uint16_t>(a));
        setW(kGenSpace, static_cast<std::uint16_t>(b));
        B(kGenOn) = 0xFF;
        break;
    case 45: startVoice(4, 0x1FBE); break;
    case 46: startVoice(4, 0x1FEA); break;
    default: break;
    }
}

}  // namespace

std::unique_ptr<SoundDriver> makeAdlib(const DriverImage& image) {
    if (image.ident.rfind("Covert Adlib", 0) != 0 || image.data.size() < kDataEnd)
        throw std::runtime_error("ASOUND.CA: not the Covert Action AdLib driver");
    return std::make_unique<Adlib>(image);
}

}  // namespace reaction::audio
