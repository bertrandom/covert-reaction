#include "game/Chase.h"

#include <cstdlib>

namespace redux {

namespace {

// speed_masks: of every 6 frames, those a car moves on at each speed.
constexpr std::array<unsigned, 5> kSpeedMasks = {0, 1, 9, 21, 27};

int sign(int n) { return n > 0 ? 1 : n < 0 ? -1 : 0; }

}  // namespace

std::array<CarOffer, 4> rollCarOffers(Rng& rng, int difficulty) {
    std::array<CarOffer, 4> out{};
    for (int i = 0; i < 4; ++i) {
        int c = 0, total = 0;
        do {
            c = rng.below(64);
            total = c & 3;
            if (c & 4) total += 2;
            if (c & 8) total++;
            total -= (c & 0x30) >> 4;
            for (int j = 0; j < i; ++j)
                if (out[static_cast<std::size_t>(j)].bits == c) total = -1;
        } while (total != 3 - difficulty || (c & 3) == 3);
        CarOffer& o = out[static_cast<std::size_t>(i)];
        o.bits = c;
        o.sprite = 2;
        if (!(c & 0x30)) o.sprite = 3;
        if ((c & 8) && (c & 3)) o.sprite = 1;
        if ((c & 3) == 2) o.sprite = 0;
        o.paint = (c & 0x30) >> 4;
    }
    return out;
}

Chase::Chase(const ChaseSetup& setup) : setup_(setup), rng_(setup.seed) {
    switch (setup.mode) {             // ENV[0] -> chase_mode (main)
    case 3: case 14: mode_ = 0; break;
    case 5: mode_ = 1; break;
    case 6: mode_ = 2; break;
    default: mode_ = setup.mode >= 0 && setup.mode <= 2 ? setup.mode : 0; break;
    }
    gameSpeed_ = (setup.envFlags >> 10) & 3;
    difficulty_ = clampTo(setup.difficulty - ((setup.envFlags >> 2) & 3) + 1, 0, 3);
    const int hour = setup.gameTime / 60 % 24;
    night_ = hour < 8 || hour >= 20;
    options_ = setup.options;
    targetBuilding_ = setup.targetBuilding;
    cars_[1].bits = cars_[2].bits = 0;
}

std::string Chase::labelText(int n) const {
    // real_mission is always set: the organization's abbreviation (the "Loc<n>"
    // labels of the other branch are never shown).
    return setup_.orgAbbrev[static_cast<std::size_t>(n)];
}

void Chase::generateMap() {
    rng_.seed(static_cast<std::uint16_t>(setup_.cityId * 123 + (setup_.mapSeed & 0x3FFF)));
    for (int i = 0; i < kBlocks; ++i)
        for (int j = 0; j < kBlocks; ++j)
            map_[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] =
                static_cast<std::uint8_t>(rng_.below(16) * 16 + 15);
    auto at = [this](int x, int y) -> std::uint8_t& {
        return map_[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)];
    };
    int nk = 0;
    do {
        const int i = rng_.below(21) + 2;
        const int j = rng_.below(21) + 2;
        const int side = nk & 3;
        if (nk & 2) {                 // a wall needs two untouched blocks
            if ((at(i, j) & 15) != 15) continue;
            if ((at(i + kDx[side], j + kDy[side]) & 15) != 15) continue;
        }
        at(i, j) &= static_cast<std::uint8_t>(255 - (1 << side));
        at(i, j) = static_cast<std::uint8_t>((((i + j) & 1) << 4) + (at(i, j) & 15) + 128);
        at(i + kDx[side], j + kDy[side]) &= static_cast<std::uint8_t>(255 - (1 << ((nk + 2) & 3)));
        walls_.push_back({i, j, side});
    } while (++nk < 192);
    for (nk = 0; nk < 16; ++nk) {
        int lx = 0, by = 0;
        bool ok = false;
        do {
            do {
                if (difficulty_ < 2) {
                    lx = rng_.below(12) * 32 + 192;
                    by = rng_.below(12) * 32 + 208;
                } else {
                    lx = rng_.below(18) * 32 + 96;
                    by = rng_.below(18) * 32 + 112;
                }
            } while (!roadOpen(lx / 32, by / 32, 2));
            ok = true;
            for (int m = 1; m < nk; ++m) {
                const int dist = std::abs(lx - bldX_[static_cast<std::size_t>(m)]) +
                                 std::abs(by - bldY_[static_cast<std::size_t>(m)]);
                if (dist < difficulty_ * 32 + 64) {
                    ok = false;
                    break;
                }
            }
        } while (!ok);
        bldX_[static_cast<std::size_t>(nk)] = lx;
        bldY_[static_cast<std::size_t>(nk)] = by;
        if (nk != 0 && (setup_.orgMask & (1 << nk))) labels_.push_back({nk, lx, by});
    }
    chaseStart_ = rng_.below(15) + 1;
    do chaseDest_ = rng_.below(15) + 1;
    while (chaseStart_ == chaseDest_);
    // real_mission (always set in main)
    chaseStart_ = static_cast<signed char>(setup_.startBuilding);
    if (options_ & 0x20) {
        do chaseStart_ = rng_.below(16);
        while (chaseStart_ == targetBuilding_);
    }
    chaseDest_ = targetBuilding_;
}

bool Chase::roadOpen(int x, int y, int dir) const {
    if (x == 0 && dir != 1) return false;
    if (y == 0 && dir != 2) return false;
    if (x == 23 && dir != 3) return false;
    if (y == 23 && dir != 0) return false;
    // The original reads outside city_map for corners off the grid; nothing
    // there is a road here.
    auto m = [this](int bx, int by) -> int {
        if (bx < 0 || by < 0 || bx >= kBlocks || by >= kBlocks) return 0;
        return map(bx, by);
    };
    switch (dir) {
    case 0: return m(x, y - 1) & 8;
    case 1: return m(x, y) & 1;
    case 2: return m(x, y) & 8;
    case 3: return m(x - 1, y) & 1;
    default: return false;
    }
}

void Chase::initCars() {
    heatLimit_ = 1000 / (difficulty_ + 3);
    heatLimit_ = heatLimit_ * 10 / (setup_.alertLevel + 100) * 10;
    if (heatLimit_ == 0) heatLimit_ = 1;       // the original divides by it (alert > 900 only)
    outcome_ = 0;
    ChaseCar& s = cars_[0];
    const int start = chaseStart_ & 15, dest = chaseDest_ & 15;
    s.x = bldX_[static_cast<std::size_t>(start)];
    s.y = bldY_[static_cast<std::size_t>(start)];
    s.dir = bldY_[static_cast<std::size_t>(dest)] < s.y ? 0 : 2;
    s.tx = s.x;
    s.ty = s.y;
    s.req = -1;
    s.heat = 0;
    s.bits = 0;
    s.speed = 2;
    for (int i = 1; i < 3; ++i) {
        ChaseCar& c = cars_[static_cast<std::size_t>(i)];
        c.x = s.x - (kDx[static_cast<std::size_t>(s.dir)] << 5) / 4 * i;
        c.y = s.y - (kDy[static_cast<std::size_t>(s.dir)] << 5) / 4 * i;
        c.dir = s.dir;
        c.follow = 1;
        c.tx = -1;
        c.req = -1;
        c.heat = 0;
        c.speed = 0;
    }
    targetX_ = bldX_[static_cast<std::size_t>(dest)];
    targetY_ = bldY_[static_cast<std::size_t>(dest)];
    if (mode_ == 2) curCar_ = 0;
}

bool Chase::loopStart() {
    dots_.clear();
    turns_.clear();
    if (mode_ != 2 && clock_ == 32) {
        cars_[1].speed = cars_[2].speed = 2;
        playSound(0x16);
        soundParam_ = cars_[static_cast<std::size_t>(curCar_)].speed;
        playSound(0x1B);
        playSound(0x1A);
        return true;
    }
    return false;
}

Chase::KeyAction Chase::key(int k) {
    ChaseCar& c = cars_[static_cast<std::size_t>(curCar_)];
    switch (k) {
    case 'f': gameSpeed_ = 2; break;
    case 'd': gameSpeed_ = 1; break;
    case 's': gameSpeed_ = 0; break;
    case 0x4400: c.follow = 1; break;                    // F10: follow
    case 0x4800: c.req = 0; c.follow = 0; break;
    case 0x4D00: c.req = 1; c.follow = 0; break;
    case 0x5000: c.req = 2; c.follow = 0; break;
    case 0x4B00: c.req = 3; c.follow = 0; break;
    case '=':
        if (c.speed == 0) playSound(0x16);
        else if (static_cast<unsigned>((c.bits + 2) & 3) > static_cast<unsigned>(c.speed)) playSound(0x18);
        c.speed = clampTo(c.speed + 1, 0, (c.bits & 3) + 2);
        if (mode_ == 2 && c.speed > 2) c.speed = 2;
        soundParam_ = c.speed;
        playSound(curCar_ == 1 ? 0x1A : 0x1B);
        break;
    case '-':
        if (c.speed) playSound(0x19);
        c.speed = clampTo(c.speed - 1, 0, 3);
        soundParam_ = c.speed;
        playSound(curCar_ == 1 ? 0x1A : 0x1B);
        break;
    case ' ':
        if (mode_ != 2) {
            if (++curCar_ >= 3) curCar_ = 1;
            scene_ = (scene_ + 4) & 7;
        }
        soundParam_ = cars_[static_cast<std::size_t>(curCar_)].speed;
        playSound(curCar_ == 1 ? 0x1A : 0x1B);
        break;
    case 'a':
    case 0x3B00:                                         // F1: arrest
        if (inRange_) {
            outcome_ = 2;
            over_ = 1;
            return KeyAction::Return;
        }
        [[fallthrough]];
    case 'e':
    case 0x3C00:                                         // F2: into a building
        if (checkBuildingReached(cars_[0].x, cars_[0].y)) {
            over_ = 1;
            outcome_ = 0;
        }
        break;
    case 0x1000: return KeyAction::QuitPrompt;           // Alt-Q
    case 0x1F00:                                         // Alt-S: sound
        if (options_ & 4) playSound(0);
        options_ ^= 4;
        break;
    case 0x1900: return KeyAction::Pause;                // Alt-P
    case 0x1B:
        if (mode_ != 2) {
            over_ = 1;
            return KeyAction::Return;
        }
        break;
    default: break;
    }
    return KeyAction::None;
}

void Chase::afterKey() {
    ChaseCar& c = cars_[static_cast<std::size_t>(curCar_)];
    if (((c.dir + 2) & 3) == c.req) {                    // a U-turn: only with good handling
        if (c.bits & 8) {
            playSound(0x17);
            c.dir = c.req;
        }
        c.req = -1;
    }
}

void Chase::requestTurn(int dir, int newDir) {
    if (((dir - 1) & 3) == newDir || ((dir + 1) & 3) == newDir) turns_.push_back(Turn{dir, newDir, scene_});
}

int Chase::carInView(int n) {
    const ChaseCar& s = cars_[0];
    const ChaseCar& c = cars_[static_cast<std::size_t>(n)];
    const int rx = s.x - c.x, dy = s.y - c.y;
    const int distx = std::abs(rx), ady = std::abs(dy);
    if (night_ && (distx > 64 || ady > 64)) return 0;
    bool open = true;
    int bkx = (c.x + (c.dir == 3 ? 31 : 0)) / 32;
    int row = (c.y + (c.dir == 0 ? 31 : 0)) / 32;
    if (distx == 0 && ady < 192 && c.x % 32 == 0) {
        const int dir = dy > 0 ? 2 : 0;
        for (int ij = 0; ij <= ady / 32; ++ij) {
            if (!roadOpen(bkx, row, dir)) {
                open = false;
                break;
            }
            row += sign(dy);
        }
        carAhead_ = dir == 0 ? dy < 0 : dy > 0;
        return open ? ady : 0;
    }
    if (ady != 0 || distx >= 192 || c.y % 32 != 0) return 0;
    const int dir = rx > 0 ? 1 : 3;
    for (int ij = 0; ij <= distx / 32; ++ij) {
        if (!roadOpen(bkx, row, dir)) {
            open = false;
            break;
        }
        bkx += sign(rx);
    }
    carAhead_ = dir == 3 ? rx < 0 : rx > 0;
    return open ? distx : 0;
}

void Chase::moveCars() {
    inRange_ = false;
    ChaseCar& s = cars_[0];
    for (int i = 0; i < 3; ++i) {
        ChaseCar& c = cars_[static_cast<std::size_t>(i)];
        const bool leader = mode_ != 2 ? i == 0 : i != 0;
        if (!leader) dot(c.x, c.y, 5);
        else dot(c.tx, c.ty, 5);
        bool moved = false;
        if (kSpeedMasks[static_cast<std::size_t>(c.speed)] & (1u << (clock_ % 6))) {
            c.x += kDx[static_cast<std::size_t>(c.dir)];
            c.y += kDy[static_cast<std::size_t>(c.dir)];
            moved = true;
            if (!leader && c.x == c.tx && c.y == c.ty) c.tx = -1;
            if (i == curCar_) scene_ = (scene_ + 1) & 7;
        }
        if (!leader) {
            if (i != curCar_ || (clock_ & 2)) dot(c.x, c.y, 10);
        } else {
            score_ = c.heat / heatLimit_;
            if (c.follow || showTargets_) {
                c.tx = c.x;
                c.ty = c.y;
                c.req = c.dir;
            }
            if (mode_ != 2 || c.follow) dot(c.tx, c.ty, c.follow ? 13 : 7);
            c.follow = 0;
            if (escapeTimer_ && --escapeTimer_ == 0) over_ = 1;
            if (mode_ != 2 && c.x == targetX_ && c.y == targetY_ && !escapeTimer_) {
                c.speed = 0;
                escapeTimer_ = -(difficulty_ * 64 - 256);
            }
        }
        int inview = carInView(i);
        if (i != 0 && inview != 0) {
            if (mode_ != 2) {
                if (c.speed) {
                    c.heat++;
                    if (inview <= 32) c.heat++;
                    if (c.speed > 2) c.heat++;
                    c.heat += ((c.bits & 0x30) >> 4) - 1;
                }
                s.follow = 1;
                if (c.heat > heatLimit_ * 2) {
                    s.heat++;
                    if (heatLimit_ / 2 < s.heat && !escapeTimer_) s.speed = 3;
                    if (s.heat > heatLimit_ && !escapeTimer_) s.speed = 4;
                }
                if (escapeTimer_ && mode_ != 2) {
                    outcome_ = 1;
                    if (++lostCount_ > 16) over_ = 1;
                }
            }
            if (mode_ != 2 && !escapeTimer_ && s.speed < c.speed) s.speed = c.speed;
            if (((s.dir + 2) & 3) == c.dir) {
                if (std::abs(c.x - s.x) + std::abs(c.y - s.y) <= 8) {
                    inRange_ = true;
                    if (mode_ == 2) outcome_ = over_ = 1;
                }
            }
            if (mode_ == 2) {
                c.tx = c.x;
                c.ty = c.y;
                c.follow = 1;
                c.speed = clampTo((clock_ >> 3) - i, 0, inview < 32 ? 2 : 3);
            }
        } else {
            if (i != 0 && c.heat > 0) c.heat--;
            if (i != 0 && mode_ == 2) {
                c.speed = 3;
                if (difficulty_ == 0) c.speed = (clock_ & (16 << i)) ? 2 : 3;
                if (difficulty_ == 3) c.speed = (clock_ & (16 << i)) ? 3 : 4;
                inview = std::abs(s.x - c.x) + std::abs(s.y - c.y);
                if ((5 - difficulty_) * 32 / 2 >= inview) {
                    c.tx = c.x;
                    c.ty = c.y;
                    c.follow = 1;
                }
            }
        }
        if (!(moved && c.x % 32 == 0 && c.y % 32 == 0)) continue;
        // At a corner: pick the way on.
        const int cx = c.x / 32, cy = c.y / 32;
        auto back = [&c] { return (c.dir + 2) & 3; };
        int newdir = 0, attempts = 0;
        if (i == 0) {
            if (mode_ == 2) {             // the player, chased
                if (c.req != -1 && roadOpen(cx, cy, c.req)) {
                    if (i == curCar_ && c.dir != c.req) requestTurn(c.dir, c.req);
                    c.dir = c.req;
                    c.req = -1;
                }
                if (!roadOpen(cx, cy, c.dir)) {
                    attempts = 0;
                    do {
                        do newdir = rng_.below(4);
                        while (!roadOpen(cx, cy, newdir));
                    } while (back() == newdir && ++attempts < 9);
                    if (i == curCar_) requestTurn(c.dir, newdir);
                    c.dir = newdir;
                }
            } else {                      // the suspect heads for the target
                if (!escapeTimer_) s.speed = 2;
                const int dx = targetX_ - c.x, dy = targetY_ - c.y;
                if ((rng_.below(2) || dy == 0) && dx != 0) {
                    newdir = dx > 0 ? 1 : 3;
                    if (!roadOpen(cx, cy, newdir)) newdir = dy > 0 ? 2 : 0;
                } else {
                    newdir = dy > 0 ? 2 : 0;
                    if (!roadOpen(cx, cy, newdir)) newdir = dx > 0 ? 1 : 3;
                }
                if (c.heat > rng_.below(heatLimit_ * 4)) newdir = rng_.below(4);
                attempts = 0;
                while (!roadOpen(cx, cy, newdir) || (back() == newdir && ++attempts < 9))
                    newdir = rng_.below(4);
                c.dir = newdir;
                for (int k = 1; k < 3; ++k) {
                    if (carInView(k)) {
                        cars_[1].tx = kDx[static_cast<std::size_t>(s.dir)] * 2 + s.x;
                        cars_[1].ty = kDy[static_cast<std::size_t>(s.dir)] * 2 + s.y;
                    }
                }
            }
            continue;
        }
        if (mode_ == 2) {                 // the pursuers close in
            const int dx = s.x - c.x, dy = s.y - c.y;
            int ty = 0, lateral = 0;
            switch (s.dir) {
            case 0: ty = -dy; lateral = dx; break;
            case 2: ty = dy; lateral = -dx; break;
            case 1: ty = dx; lateral = dy; break;
            case 3: ty = -dx; lateral = -dy; break;
            default: break;
            }
            if (lateral == 0) {
                newdir = ((turnToggle_++ & 1) * 2 + s.dir - 1) & 3;
                if (back() == newdir || !roadOpen(cx, cy, newdir)) newdir = s.dir;
                if (!roadOpen(kDx[static_cast<std::size_t>(newdir)] + cx, kDy[static_cast<std::size_t>(newdir)] + cy, s.dir))
                    newdir = s.dir;
                if (ty < 0) newdir = (s.dir + 2) & 3;
                if (ty > 32) newdir = s.dir;
            } else if (-std::abs(rng_.below(lateral)) < ty) {
                newdir = s.dir;
                if (back() == newdir || !roadOpen(cx, cy, newdir))
                    newdir = (lateral > 0 ? s.dir + 1 : s.dir - 1) & 3;
            } else {
                newdir = (lateral > 0 ? s.dir + 1 : s.dir - 1) & 3;
                if (back() == newdir || !roadOpen(cx, cy, newdir)) newdir = s.dir;
            }
        } else {                          // Max's team
            newdir = c.dir;
            if (c.req != -1) {
                if (roadOpen(cx, cy, c.req)) {
                    newdir = c.req;
                    c.req = -1;
                }
            } else if (c.follow == 1) {
                int dx = cars_[1].tx - c.x, dy = cars_[1].ty - c.y;
                if (c.bits & 4) {         // tracking: heads for the suspect himself
                    if (sign(dx) != sign(s.x - c.x) || sign(dy) != sign(s.y - c.y)) {
                        dx = sign(s.x - c.x) * 4;
                        dy = sign(s.y - c.y) * 4 - rng_.below(2) * 2 + 1;
                    }
                }
                if (dx || dy) {
                    newdir = std::abs(dx) > std::abs(dy) ? (dx > 0 ? 1 : 3) : (dy > 0 ? 2 : 0);
                    if (back() == newdir || !roadOpen(cx, cy, newdir))
                        newdir = std::abs(dx) <= std::abs(dy) ? (dx > 0 ? 1 : 3) : (dy > 0 ? 2 : 0);
                    if (back() == newdir) newdir = c.dir;
                }
            }
            if (carInView(i)) {
                cars_[1].tx = kDx[static_cast<std::size_t>(s.dir)] * 2 + s.x;
                cars_[1].ty = kDy[static_cast<std::size_t>(s.dir)] * 2 + s.y;
            }
        }
        if (roadOpen(cx, cy, newdir) && back() != newdir) {
            if (i == curCar_) requestTurn(c.dir, newdir);
        } else {
            attempts = 0;
            do {
                do newdir = rng_.below(4);
                while (!roadOpen(cx, cy, newdir));
            } while (back() == newdir && ++attempts < 9);
        }
        c.dir = newdir;
    }
    if (mode_ != 2 && s.follow == 0 && heatLimit_ * 2 < s.heat && !escapeTimer_) over_ = 2;
}

bool Chase::checkBuildingReached(int x, int y) {
    // The original's `dist` is only computed for the city's known buildings and
    // keeps its last value (first: stack garbage) for the others, so an unknown
    // building is never "reached" unless the garbage is <= 6; here it never is.
    for (int n = 1; n < 16; ++n) {
        if (!(setup_.orgMask & (1 << n))) continue;
        const int dist = std::abs(x - bldX_[static_cast<std::size_t>(n)]) +
                         std::abs(y - bldY_[static_cast<std::size_t>(n)]);
        if (dist <= 6) {
            targetBuilding_ = buildingReached_ = n;      // real_mission: ENV[0Eh] = n
            return true;
        }
    }
    return false;
}

std::string Chase::clockText() const {
    const int t = clock_;
    const int clk = (t / 60 + static_cast<int>(setup_.gameTime)) % 1440;
    const int hr = clk / 60, min = clk % 60, ss = t % 60;
    std::string s = "0:00:00";
    s[0] = static_cast<char>(hr % 10 + '0');
    s[2] = static_cast<char>(min / 10 + '0');
    s[3] = static_cast<char>(min % 10 + '0');
    s[5] = static_cast<char>(ss / 10 + '0');
    s[6] = static_cast<char>(ss % 10 + '0');
    return s;
}

ChaseResult Chase::result(bool quit) const {
    ChaseResult r;
    const bool he = setup_.sex & 1;
    std::string& m = r.message;
    if (outcome_ == 0) {
        if (mode_ == 0) {
            if (over_ != 2)
                m = "'We've totally lost\ntrack of this guy.\nLet's go back to\nwhere we started.'\n";
            else
                m = std::string(he ? "He" : "She") + " turns the corner\nand dashes into a\ncrowded restaurant.\n"
                    "'We'll have to\nbe less obvious\nnext time, Max.'\n";
        }
        if (mode_ == 2) m = "You dash into the\nbuilding seconds ahead\nof your pursuers!\n";
    }
    if (outcome_ == 1) {
        if (mode_ == 0)
            m = std::string(he ? "He" : "She") + " pulls over\nand walks into\na building as you\nquietly park nearby.\n";
        if (mode_ == 2) m = "Your pursuers run\nyou off the road!\n";
    }
    if (outcome_ == 2)
        m = std::string("You run ") + (he ? "his" : "her") + " car\noff the road and\nmake the arrest!\n";
    r.outcome = outcome_;
    r.score = clampTo(score_ * 25, 0, 100);
    r.nextProgram = quit ? 0xFF : (options_ & 0x20) ? 0x11 : 0;
    r.minutes = clock_ / 60;
    r.envFlags = static_cast<std::uint16_t>((setup_.envFlags & 0xF7FF) | (gameSpeed_ << 10));
    r.buildingReached = buildingReached_;
    r.options = options_;
    r.chaseOver = over_;
    r.quitGame = quit;
    return r;
}

}  // namespace redux
