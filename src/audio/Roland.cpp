// RSOUND.CA ("RolandMT-32g15-10-90"), the Roland MT-32 / LAPC-1 driver, ported
// from covert-decomp src/DRIVERS/RSOUND.ASM. Offsets in comments and names are
// the DATA-segment offsets of the original (w_D1810 = DATA:1810h, etc.).
//
// The MT-32 is driven through an MPU-401 in UART mode: init resets it, sends
// command 3Fh (UART), resets every part and uploads the game's patches, timbres
// and rhythm setup by SysEx (start_music, 42 messages), each one followed by a
// calibrated wait. The sequencer then plays eight voices (sub_1084): voices 1-7
// on MIDI channels 2-8 (status nibble 1-7), voice 8 on channel 10 (nibble 9,
// rhythm). Each voice steps through a byte stream of notes (note, duration in
// ticks) and commands F2h-FFh (program, volume, pan, bend, velocity, gate time,
// sweeps, chords, loops, random patches). Running status is kept in b_D2D9C.
//
// Effect 44 starts a generated "tension" score (sub_0CC6) on voices 1-3, from
// play_sound(44, a, b): a = intensity 1-5 (w_D1810), b = how far apart its phrases
// are (w_D1812). Effects 26/27 are the chase's car engines (a = speed), sent
// straight to channel 8 (nibble 7).
#include <stdexcept>

#include "audio/Driver.h"

namespace redux::audio {
namespace {

// DATA offsets.
enum : std::uint16_t {
    kFxStop = 0x0050,       // fx_1_data: volume 100, bend centre, end
    kRestThen = 0x04BE,     // w_D04BE: a rest of b_D04BF ticks, then FDh (jump)
    kRestLen = 0x04BF,
    kStarted = 0x17E8,      // b_D17E8: start_music done
    kRand = 0x17F2,         // w_D17F2: random()
    kGenCount = 0x180A, kGenPeriod = 0x180C, kGenPrev = 0x180E,
    kGenLevel = 0x1810, kGenSpace = 0x1812, kGenOn = 0x1814, kRand2 = 0x1815,
    kGenFrag1 = 0x1817, kGenFrag = 0x1827, kGenBass = 0x1877,
    kEngBase = 0x187F, kEngPrevBase = 0x1881, kEngSpeed = 0x1883, kEngPrevSpeed = 0x1885,
    kEngNote = 0x1887,
    kSysexHeader = 0x1889,  // F0 41 10 16 12 (Roland, unit 17, MT-32, DT1), FFh
    kSysexDisplay = 0x188F, // "   Covert Action    " on the LCD
    kStatus = 0x2D9C,       // b_D2D9C: running status
    kChord = 0x2D9E,        // b_D2D9E: the command loop ended on a chord
    kEnabled = 0x2DA0,      // b_D2DA0: sequencer on
    kVoices = 0x2DA2,       // 8 voices of 22h bytes
    kChannel = 0x2EB2,      // b_D2EB2: channel of the voice being played
    kNotesOn = 0x2EB4,      // 4 sounding notes per channel (FFh = none)
};
constexpr std::uint16_t kVoiceSize = 0x22;
constexpr std::uint16_t voice(int i) { return static_cast<std::uint16_t>(kVoices + i * kVoiceSize); }

// Voice fields.
enum : std::uint16_t {
    vDur = 0x00, vBendStep = 0x01, vVolStep = 0x02, vPanStep = 0x03, vNote = 0x04, vProg = 0x05,
    vVel = 0x06, vGate = 0x07, vGateCount = 0x08, vVolCount = 0x09, vBendCount = 0x0A,
    vPanCount = 0x0B, vVol = 0x0C, vBend = 0x0D, vPan = 0x0E, vVolRate = 0x0F, vBendRate = 0x10,
    vPanRate = 0x11, vBendLeft = 0x12, vStart = 0x14, vPtr = 0x16, vMark1 = 0x18, vMark2 = 0x1A,
    vCount1 = 0x1C, vCount2 = 0x1E, vChain = 0x20,
};

// start_music's SysEx messages, in order (FFh-terminated data after the header).
constexpr std::uint16_t kSetup[] = {
    0x188F, 0x18A7, 0x18C2, 0x19C6, 0x19FA, 0x1A81, 0x1B7B, 0x1C75, 0x1CFC, 0x1D83, 0x1DCE,
    0x1E19, 0x1E64, 0x1EEB, 0x1F42, 0x1FC9, 0x2050, 0x214A, 0x21A1, 0x2228, 0x22AF, 0x2306,
    0x235D, 0x23E4, 0x246B, 0x24F2, 0x25EC, 0x2673, 0x2736, 0x27BD, 0x2844, 0x289B, 0x28F2,
    0x2979, 0x2A00, 0x2A87, 0x2B0E, 0x2B59, 0x2C1C, 0x2C73, 0x2CCA, 0x2D51,
};

// The control-change status bytes of the parts fx_37 / fx_38 touch.
constexpr std::uint8_t kPartControls[] = {0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB9};

class Roland final : public SoundDriver {
public:
    explicit Roland(const DriverImage& img) : m_(img) {}

    void init() override {   // rsnd_init
        resetVoices();
        // hook_interrupts (timer at 4C90h); mpu_reset; mpu_command(3Fh): UART mode.
        startMusic();
    }

    void shutdown() override { fx0(); }   // rsnd_shutdown: fx_0, mpu_reset, unhook

    void tick() override {   // timer_isr -> sound_tick
        ++tickCount_;
        if (m_.b(kGenOn)) generate();
        random();
        sequence();
    }

    void play(int n, int a, int b) override;   // rsnd_play_sound

private:
    Mem m_;
    std::uint16_t tickCount_ = 0;   // cs:tick_count

    std::uint8_t& B(std::uint16_t a) { return m_.b(a); }
    std::uint16_t W(std::uint16_t a) const { return m_.w(a); }
    void setW(std::uint16_t a, std::uint16_t v) { m_.setW(a, v); }
    static std::uint16_t at(std::uint16_t base, int off) { return static_cast<std::uint16_t>(base + off); }

    void out(std::uint8_t byte) { if (sink_) sink_->midi(byte); }   // mpu_data

    // ---- set-up -------------------------------------------------------------
    void resetVoices() {   // reset_voices
        setW(kGenSpace, 0);
        setW(kGenLevel, 0);
        setW(kGenPrev, 0);
        B(kEnabled) = 0;
        for (int i = 0; i < 8; ++i)
            for (int k = 0; k < 4; ++k) B(at(voice(i), k)) = 0;
        B(kEnabled) = 0xFF;
    }

    void sendSysex(std::uint16_t a) {   // send_sysex
        for (std::uint16_t p = kSysexHeader; B(p) != 0xFF; ++p) out(B(p));
        std::uint8_t sum = 0;
        for (; B(a) != 0xFF; ++a) {
            sum = static_cast<std::uint8_t>(sum + B(a));
            out(B(a));
        }
        out(static_cast<std::uint8_t>((~sum + 1) & 0x7F));
        out(0xF7);
        // ...then a delay loop of w_D17EE iterations (calibrated at init).
    }

    void startMusic() {   // start_music
        if (B(kStarted)) return;
        fx0();
        for (std::uint16_t a : kSetup) sendSysex(a);
        B(kStarted) = 0xFF;
    }

    void fx0() {   // fx_0: silence and reset every part
        resetVoices();
        for (int c = 9; c >= 1; --c) {
            const auto s = static_cast<std::uint8_t>(0xB0 | c);
            B(kStatus) = s;
            for (std::uint8_t byte : {s, std::uint8_t(0x7B), std::uint8_t(0), std::uint8_t(0x79),
                                      std::uint8_t(0), std::uint8_t(7), std::uint8_t(0x64),
                                      std::uint8_t(0x0A), std::uint8_t(0x40)})
                out(byte);   // all notes off, reset controllers, volume 100, pan centre
        }
        sendSysex(kSysexDisplay);
    }

    // ---- voices -------------------------------------------------------------
    void start(int i, std::uint16_t data, std::uint16_t chain = 0) {   // sub_0546
        const std::uint16_t v = voice(i);
        B(at(v, vBendStep)) = 0;
        B(at(v, vVolStep)) = 0;
        B(at(v, vPanStep)) = 0;
        B(at(v, vVolRate)) = 0xFF;
        B(at(v, vVol)) = 0x64;
        B(at(v, vBend)) = 0x40;
        B(at(v, vPan)) = 0x40;
        for (int f : {vStart, vPtr, vMark1, vMark2}) setW(at(v, f), data);
        setW(at(v, vCount1), 0);
        setW(at(v, vCount2), 0);
        B(at(v, vGate)) = 0;
        setW(at(v, vChain), chain);
        B(at(v, vDur)) = 1;
    }

    void restart(int i) {   // L_0592 (fx_47..fx_54)
        const std::uint16_t v = voice(i);
        B(at(v, vBendStep)) = 0;
        B(at(v, vVolRate)) = 0xFF;
        B(at(v, vVolStep)) = 0;
        const std::uint16_t s = W(at(v, vStart));
        for (int f : {vPtr, vMark1, vMark2}) setW(at(v, f), s);
        setW(at(v, vCount1), 0);
        setW(at(v, vCount2), 0);
        B(at(v, vGate)) = 0;
        B(at(v, vDur)) = 1;
    }

    void stopAt(int i) { setW(at(voice(i), vPtr), kFxStop); }

    // ---- MIDI messages of the sequencer (C code from 0E02h) ------------------
    std::uint16_t notesOn(int k = 0) const {
        return static_cast<std::uint16_t>(kNotesOn + m_.b(kChannel) * 4 + k);
    }
    void status(int kind) {   // send the status byte unless it is the running status
        const auto s = static_cast<std::uint8_t>(m_.b(kChannel) | kind);
        if (s != B(kStatus)) {
            out(s);
            B(kStatus) = s;
        }
    }
    void notesOff() {   // sub_0E02
        for (int k = 0; k < 4; ++k) {
            const std::uint8_t n = B(notesOn(k));
            if (n == 0xFF) break;
            status(0x90);
            out(n);
            out(0);
            B(notesOn(k)) = 0xFF;
        }
    }
    void noteOn(std::uint8_t note, std::uint8_t vel) { status(0x90); out(note); out(vel); }   // sub_0EBA
    void program(std::uint8_t p) { status(0xC0); out(p); }                                   // sub_0F1A
    void volume(std::uint8_t v) { status(0xB0); out(7); out(v); }                            // sub_0F6C
    void bend(std::uint8_t v) { status(0xE0); out(0); out(v); }                              // sub_0FCA
    void pan(std::uint8_t v) { status(0xB0); out(0x0A); out(v); }                            // sub_1026

    void random() { setW(kRand, ror3(static_cast<std::uint16_t>(0x9248 + W(kRand)))); }

    // random2: ADC with the caller's carry, then ADC tick_count with the carry out.
    std::uint16_t random2(bool cf) {
        std::uint32_t s = std::uint32_t(W(kRand2)) + 0x7131 + (cf ? 1 : 0);
        s = (s & 0xFFFF) + tickCount_ + (s >> 16);
        setW(kRand2, static_cast<std::uint16_t>(s));
        return static_cast<std::uint16_t>(s);
    }

    void stepVoice(std::uint16_t v);   // sub_1084

    void sequence() {   // sub_170E
        if (!B(kEnabled)) return;
        B(kChannel) = 1;
        for (int i = 0; i < 7; ++i) stepVoice(voice(i));
        B(kChannel) = 9;
        stepVoice(voice(7));
    }

    // ---- effect 44: the generated score (sub_0CC6 and helpers) ---------------
    void generate();
    void pickFragment();   // sub_0D91
    void setRestLen(std::uint16_t ax) { B(kRestLen) = static_cast<std::uint8_t>(ax); }   // sub_0DEC
    void setBassVelocity(std::uint8_t al) {   // sub_0CB9
        B(0x08E9) = al;
        B(0x0901) = al;
        B(0x0919) = al;
        B(0x0931) = al;
    }
    void stopVoice1() { setW(0x2DB8, kFxStop); }   // sub_0CA2
    void stopVoices23() {                          // sub_0CAB (+ sub_0CB7)
        setW(0x2DDA, kFxStop);
        setW(0x2DFC, kFxStop);
        setBassVelocity(0x45);
    }
    void phraseVoice3() {   // sub_0D7D
        if (B(voice(2)) != 0) return;
        start(2, kRestThen);
        pickFragment();
    }
    void phraseGap() {   // sub_0DC6
        std::uint16_t ax = static_cast<std::uint16_t>((W(kRand2) & 0xFF) + 1);
        ax = static_cast<std::uint16_t>((ax / W(kGenLevel)) | 1);
        ax = static_cast<std::uint16_t>(W(kGenSpace) * 20 + ax);
        if (static_cast<std::int16_t>(ax) > 0xFF) ax = 0xFF;
        setRestLen(ax);
    }

    void engine(std::uint16_t base, int speed);   // fx_26 / fx_27
};

void Roland::stepVoice(std::uint16_t v) {
    if (B(v) != 0) {
        if (B(at(v, vGateCount)) != 0 && --B(at(v, vGateCount)) == 0) notesOff();
        if (--B(v) == 0) {
            bool chord = false;
            for (;;) {   // L_10C5: commands until a note or a chord
                const std::uint16_t p = W(at(v, vPtr));
                const std::uint8_t c = B(p);
                if (!(c & 0x80) || sx(c) <= sx(0xF1)) break;
                const auto arg = [&](int k) -> std::uint8_t& { return B(at(p, k)); };
                const auto adv = [&](int k) { setW(at(v, vPtr), at(W(at(v, vPtr)), k)); };
                switch (c) {
                case 0xFF:   // inner loop: FF 00 marks, FF n repeats n more times
                    if (W(at(v, vCount1)) == 0) {
                        if (arg(1) == 0) {
                            adv(2);
                            setW(at(v, vMark1), W(at(v, vPtr)));
                            setW(at(v, vCount1), 0);
                        } else {
                            setW(at(v, vCount1), static_cast<std::uint16_t>(sx(arg(1))));
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
                case 0xFE:   // outer loop
                    if (W(at(v, vCount2)) == 0) {
                        if (arg(1) == 0) {
                            adv(2);
                            setW(at(v, vMark2), W(at(v, vPtr)));
                            setW(at(v, vCount1), 0);
                            setW(at(v, vCount2), 0);
                        } else {
                            setW(at(v, vCount2), static_cast<std::uint16_t>(sx(arg(1))));
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
                case 0xFD:   // from the top, or on to the chained stream
                    if (W(at(v, vChain)) == 0) {
                        setW(at(v, vPtr), W(at(v, vStart)));
                    } else {
                        const std::uint16_t ch = W(at(v, vChain));
                        for (int f : {vStart, vPtr, vMark1, vMark2}) setW(at(v, f), ch);
                    }
                    break;
                case 0xFC:   // program
                    B(at(v, vProg)) = arg(1);
                    adv(2);
                    program(static_cast<std::uint8_t>(sx(arg(1))));
                    break;
                case 0xFB: B(at(v, vGate)) = arg(1); adv(2); break;   // gate (ticks cut off)
                case 0xFA:   // bend sweep: rate, step, ticks
                    B(at(v, vBendRate)) = arg(1);
                    B(at(v, vBendStep)) = arg(2);
                    B(at(v, vBendLeft)) = arg(3);
                    B(at(v, vBendCount)) = 1;
                    adv(4);
                    break;
                case 0xF9: B(at(v, vVel)) = arg(1); adv(2); break;   // velocity
                case 0xF8:   // volume sweep: rate, step
                    B(at(v, vVolRate)) = arg(1);
                    B(at(v, vVolStep)) = arg(2);
                    B(at(v, vVolCount)) = 1;
                    adv(3);
                    break;
                case 0xF7:   // bend
                    B(at(v, vBend)) = arg(1);
                    adv(2);
                    bend(static_cast<std::uint8_t>(sx(arg(1))));
                    break;
                case 0xF6:   // volume
                    B(at(v, vVol)) = arg(1);
                    adv(2);
                    volume(static_cast<std::uint8_t>(sx(arg(1))));
                    break;
                case 0xF5: {   // chord: n notes, duration
                    const std::int8_t n = static_cast<std::int8_t>(arg(1));
                    std::int8_t k = 0;
                    for (; k < n; ++k) {
                        B(notesOn(k)) = arg(2 + k);
                        noteOn(B(notesOn(k)), B(at(v, vVel)));
                    }
                    for (k = n; k < 4; ++k) B(notesOn(k)) = 0xFF;
                    B(v) = arg(2 + n);
                    B(at(v, vGateCount)) = static_cast<std::uint8_t>(B(v) - sx(B(at(v, vGate))));
                    adv(n + 3);
                    B(kChord) = 1;
                    chord = true;
                    break;
                }
                case 0xF4:   // pan
                    B(at(v, vPan)) = arg(1);
                    pan(B(at(v, vPan)));
                    adv(2);
                    break;
                case 0xF3:   // pan sweep: rate, step (0: back to the centre)
                    B(at(v, vPanRate)) = arg(1);
                    B(at(v, vPanStep)) = arg(2);
                    if (B(at(v, vPanStep)) == 0) pan(0x40);
                    B(at(v, vPanCount)) = 1;
                    adv(3);
                    break;
                case 0xF2: {   // patch the stream: n choices, write one at a relative offset
                    const std::int16_t n = sx(arg(1));
                    const std::uint16_t q = at(p, 2);
                    random();
                    const std::int16_t k = sx(static_cast<std::uint8_t>((n - 1) & W(kRand)));
                    const std::uint8_t val = B(at(q, k));
                    B(at(q, n + 1 + sx(B(at(q, n))))) = val;
                    adv(n + 3);
                    break;
                }
                }
                if (B(kChord)) break;
            }
            if (B(kChord)) {
                B(kChord) = 0;   // L_162A
            } else if (!chord) {
                // L_154E: a note (or a rest: note 0) and its duration
                const std::uint16_t p = W(at(v, vPtr));
                B(at(v, vNote)) = B(p);
                B(v) = B(at(p, 1));
                setW(at(v, vPtr), at(p, 2));
                if (B(at(v, vNote)) == 0 || B(v) == 0) {
                    notesOff();
                } else {
                    B(at(v, vGateCount)) = static_cast<std::uint8_t>(B(v) - sx(B(at(v, vGate))));
                    // A negative gate ties the note to the previous one of the same pitch.
                    if (!(sx(B(at(v, vGate))) < 0 && B(notesOn()) == B(at(v, vNote))))
                        noteOn(B(at(v, vNote)), B(at(v, vVel)));
                    B(notesOn()) = B(at(v, vNote));
                }
            }
        }
    }
    // L_162F: the sweeps run on every voice, sounding or not.
    if (B(at(v, vVolStep)) != 0 && --B(at(v, vVolCount)) == 0) {
        B(at(v, vVolCount)) = B(at(v, vVolRate));
        B(at(v, vVol)) = static_cast<std::uint8_t>(B(at(v, vVol)) + B(at(v, vVolStep)));
        volume(B(at(v, vVol)));
    }
    if (B(at(v, vBendStep)) != 0) {
        if (--B(at(v, vBendCount)) == 0) {
            B(at(v, vBendCount)) = B(at(v, vBendRate));
            B(at(v, vBend)) = static_cast<std::uint8_t>(B(at(v, vBend)) + B(at(v, vBendStep)));
            bend(B(at(v, vBend)));
        }
        if (--B(at(v, vBendLeft)) == 0) B(at(v, vBendStep)) = 0;
    }
    if (B(at(v, vPanStep)) != 0 && --B(at(v, vPanCount)) == 0) {
        B(at(v, vPanCount)) = B(at(v, vPanRate));
        B(at(v, vPan)) = static_cast<std::uint8_t>(B(at(v, vPan)) + B(at(v, vPanStep)));
        pan(B(at(v, vPan)));
    }
    ++B(kChannel);
}

void Roland::pickFragment() {   // sub_0D91: voice 3's next phrase, at random
    const auto lv = static_cast<std::uint16_t>(W(kGenLevel) - 1);
    const auto bx = static_cast<std::uint16_t>(lv << 4);
    const std::uint16_t r = random2((lv >> 12) & 1);   // CF = the last bit SHL shifted out
    std::uint16_t a;
    if (W(kGenLevel) == 1) a = at(at(kGenFrag1, bx), r & 0x1E);
    else a = at(at(kGenFrag, bx), r & 0x0E);
    setW(at(voice(2), vStart), W(a));   // w_D2DFA
}

void Roland::generate() {   // sub_0CC6
    if (W(kGenLevel) == 0 || W(kGenSpace) == 0) {   // L_0C86: stop
        B(kGenOn) = 0;
        stopAt(0);
        stopAt(1);
        stopAt(2);
        setW(kGenPrev, 0);
        return;
    }
    random2(false);   // CF from CMP w_D1812, 0
    if (W(kGenLevel) < 4) {   // sub_0CEE: voice 3's phrases only
        if (W(kGenPrev) >= 4) {
            stopVoice1();
            stopVoices23();
        }
        phraseVoice3();
        phraseGap();
    } else {   // sub_0D09: plus the bass ostinato on voice 1
        const std::uint16_t v1 = voice(0);
        if (W(v1) == 0) {   // sub_0C4A / L_0C6B
            setW(kGenPeriod, 0x70);
            setW(kGenCount, 0x72);
            setBassVelocity(0x45);
            start(0, 0x08E6);
        } else if (W(v1) == 1) {
            setW(at(v1, vStart), W(at(kGenBass, W(kRand2) & 6)));
        }
        if (W(kGenCount) == 0) setW(kGenCount, W(kGenPeriod));
        setW(kGenCount, static_cast<std::uint16_t>(W(kGenCount) - 1));
        if (W(kGenLevel) == 5) {   // L_0D3E: and the counter-melody on voice 2
            if (W(kGenPrev) != 5) {
                setBassVelocity(0x5F);
                start(1, kRestThen, 0x087E);
                start(2, kRestThen);
                std::uint16_t ax = static_cast<std::uint16_t>(W(kGenCount) - 1);
                if (static_cast<std::int16_t>(ax) < 0) ax = W(kGenPeriod);
                setRestLen(ax);
            }
            if (B(voice(2)) == 1) pickFragment();
        } else {
            if (W(kGenPrev) == 5) stopVoices23();
            phraseVoice3();
            phraseGap();
        }
    }
    setW(kGenPrev, W(kGenLevel));
}

void Roland::engine(std::uint16_t base, int speed) {   // fx_26 / fx_27 (L_097B)
    const auto sp = static_cast<std::uint16_t>(speed);
    setW(kEngBase, base);
    setW(kEngSpeed, sp);
    if (sp == 0) {   // L_09DB: engine off
        B(kStatus) = 0x97;
        out(0x97);
        out(static_cast<std::uint8_t>(W(kEngNote)));
        out(0);
        setW(kEngNote, 0xFFFF);
        setW(kEngBase, 0xFFFF);
    } else if (!(W(kEngBase) == W(kEngPrevBase) && sp == W(kEngPrevSpeed))) {
        if (W(kEngPrevSpeed) == 0) {   // engine starts: program 0 on channel 8
            B(kStatus) = 0xC7;
            out(0xC7);
            out(0);
        }
        B(kStatus) = 0x97;
        out(0x97);
        if (W(kEngNote) != 0xFFFF) {
            out(static_cast<std::uint8_t>(W(kEngNote)));
            out(0);
        }
        setW(kEngNote, static_cast<std::uint16_t>(W(kEngSpeed) * 2 + W(kEngBase)));
        out(static_cast<std::uint8_t>(W(kEngNote)));
        out(0x69);
    }
    setW(kEngPrevSpeed, W(kEngSpeed));
    setW(kEngPrevBase, W(kEngBase));
}

void Roland::play(int n, int a, int b) {
    if (n > 0x36 || n < 0) return;   // CMP BX, 36h / JG (and fx_table can't take a negative n)
    switch (n) {
    case 0: fx0(); break;
    case 1:   // stop the music: voices 1-7 end after their current note
        for (int i = 0; i < 7; ++i) stopAt(i);
        break;
    case 2:   // title theme (FINAL, end of case)
        start(0, 0x005E); start(1, 0x00EE); start(2, 0x019C); start(3, 0x02CC);
        start(4, 0x0354); start(5, 0x03F4); start(6, 0x0490);
        break;
    case 3: start(3, 0x1464); break;
    case 4: start(4, 0x1472); break;
    case 5: start(3, 0x14AC); break;
    case 6: start(3, 0x1490); break;
    case 7: start(5, 0x1512); break;
    case 8: start(5, 0x151A); break;
    case 9: start(3, 0x14B4); break;
    case 10: start(3, 0x1562); start(4, 0x1570); break;
    case 11: start(3, 0x15A2); break;
    case 12: start(3, 0x1594); break;
    case 13: start(4, 0x14BE); break;
    case 14: start(4, 0x14C6); break;
    case 15: start(4, 0x14CE); break;
    case 16: start(3, 0x14D6); break;
    case 17: start(3, 0x14DE); break;
    case 18: start(5, 0x14EC); start(7, 0x1428); break;
    case 19: start(2, 0x1522); break;
    case 20: start(2, 0x152A); break;
    case 21: break;
    case 22: start(7, 0x14FC); break;
    case 23: case 25: start(1, 0x1508); start(7, 0x1502); break;
    case 24: start(7, 0x1510); break;
    case 26: engine(0x2B, a); break;
    case 27: engine(0x30, a); break;
    case 28: start(0, 0x1536); start(4, 0x154C); break;
    case 29: start(0, 0x157E); break;
    case 30: start(0, 0x1586); break;
    case 31: start(0, 0x15AA); break;
    case 32: start(0, 0x15BC); break;
    case 33: start(3, 0x1480); break;
    case 34: start(4, 0x1488); break;
    case 35:   // fx_35: a held note on channel 8
        B(kStatus) = 0xC7; out(0xC7); out(0x0C);
        B(kStatus) = 0x97; out(0x97); out(0x30); out(0x64);
        break;
    case 36: B(kStatus) = 0x97; out(0x97); out(0x30); out(0); break;
    case 37:   // pause: sequencer off, every part silent
        B(kEnabled) = 0;
        for (std::uint8_t s : kPartControls)
            for (std::uint8_t byte : {s, std::uint8_t(7), std::uint8_t(0), std::uint8_t(0x7B), std::uint8_t(0)})
                out(byte);   // (b_D2D9C is not updated: the running status goes stale)
        break;
    case 38: {   // resume: the voices' volumes back
        int i = 0;
        for (std::uint8_t s : kPartControls) {
            out(s);
            out(7);
            out(B(at(voice(i++), vVol)));
        }
        B(kEnabled) = 0xFF;
        break;
    }
    case 39:   // TAC: alarm
        start(0, 0x12C4); start(1, 0x1306); start(2, 0x1382); start(3, 0x13B8); start(4, 0x13F0);
        break;
    case 40: start(0, 0x0946); start(1, 0x0956); start(7, 0x0966); break;
    case 41:
        start(0, 0x097C); start(1, 0x0A5E); start(2, 0x0AB2); start(3, 0x0AF2); start(4, 0x0B1C);
        start(5, 0x0BB0);
        break;
    case 42:
        start(0, 0x0C82); start(1, 0x0D3E); start(2, 0x0DA6); start(3, 0x0E04); start(4, 0x0CDE);
        start(7, 0x0E40);
        break;
    case 43:
        start(0, 0x0E7E); start(1, 0x0EEC); start(2, 0x0FE8); start(3, 0x1158); start(7, 0x11D8);
        break;
    case 44:   // generated score: a = intensity, b = phrase spacing
        setW(kGenLevel, static_cast<std::uint16_t>(a));
        setW(kGenSpace, static_cast<std::uint16_t>(b));
        B(kGenOn) = 0xFF;
        break;
    case 45: start(0, 0x15BE); break;
    case 46: start(0, 0x15D6); break;
    default: restart(n - 47); break;   // 47-54: restart voice 1-8
    }
}

}  // namespace

std::unique_ptr<SoundDriver> makeRoland(const DriverImage& image) {
    if (image.ident.rfind("RolandMT-32", 0) != 0 || image.data.size() < 0x2D9C)
        throw std::runtime_error("RSOUND.CA: not the Covert Action MT-32 driver");
    return std::make_unique<Roland>(image);
}

}  // namespace redux::audio
