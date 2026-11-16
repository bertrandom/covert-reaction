// The numeric keypad for keyboards without one (laptops, compact keyboards).
//
// TAC and BUG move in eight directions, and the original's diagonals come from
// keypad 7 9 1 3 (or Home PgUp End PgDn), which most laptops lack. Two ways in:
//
//   - Arrow chords: holding two perpendicular arrows gives the diagonal between
//     them (Up+Right = keypad 9, Up+Left = 7, Down+Right = 3, Down+Left = 1).
//     Releasing one goes back to the other arrow. Modifiers (Shift to dive in
//     TAC) pass through.
//   - The number row as the keypad: 1-9 are the keypad digits (5 = keypad 5,
//     sneak in TAC), via digitAsKeypad().
//   - A keypad on the letters, as laptops with an embedded keypad have it:
//     7 8 9 / U I O / J K L (letterKeypad()).
//
// ArrowChords is fed every key down/up of a scene and returns the key the scene
// should act on, as if it came from the keypad.
#pragma once

#include <SDL.h>

#include <optional>

namespace redux {

inline bool isArrowKey(SDL_Keycode k) {
    return k == SDLK_UP || k == SDLK_DOWN || k == SDLK_LEFT || k == SDLK_RIGHT;
}

// SDLK_1..SDLK_9 -> SDLK_KP_1..SDLK_KP_9; other keys unchanged.
inline SDL_Keycode digitAsKeypad(SDL_Keycode k) {
    switch (k) {
    case SDLK_1: return SDLK_KP_1;
    case SDLK_2: return SDLK_KP_2;
    case SDLK_3: return SDLK_KP_3;
    case SDLK_4: return SDLK_KP_4;
    case SDLK_5: return SDLK_KP_5;
    case SDLK_6: return SDLK_KP_6;
    case SDLK_7: return SDLK_KP_7;
    case SDLK_8: return SDLK_KP_8;
    case SDLK_9: return SDLK_KP_9;
    default: return k;
    }
}

// The keypad laid over the letters, as on laptops with an embedded keypad:
//     7 8 9          keypad 7 8 9
//     U I O    as    keypad 4 5 6
//     J K L          keypad 1 2 3
// Other keys unchanged. Only for screens where these letters mean nothing else.
inline SDL_Keycode letterKeypad(SDL_Keycode k) {
    switch (k) {
    case SDLK_7: return SDLK_KP_7;
    case SDLK_8: return SDLK_KP_8;
    case SDLK_9: return SDLK_KP_9;
    case SDLK_u: return SDLK_KP_4;
    case SDLK_i: return SDLK_KP_5;
    case SDLK_o: return SDLK_KP_6;
    case SDLK_j: return SDLK_KP_1;
    case SDLK_k: return SDLK_KP_2;
    case SDLK_l: return SDLK_KP_3;
    default: return k;
    }
}

class ArrowChords {
public:
    // A key press: the key to act on. Arrows give the direction of all arrows now
    // held (a keypad diagonal for two perpendicular ones, the arrow itself
    // otherwise); other keys come back unchanged.
    SDL_Keysym keyDown(SDL_Keysym k) {
        if (!isArrowKey(k.sym)) return k;
        held_ |= bit(k.sym);
        if (auto d = direction()) k.sym = *d;
        return k;
    }

    // A key release: for an arrow, the direction still held afterwards (for
    // scenes that repeat a held key), nullopt when no arrow is left; for other
    // keys, nullopt.
    std::optional<SDL_Keycode> keyUp(SDL_Keycode k) {
        if (!isArrowKey(k)) return std::nullopt;
        held_ &= ~bit(k);
        return direction();
    }

    // Forget held arrows (focus lost, a menu took over).
    void reset() { held_ = 0; }

    // The direction of the arrows held now; nullopt for none or only opposites.
    std::optional<SDL_Keycode> direction() const {
        const int v = (held_ & kUp ? -1 : 0) + (held_ & kDown ? 1 : 0);
        const int h = (held_ & kLeft ? -1 : 0) + (held_ & kRight ? 1 : 0);
        if (v < 0 && h > 0) return SDLK_KP_9;
        if (v < 0 && h < 0) return SDLK_KP_7;
        if (v > 0 && h > 0) return SDLK_KP_3;
        if (v > 0 && h < 0) return SDLK_KP_1;
        if (v < 0) return SDLK_UP;
        if (v > 0) return SDLK_DOWN;
        if (h < 0) return SDLK_LEFT;
        if (h > 0) return SDLK_RIGHT;
        return std::nullopt;
    }

private:
    static constexpr unsigned kUp = 1, kDown = 2, kLeft = 4, kRight = 8;
    static unsigned bit(SDL_Keycode k) {
        switch (k) {
        case SDLK_UP: return kUp;
        case SDLK_DOWN: return kDown;
        case SDLK_LEFT: return kLeft;
        case SDLK_RIGHT: return kRight;
        default: return 0;
        }
    }
    unsigned held_ = 0;
};

}  // namespace redux
