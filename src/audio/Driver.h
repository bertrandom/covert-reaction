// The original's sound drivers (RSOUND.CA, ASOUND.CA, ISOUND.CA, NSOUND.CA), ported.
//
// COVERT loads one driver at start-up (the player picks the device in INTRO's
// menu) and keeps it for the whole session. Every program calls the same two
// entry points (covert-decomp src/ASM/DRVLOAD.ASM, slots 100-110):
//
//   slot 100 init()                  COVERT, once, at start-up
//   slot 101 play_sound(n, a, b)     effect or music n, when options & 4 (sound on)
//   slot 102 shutdown()              COVERT, at exit
//
// The drivers hook the timer interrupt (int 8) and run their sequencer from it:
// the PIT is reprogrammed to divisor 4C90h (1193182 / 19600 = 60.88 Hz); the
// AdLib and PC speaker drivers switch to divisor 0F88h (300.1 Hz) while noise
// and warble effects play. tick() here is one such interrupt; timerDivisor()
// says when the next one is due, in PIT clocks.
//
// The music and effect data live inside the drivers, in their DATA segments: the
// ports read them from the player's own .CA files at run time (DriverImage) and
// run on a 64 KiB copy of that segment, addressed by the same 16-bit offsets as
// the original (the sequencers patch their own data, e.g. RSOUND's random notes).
//
// The output is what the driver sends to its device (DeviceSink): MIDI bytes for
// the MT-32 (MPU-401 data port), register writes for the AdLib (ports 388h/389h),
// PIT channel 2 divisors and the speaker gate (ports 42h/61h) for the PC speaker.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace reaction::audio {

// The PIT's input clock (Hz): timer divisors are in these units.
constexpr double kPitHz = 1193182.0;
constexpr std::uint16_t kSlowTimer = 0x4C90;   // 60.88 Hz
constexpr std::uint16_t kFastTimer = 0x0F88;   // 300.1 Hz

enum class Device { None, Speaker, Adlib, Mt32 };
const char* deviceName(Device d);
// The driver file for a device ("RSOUND.CA", ...).
const char* driverFile(Device d);

// What a driver sends to its device.
class DeviceSink {
public:
    virtual ~DeviceSink() = default;
    virtual void midi(std::uint8_t /*byte*/) {}                        // MPU-401 data port (UART)
    virtual void opl(std::uint8_t /*reg*/, std::uint8_t /*value*/) {}  // AdLib register write
    virtual void speakerDivisor(std::uint16_t /*div*/) {}              // PIT channel 2 (0 = 65536)
    virtual void speakerGate(bool /*on*/) {}                           // port 61h bits 0-1
};

// A driver's DATA segment as loaded: the bytes of the .CA file (an MZ executable)
// from its data paragraph on, zeros after (BSS).
struct DriverImage {
    std::string ident;                    // the header's name and date, e.g. "RolandMT-32g15-10-90"
    std::vector<std::uint8_t> data;       // DATA segment bytes from the file
    static DriverImage parse(const std::vector<std::uint8_t>& file);   // throws std::runtime_error
};

// 64 KiB of DS-relative memory: the ports address it like the original does.
class Mem {
public:
    explicit Mem(const DriverImage& img);
    std::uint8_t& b(std::uint16_t a) { return m_[a]; }
    std::uint8_t b(std::uint16_t a) const { return m_[a]; }
    std::uint16_t w(std::uint16_t a) const {
        return static_cast<std::uint16_t>(m_[a] | m_[static_cast<std::uint16_t>(a + 1)] << 8);
    }
    void setW(std::uint16_t a, std::uint16_t v) {
        m_[a] = static_cast<std::uint8_t>(v);
        m_[static_cast<std::uint16_t>(a + 1)] = static_cast<std::uint8_t>(v >> 8);
    }

private:
    std::array<std::uint8_t, 0x10000> m_{};
};

class SoundDriver {
public:
    virtual ~SoundDriver() = default;
    virtual void init() = 0;                       // slot 100
    virtual void play(int n, int a, int b) = 0;    // slot 101: play_sound(n, a, b)
    virtual void shutdown() {}                     // slot 102
    virtual void tick() = 0;                       // one timer interrupt
    virtual std::uint16_t timerDivisor() const { return kSlowTimer; }
    void setSink(DeviceSink* s) { sink_ = s; }

protected:
    DeviceSink* sink_ = nullptr;
};

// The driver for `device`, on the image of its .CA file. Device::None gives
// NSOUND (does nothing). Throws std::runtime_error on a malformed image.
std::unique_ptr<SoundDriver> makeDriver(Device device, const DriverImage& image);

std::unique_ptr<SoundDriver> makeRoland(const DriverImage& image);    // RSOUND
std::unique_ptr<SoundDriver> makeAdlib(const DriverImage& image);     // ASOUND
std::unique_ptr<SoundDriver> makeSpeaker(const DriverImage& image);   // ISOUND

// 16-bit helpers shared by the ports.
inline std::uint16_t ror3(std::uint16_t v) { return static_cast<std::uint16_t>(v >> 3 | v << 13); }
inline std::int16_t sx(std::uint8_t v) { return static_cast<std::int8_t>(v); }   // cbw

}  // namespace reaction::audio
