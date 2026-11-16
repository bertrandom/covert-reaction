// SDL key events as the original's get_key() codes (UI.C): ASCII for character
// keys (13 Enter, 27 Esc, 8 Backspace), scan code << 8 for the others (4800h Up,
// 5000h Down, 4B00h Left, 4D00h Right, 4700h Home, 4F00h End, 4900h PgUp,
// 5100h PgDn, 5200h Insert, 5300h Delete, 3B00h F1). The keypad's digits count
// as the arrows they are on, as with NumLock off.
#pragma once

#include <SDL.h>

namespace redux {

// The code of a key press, -1 if the event isn't one (or is a modifier alone).
// Character keys give their unshifted/shifted ASCII from the key symbol, for
// keys pushed by the scripted mode; text entry should prefer SDL_TEXTINPUT
// (see textChar).
inline int dosKey(const SDL_Event& e) {
    if (e.type != SDL_KEYDOWN) return -1;
    const SDL_Keycode k = e.key.keysym.sym;
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return 13;
    case SDLK_ESCAPE: return 27;
    case SDLK_BACKSPACE: return 8;
    case SDLK_TAB: return 9;
    case SDLK_UP: case SDLK_KP_8: return 0x4800;
    case SDLK_DOWN: case SDLK_KP_2: return 0x5000;
    case SDLK_LEFT: case SDLK_KP_4: return 0x4B00;
    case SDLK_RIGHT: case SDLK_KP_6: return 0x4D00;
    case SDLK_HOME: case SDLK_KP_7: return 0x4700;
    case SDLK_END: case SDLK_KP_1: return 0x4F00;
    case SDLK_PAGEUP: case SDLK_KP_9: return 0x4900;
    case SDLK_PAGEDOWN: case SDLK_KP_3: return 0x5100;
    case SDLK_INSERT: case SDLK_KP_0: return 0x5200;
    case SDLK_DELETE: case SDLK_KP_PERIOD: return 0x5300;
    case SDLK_F1: return 0x3B00;
    default: break;
    }
    if (k >= 32 && k < 127) {
        int c = static_cast<int>(k);
        if ((e.key.keysym.mod & (KMOD_SHIFT | KMOD_CAPS)) && c >= 'a' && c <= 'z') c -= 'a' - 'A';
        return c;
    }
    return -1;
}

// Whether a character key press will also come as SDL_TEXTINPUT: real key
// events carry the window's id, the scripted mode's pushed ones don't.
inline bool keyComesAsText(const SDL_Event& e) {
    return e.type == SDL_KEYDOWN && e.key.windowID != 0 && e.key.keysym.sym >= 32 && e.key.keysym.sym < 127;
}

// The ASCII character of an SDL_TEXTINPUT event, -1 if none.
inline int textChar(const SDL_Event& e) {
    if (e.type != SDL_TEXTINPUT) return -1;
    const auto c = static_cast<unsigned char>(e.text.text[0]);
    return c >= 32 && c < 127 && e.text.text[1] == 0 ? c : -1;
}

}  // namespace redux
