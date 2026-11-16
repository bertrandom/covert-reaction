#include "game/Career.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

namespace redux {

namespace {

struct BlockIo {
    std::array<std::uint8_t, Career::kBlockSize>* out = nullptr;
    const std::array<std::uint8_t, Career::kBlockSize>* in = nullptr;
    std::size_t pos = 0;

    template <typename T>
    void word(T& v) {
        if (out) {
            const auto u = static_cast<std::uint16_t>(v);
            (*out)[pos] = static_cast<std::uint8_t>(u & 0xFF);
            (*out)[pos + 1] = static_cast<std::uint8_t>(u >> 8);
        } else {
            v = static_cast<T>((*in)[pos] | (*in)[pos + 1] << 8);
        }
        pos += 2;
    }
    void bytes(char* p, std::size_t n) {
        if (out) std::memcpy(out->data() + pos, p, n);
        else std::memcpy(p, in->data() + pos, n);
        pos += n;
    }
    void bytes(std::uint8_t* p, std::size_t n) { bytes(reinterpret_cast<char*>(p), n); }
};

template <typename C>
void fields(BlockIo& io, C& c) {
    io.word(c.plotNum);
    io.word(c.plotEpisodes);
    io.word(c.mm.known);
    io.word(c.mm.org);
    io.word(c.mm.city);
    io.word(c.mm.id);
    io.word(c.leaderCode);
    io.word(c.lastEpisode);
    io.word(c.episodeCrime);
    io.word(c.worldNum);
    io.word(c.careerPoints);
    io.word(c.curMonth);
    io.word(c.envVar30Copy);
    io.bytes(c.curPersonName.raw.data(), c.curPersonName.raw.size());
    io.word(c.careerRank);
    io.word(c.f28);
    io.bytes(c.playerName.raw.data(), c.playerName.raw.size());
    io.bytes(c.playerOrgName.raw.data(), c.playerOrgName.raw.size());
    io.bytes(c.codename.raw.data(), c.codename.raw.size());
    io.bytes(c.history.data(), c.history.size());
    std::uint16_t lo = static_cast<std::uint16_t>(c.mastermindsArrested & 0xFFFF);
    std::uint16_t hi = static_cast<std::uint16_t>(c.mastermindsArrested >> 16);
    io.word(lo);
    io.word(hi);
    c.mastermindsArrested = static_cast<std::uint32_t>(hi) << 16 | lo;
}

}  // namespace

Career Career::unpack(const std::array<std::uint8_t, kBlockSize>& block) {
    Career c;
    BlockIo io;
    io.in = &block;
    fields(io, c);
    return c;
}

void Career::pack(std::array<std::uint8_t, kBlockSize>& block) const {
    Career c = *this;
    BlockIo io;
    io.out = &block;
    fields(io, c);
}

GameState::GameState() {
    auto seed = static_cast<std::uint32_t>(std::time(nullptr) & 0x7FFF);
    if (const char* s = std::getenv("COVERT_SEED")) seed = static_cast<std::uint32_t>(std::strtoul(s, nullptr, 0));
    rng.seed(seed);
    world.hdr.randSeed = static_cast<std::int16_t>(seed);
    // FINAL main on a first start (start_mode 1): options |= 0Ch, sound and
    // animations on.
    world.hdr.options = static_cast<std::int16_t>(world.hdr.options | 0xC);
    if (const char* s = std::getenv("COVERT_COPY_PROTECTION")) copyProtection = std::atoi(s) != 0;
}

}  // namespace redux
