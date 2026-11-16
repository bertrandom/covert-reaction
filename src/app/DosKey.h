// SDL key events as the original's get_key() codes (UI.C): ASCII for character
// keys (13 Enter, 27 Esc, 8 Backspace), scan code << 8 for the others (4800h Up,
// 5000h Down, 4B00h Left, 4D00h Right, 4700h Home, 4F00h End, 4900h PgUp,
// 5100h PgDn, 5200h Insert, 5300h Delete, 3B00h F1). The keypad's digits count
// as the arrows they are on, as with NumLock off. With Alt held, keys give what
// the BIOS gives for them: Alt+letter its scan code << 8 (1E00h Alt-A, 1F00h
// Alt-S, 1000h Alt-Q), Alt+digit 7800h..8100h, Alt+Esc 0100h, Alt+Up 9800h ...
// (never the plain key), so screens that don't look for them ignore them.
#pragma once

#include <SDL.h>

namespace reaction {

// The BIOS code of an Alt+key (enhanced keyboard), -1 if none (Alt+keypad
// digits type a character code on DOS, not a key).
inline int altDosKey(SDL_Keycode k) {
    if (k >= SDLK_a && k <= SDLK_z) {
        static constexpr unsigned char kScan[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                                    0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                                    0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
        return kScan[k - SDLK_a] << 8;
    }
    if (k >= SDLK_1 && k <= SDLK_9) return (0x78 + static_cast<int>(k - SDLK_1)) << 8;
    switch (k) {
    case SDLK_0: return 0x8100;
    case SDLK_MINUS: return 0x8200;
    case SDLK_EQUALS: return 0x8300;
    case SDLK_LEFTBRACKET: return 0x1A00;
    case SDLK_RIGHTBRACKET: return 0x1B00;
    case SDLK_SEMICOLON: return 0x2700;
    case SDLK_QUOTE: return 0x2800;
    case SDLK_BACKQUOTE: return 0x2900;
    case SDLK_BACKSLASH: return 0x2B00;
    case SDLK_COMMA: return 0x3300;
    case SDLK_PERIOD: return 0x3400;
    case SDLK_SLASH: return 0x3500;
    case SDLK_SPACE: return ' ';
    case SDLK_ESCAPE: return 0x0100;
    case SDLK_BACKSPACE: return 0x0E00;
    case SDLK_RETURN: return 0x1C00;
    case SDLK_KP_ENTER: return 0xA600;
    case SDLK_TAB: return 0xA500;
    case SDLK_F1: return 0x6800;
    case SDLK_UP: return 0x9800;
    case SDLK_DOWN: return 0xA000;
    case SDLK_LEFT: return 0x9B00;
    case SDLK_RIGHT: return 0x9D00;
    case SDLK_HOME: return 0x9700;
    case SDLK_END: return 0x9F00;
    case SDLK_PAGEUP: return 0x9900;
    case SDLK_PAGEDOWN: return 0xA100;
    case SDLK_INSERT: return 0xA200;
    case SDLK_DELETE: return 0xA300;
    default: return -1;
    }
}

// The code of a key press, -1 if the event isn't one (or is a modifier alone).
// Character keys give their unshifted/shifted ASCII from the key symbol, for
// keys pushed by the scripted mode; text entry should prefer SDL_TEXTINPUT
// (see textChar).
inline int dosKey(const SDL_Event& e) {
    if (e.type != SDL_KEYDOWN) return -1;
    const SDL_Keycode k = e.key.keysym.sym;
    if (e.key.keysym.mod & KMOD_ALT) return altDosKey(k);
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

}  // namespace reaction
