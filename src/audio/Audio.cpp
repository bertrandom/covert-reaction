#include "audio/Audio.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace fs = std::filesystem;

namespace redux::audio {

std::vector<fs::path> mt32RomDirs(const std::optional<fs::path>& explicitDir, const fs::path& gameDir,
                                  const std::vector<fs::path>& extra) {
    std::vector<fs::path> out;
    if (explicitDir) out.push_back(*explicitDir);
    if (const char* env = std::getenv("COVERT_MT32_ROMS"); env && *env) out.emplace_back(env);
    for (const fs::path& base : {gameDir, gameDir.parent_path()}) {
        if (base.empty()) continue;
        if (base == gameDir) out.push_back(base);
        for (const char* sub : {"MT32", "mt32", "MT32/roms", "mt32/roms", "MT32-ROMs", "mt32-roms", "ROMs", "roms"})
            out.push_back(base / sub);
    }
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
}

std::optional<Device> parseDevice(const std::string& s) {
    if (s == "mt32") return Device::Mt32;
    if (s == "adlib") return Device::Adlib;
    if (s == "speaker") return Device::Speaker;
    if (s == "none") return Device::None;
    return std::nullopt;
}

AudioEngine::AudioEngine(const GameData& data, std::vector<fs::path> romDirs, int sampleRate)
    : data_(data),
      romDirs_(std::move(romDirs)),
      rate_(sampleRate),
      clocksPerFrame_(kPitHz / sampleRate) {
    cur_.driver = makeDriver(Device::None, DriverImage{});
    cur_.synth = makeSilentSynth();
}

AudioEngine::~AudioEngine() = default;

AudioEngine::Unit AudioEngine::build(std::optional<Device> want, std::string& why) {
    // The fallback order: what was asked for, then the next device down.
    std::vector<Device> order;
    const Device first = want.value_or(Device::Mt32);
    for (Device d : {Device::Mt32, Device::Adlib, Device::Speaker, Device::None})
        if (d == first || !order.empty()) order.push_back(d);
    for (Device d : order) {
        Unit u;
        u.device = d;
        try {
            if (d == Device::None) {
                u.driver = makeDriver(d, DriverImage{});
                u.synth = makeSilentSynth();
                if (why.empty()) why = "no sound";
                return u;
            }
            auto file = data_.find(driverFile(d));
            if (!file) {
                why += std::string(driverFile(d)) + " is missing; ";
                continue;
            }
            const DriverImage img = DriverImage::parse(data_.read(driverFile(d)));
            if (d == Device::Mt32) {
                if (!romsSearched_) {
                    roms_ = findMt32Roms(romDirs_);
                    romsSearched_ = true;
                }
                if (!roms_) {
                    why += "no MT-32 / CM-32L ROMs found; ";
                    continue;
                }
                u.synth = makeMt32Synth(*roms_, rate_);
                why += std::string("MT-32 (") + roms_->describe() + ")";
            } else if (d == Device::Adlib) {
                u.synth = makeOplSynth(rate_);
                why += "AdLib (OPL2 emulation)";
            } else {
                u.synth = makeSpeakerSynth(rate_);
                why += "PC speaker";
            }
            u.driver = makeDriver(d, img);
            return u;
        } catch (const std::exception& e) {
            why += std::string(deviceName(d)) + ": " + e.what() + "; ";
        }
    }
    Unit none;
    none.driver = makeDriver(Device::None, DriverImage{});
    none.synth = makeSilentSynth();
    return none;
}

void AudioEngine::select(const std::string& setting) {
    {
        std::lock_guard lock(mu_);
        if (setting == setting_) return;
    }
    std::string why;
    Unit u = build(parseDevice(setting), why);
    std::lock_guard lock(mu_);
    setting_ = setting;
    selected_ = u.device;
    status_ = why;
    pending_ = std::move(u);
}

void AudioEngine::setSampleRate(int rate) {
    std::string s;
    {
        std::lock_guard lock(mu_);
        if (rate == rate_ || rate <= 0) return;
        rate_ = rate;
        clocksPerFrame_ = kPitHz / rate;
        s.swap(setting_);
    }
    if (!s.empty()) select(s);
}

void AudioEngine::play(int n, int a, int b) {
    std::lock_guard lock(mu_);
    if (queue_.size() < 256) queue_.push_back({n, a, b});
}

Device AudioEngine::device() const {
    std::lock_guard lock(mu_);
    return selected_;
}

std::string AudioEngine::status() const {
    std::lock_guard lock(mu_);
    return status_;
}

void AudioEngine::render(std::int16_t* out, int frames) {
    std::vector<Cmd> cmds;
    std::optional<Unit> next;
    {
        std::lock_guard lock(mu_);
        cmds.swap(queue_);
        next.swap(pending_);
    }
    if (next) {
        cur_ = std::move(*next);
        cur_.driver->setSink(cur_.synth.get());
        cur_.driver->init();   // COVERT's drv_sound100() at start-up
        untilTick_ = 0;
    }
    for (const Cmd& c : cmds) cur_.driver->play(c.n, c.a, c.b);
    while (frames > 0) {
        if (untilTick_ <= 0) {
            cur_.driver->tick();
            const std::uint16_t d = cur_.driver->timerDivisor();
            untilTick_ += d == 0 ? 65536.0 : d;
        }
        const int n = std::clamp(static_cast<int>(std::ceil(untilTick_ / clocksPerFrame_)), 1, frames);
        cur_.synth->render(out, n);
        out += 2 * n;
        frames -= n;
        untilTick_ -= n * clocksPerFrame_;
    }
}

}  // namespace redux::audio
