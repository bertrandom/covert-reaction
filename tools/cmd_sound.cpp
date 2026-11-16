// Sound: the drivers' output (MIDI, OPL writes, speaker), rendered WAVs, MT-32 ROMs.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "Tool.h"
#include "audio/Audio.h"
#include "core/Folders.h"
#include "core/Settings.h"

using namespace reaction;
using namespace reaction::audio;

namespace {

Device deviceArg(const std::string& s) {
    auto d = parseDevice(s);
    if (!d && s != "auto") throw std::runtime_error("device: mt32, adlib, speaker or none");
    return d.value_or(Device::Mt32);
}

// "N" or "N,A,B"
struct Play {
    double at = 0;
    int n = 0, a = 0, b = 0;
};
Play parsePlay(const std::string& s) {
    Play p;
    std::string rest = s;
    if (auto c = rest.find('@'); c != std::string::npos) {   // "N,A,B@seconds"
        p.at = std::stod(rest.substr(c + 1));
        rest = rest.substr(0, c);
    }
    int v[3] = {0, 0, 0};
    std::size_t i = 0, k = 0;
    while (k < 3 && i <= rest.size()) {
        auto c = rest.find(',', i);
        v[k++] = std::stoi(rest.substr(i, c == std::string::npos ? std::string::npos : c - i));
        if (c == std::string::npos) break;
        i = c + 1;
    }
    p.n = v[0];
    p.a = v[1];
    p.b = v[2];
    return p;
}

class Printer final : public DeviceSink {
public:
    bool on = true;
    std::string line;
    void midi(std::uint8_t b) override { add(b >= 0x80 ? "|" : "", b); }
    void opl(std::uint8_t r, std::uint8_t v) override {
        if (!on) return;
        char buf[16];
        std::snprintf(buf, sizeof buf, " %02X=%02X", r, v);
        line += buf;
    }
    void speakerDivisor(std::uint16_t d) override {
        if (!on) return;
        char buf[32];
        std::snprintf(buf, sizeof buf, " div=%04X(%.0fHz)", d, kPitHz / (d ? d : 65536));
        line += buf;
    }
    void speakerGate(bool g) override {
        if (on) line += g ? " on" : " off";
    }

private:
    void add(const char* pre, std::uint8_t b) {
        if (!on) return;
        char buf[16];
        std::snprintf(buf, sizeof buf, " %s%02X", pre, b);
        line += buf;
    }
};

void writeWav(const std::string& path, const std::vector<std::int16_t>& s, int rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("can't write " + path);
    auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(s.size() * 2);
    f.write("RIFF", 4);
    u32(36 + bytes);
    f.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(2);
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate) * 4);
    u16(4);
    u16(16);
    f.write("data", 4);
    u32(bytes);
    f.write(reinterpret_cast<const char*>(s.data()), static_cast<std::streamsize>(bytes));
}

}  // namespace

REACTION_COMMAND("sound-events",
              "sound-events DEVICE N[,A,B] [MORE...] [--ticks T] [--init]: run a driver (mt32, adlib, "
              "speaker), play_sound(N, A, B), and print what it sends to the device on each timer tick "
              "(MIDI bytes, |status; OPL reg=value; speaker divisor/gate). N,A,B@T plays at tick T",
              [](const GameData& d, const std::vector<std::string>& args) {
                  if (args.size() < 2) throw std::runtime_error("usage: sound-events DEVICE N[,A,B] ...");
                  const Device dev = deviceArg(args[0]);
                  long ticks = 600;
                  bool showInit = false;
                  std::vector<Play> plays;
                  for (std::size_t i = 1; i < args.size(); ++i) {
                      if (args[i] == "--ticks" && i + 1 < args.size()) ticks = std::stol(args[++i]);
                      else if (args[i] == "--init") showInit = true;
                      else plays.push_back(parsePlay(args[i]));
                  }
                  auto drv = makeDriver(dev, DriverImage::parse(d.read(driverFile(dev))));
                  Printer p;
                  drv->setSink(&p);
                  p.on = showInit;
                  drv->init();
                  if (showInit) std::printf("init:%s\n", p.line.c_str());
                  p.on = true;
                  for (long t = 0; t < ticks; ++t) {
                      p.line.clear();
                      for (const Play& pl : plays)
                          if (static_cast<long>(pl.at) == t) drv->play(pl.n, pl.a, pl.b);
                      if (!p.line.empty()) std::printf("play %5ld:%s\n", t, p.line.c_str());
                      p.line.clear();
                      drv->tick();
                      if (!p.line.empty())
                          std::printf("tick %5ld [%04X]:%s\n", t, drv->timerDivisor(), p.line.c_str());
                  }
                  return 0;
              });

REACTION_COMMAND("sound-wav",
              "sound-wav DEVICE OUT.wav N[,A,B][@SECONDS] [MORE...] [--seconds S] [--rate HZ] "
              "[--mt32-roms DIR] [--keep-init] [--clean-mixing]: render play_sound calls on a device (auto, mt32, adlib, "
              "speaker) to a WAV file, and print its levels",
              [](const GameData& d, const std::vector<std::string>& args) {
                  if (args.size() < 3) throw std::runtime_error("usage: sound-wav DEVICE OUT.wav N ...");
                  double seconds = 30;
                  int rate = 48000;
                  bool keepInit = false, clean = false;
                  std::optional<std::filesystem::path> roms;
                  std::vector<Play> plays;
                  for (std::size_t i = 2; i < args.size(); ++i) {
                      if (args[i] == "--seconds" && i + 1 < args.size()) seconds = std::stod(args[++i]);
                      else if (args[i] == "--rate" && i + 1 < args.size()) rate = std::stoi(args[++i]);
                      else if (args[i] == "--mt32-roms" && i + 1 < args.size()) roms = args[++i];
                      else if (args[i] == "--keep-init") keepInit = true;
                      else if (args[i] == "--clean-mixing") clean = true;   // the Options setting
                      else plays.push_back(parsePlay(args[i]));
                  }
                  AudioEngine eng(d, mt32RomDirs(roms, d.dir()), rate);
                  eng.select(args[0]);
                  eng.setMt32CleanMixing(clean);
                  std::fprintf(stderr, "device: %s: %s\n", deviceName(eng.device()), eng.status().c_str());
                  std::vector<std::int16_t> warm(static_cast<std::size_t>(rate) * 2 * 3);
                  // COVERT starts the driver long before the first sound: skip the
                  // MT-32's reset and timbre upload (about 1.5 s of MIDI) unless asked.
                  if (!keepInit) eng.render(warm.data(), rate * 3);
                  std::sort(plays.begin(), plays.end(), [](const Play& x, const Play& y) { return x.at < y.at; });
                  const auto total = static_cast<std::size_t>(seconds * rate);
                  std::vector<std::int16_t> out(total * 2);
                  std::size_t pos = 0, next = 0;
                  const std::size_t block = 256;
                  while (pos < total) {
                      while (next < plays.size() && plays[next].at * rate <= double(pos)) {
                          eng.play(plays[next].n, plays[next].a, plays[next].b);
                          ++next;
                      }
                      const std::size_t n = std::min(block, total - pos);
                      eng.render(out.data() + pos * 2, static_cast<int>(n));
                      pos += n;
                  }
                  writeWav(args[1], out, rate);
                  // Levels, per second: peak and RMS (dBFS), and where the sound ends.
                  double peak = 0, sum = 0;
                  std::size_t last = 0;
                  for (std::size_t i = 0; i < out.size(); ++i) {
                      const double v = std::abs(out[i]) / 32768.0;
                      peak = std::max(peak, v);
                      sum += v * v;
                      if (std::abs(out[i]) > 64) last = i / 2;
                  }
                  auto db = [](double v) { return v > 0 ? 20 * std::log10(v) : -999.0; };
                  std::printf("%s: %.2f s, peak %.1f dBFS, RMS %.1f dBFS, last sound at %.2f s\n",
                              args[1].c_str(), seconds, db(peak), db(std::sqrt(sum / double(out.size()))),
                              double(last) / rate);
                  for (std::size_t s = 0; s * rate < total; ++s) {
                      double pk = 0, sm = 0;
                      const std::size_t a = s * rate * 2, b = std::min(out.size(), (s + 1) * rate * 2);
                      for (std::size_t i = a; i < b; ++i) {
                          const double v = std::abs(out[i]) / 32768.0;
                          pk = std::max(pk, v);
                          sm += v * v;
                      }
                      std::printf("  %3zu s: peak %6.1f  rms %6.1f\n", s, db(pk), db(std::sqrt(sm / double(b - a))));
                  }
                  return 0;
              });

REACTION_COMMAND("sound-roms",
              "sound-roms [DIR] [--settings FILE]: list the MT-32 / CM-32L ROMs found (DIR, "
              "COVERT_MT32_ROMS, the mt32_roms folder saved in FILE or in "
              "$COVERT_REACTION_USER_DIR/settings.ini, next to the game data) and the set that would be used",
              [](const GameData& d, const std::vector<std::string>& args) {
                  std::optional<std::filesystem::path> dir;
                  std::filesystem::path settings;
                  if (const char* user = std::getenv("COVERT_REACTION_USER_DIR"); user && *user)
                      settings = std::filesystem::path(user) / "settings.ini";
                  for (std::size_t i = 0; i < args.size(); ++i) {
                      if (args[i] == "--settings" && i + 1 < args.size()) settings = args[++i];
                      else dir = args[i];
                  }
                  std::filesystem::path saved;
                  if (!settings.empty()) {
                      const std::string s = Settings::load(settings).mt32Roms;
                      if (!s.empty()) saved = folders::fromUtf8(s);
                  }
                  if (!saved.empty()) std::printf("saved mt32_roms: %s\n", saved.string().c_str());
                  std::vector<std::string> log;
                  auto dirs = mt32RomDirs(dir, d.dir(), {}, saved);
                  auto roms = findMt32Roms(dirs, &log);
                  for (const auto& l : log) std::printf("found: %s\n", l.c_str());
                  if (!roms) {
                      std::printf("no usable control + PCM ROM pair; looked in:\n");
                      for (const auto& p : dirs) std::printf("  %s\n", p.string().c_str());
                      return 1;
                  }
                  std::printf("using: %s\n  control: %s\n  pcm: %s\n", roms->describe().c_str(),
                              roms->control.files[0].string().c_str(), roms->pcm.files[0].string().c_str());
                  return 0;
              });
