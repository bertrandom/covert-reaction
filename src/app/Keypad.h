// The numeric keypad for keyboards without one (laptops, compact keyboards).
//
// TAC and BUG move in eight directions, and the original's diagonals come from
// keypad 7 9 1 3 (or Home PgUp End PgDn), which most laptops lack. Two ways in:
//
//   - The number row as the keypad: 1-9 are the keypad digits (5 = keypad 5,
//     sneak in TAC), via digitAsKeypad().
//   - A keypad on the letters, as laptops with an embedded keypad have it:
//     7 8 9 / U I O / J K L (letterKeypad()).
#pragma once

#include <SDL.h>

namespace redux {

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

}  // namespace redux
