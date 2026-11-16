// dissolve_page (MGRAPHIC slot 9): the page copied to the screen pixel by pixel
// in the order of a 16-bit LFSR (polynomial B400h); calibrated to 215h pixels per
// retrace, so about 120 retraces for the screen. Used by the vacation snapshots
// (the Hall of Fame and the debriefing).
#pragma once

#include <utility>

#include "core/Image.h"

namespace reaction {

class Dissolve {
public:
    Dissolve() = default;
    explicit Dissolve(Image target) : target_(std::move(target)), left_(0xFA00) {}
    bool active() const { return left_ > 0; }
    void tick(Image& screen) {
        for (int n = 0; n < 0x215 && left_ > 0; ++n) {
            do {
                const bool carry = lfsr_ & 1;
                lfsr_ >>= 1;
                if (carry) lfsr_ ^= 0xB400;
            } while (lfsr_ > 0xFA00);
            const std::size_t i = lfsr_ - 1;
            screen.pixels()[i] = target_.pixels()[i];
            --left_;
        }
    }

private:
    Image target_;
    unsigned lfsr_ = 1;
    int left_ = 0;
};

}  // namespace reaction
