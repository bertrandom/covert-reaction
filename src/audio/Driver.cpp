#include "audio/Driver.h"

#include <algorithm>
#include <stdexcept>

namespace reaction::audio {

const char* deviceName(Device d) {
    switch (d) {
    case Device::Mt32: return "mt32";
    case Device::Adlib: return "adlib";
    case Device::Speaker: return "speaker";
    case Device::None: break;
    }
    return "none";
}

const char* driverFile(Device d) {
    switch (d) {
    case Device::Mt32: return "RSOUND.CA";
    case Device::Adlib: return "ASOUND.CA";
    case Device::Speaker: return "ISOUND.CA";
    case Device::None: break;
    }
    return "NSOUND.CA";
}

DriverImage DriverImage::parse(const std::vector<std::uint8_t>& f) {
    auto w = [&](std::size_t o) -> unsigned {
        if (o + 2 > f.size()) throw std::runtime_error("sound driver: truncated file");
        return f[o] | f[o + 1] << 8;
    };
    if (f.size() < 0x40 || f[0] != 'M' || f[1] != 'Z') throw std::runtime_error("sound driver: not an MZ file");
    // The load module follows the header; CODE is at its start, and the driver
    // header (read by COVERT's loader) at CODE:0010h: the 20-character name, then
    // at +18h the CODE and DATA paragraphs (relative to the load module).
    const std::size_t mod = std::size_t(w(8)) * 16;
    if (mod + 0x40 > f.size()) throw std::runtime_error("sound driver: truncated header");
    DriverImage img;
    img.ident.assign(reinterpret_cast<const char*>(&f[mod + 0x10]), 20);
    const std::size_t data = mod + std::size_t(w(mod + 0x2A)) * 16;
    if (data > f.size()) throw std::runtime_error("sound driver: bad data segment");
    img.data.assign(f.begin() + static_cast<std::ptrdiff_t>(data), f.end());
    if (img.data.size() > 0x10000) img.data.resize(0x10000);
    return img;
}

Mem::Mem(const DriverImage& img) { std::copy(img.data.begin(), img.data.end(), m_.begin()); }

namespace {
// NSOUND: init returns 0, every other slot returns at once.
class Silent final : public SoundDriver {
public:
    void init() override {}
    void play(int, int, int) override {}
    void tick() override {}
};
}  // namespace

std::unique_ptr<SoundDriver> makeDriver(Device device, const DriverImage& image) {
    switch (device) {
    case Device::Mt32: return makeRoland(image);
    case Device::Adlib: return makeAdlib(image);
    case Device::Speaker: return makeSpeaker(image);
    case Device::None: break;
    }
    return std::make_unique<Silent>();
}

}  // namespace reaction::audio
