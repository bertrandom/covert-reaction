// The original's random numbers: MSC 5.1's rand() (a 32-bit LCG returning 15 bits)
// and the MicroProse library's random(n) = rand() * n >> 15 (GFX.C). Seeded the same
// way, the reimplementation draws the same numbers, so a case generated from a
// seed matches the original's.
#pragma once

#include <cstdint>

namespace redux {

class Rng {
public:
    explicit Rng(std::uint32_t seed = 1) : state_(seed) {}

    void seed(std::uint32_t s) { state_ = s; }     // srand
    std::uint32_t state() const { return state_; }

    // rand(): 0..32767.
    int next() {
        state_ = state_ * 214013u + 2531011u;
        return static_cast<int>(state_ >> 16 & 0x7FFF);
    }
    // random(n): 0..n-1 (n > 0), as the game's random().
    int below(int n) { return static_cast<int>(static_cast<std::int32_t>(next()) * n >> 15); }

private:
    std::uint32_t state_;
};

// clamp(v, lo, hi) as the library's (lo wins if lo > hi).
constexpr int clampTo(int v, int lo, int hi) {
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

}  // namespace redux
