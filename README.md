| Animations | The animations behind the briefings and GAME's screens (the original's Alt-A, which changes this setting too); the game keeps to it where the original turns them back on at a new case | On |
# Covert Reaction

A modern, portable reimplementation of Sid Meier's Covert Action (MicroProse, 1990)
on SDL2, written in C++20. It plays with the files of the original game: pictures,
animations, fonts and text are loaded at run time from your copy, so no game data
is part of this repository. The game logic follows the reconstructed original
source in [covert-decomp](../covert-decomp).

Status: early. The engine skeleton runs; the resource loaders (pictures, palettes,
fonts, animations) are being written, then the game screens one by one.

## Building

Needs CMake 3.20+, a C++20 compiler and SDL2 (or `sdl2-compat`).

macOS (Homebrew):

```bash
brew install cmake sdl2
```

```bash
cmake -S . -B build && cmake --build build -j
```

The app is `build/Covert Reaction.app`.

Linux (Debian/Ubuntu):

```bash
sudo apt install cmake g++ libsdl2-dev
```

```bash
cmake -S . -B build && cmake --build build -j
```

Windows (Visual Studio 2022, SDL2 through vcpkg):

```bash
vcpkg install sdl2
```

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake && cmake --build build --config Release
```

## Web

The game also builds to WebAssembly with Emscripten, using Emscripten's own SDL2
port (downloaded on the first build). Needs a recent Emscripten with native
WebAssembly exceptions (`brew install emscripten`, or the
[emsdk](https://emscripten.org/docs/getting_started/downloads.html)).

```bash
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release && cmake --build build-web -j8
```

This makes `build-web/index.html`, `index.js` and `index.wasm`. Serve the
directory over HTTP (browsers don't run WebAssembly from `file://`) and open
<http://localhost:8080/>:

```bash
python3 -m http.server -d build-web 8080
```

No game data is in the web build: the page asks each player for their own copy
of Covert Action (pick the game's folder, or drop it on the page). The files are
copied into the browser's storage (IndexedDB) and never uploaded, so they are
asked for only once; "Forget the game files" removes them. Saved games and the
Hall of Fame are kept in IndexedDB too.

For local testing only, `-DREACTION_WEB_PRELOAD_DATA=ON` packs `resources/Covert`
into the build (`index.data`) and starts straight away. That build contains the
original, copyrighted game files: never publish or share it.

## Options

The main menu has an **Options** item (not in the original) for Covert Reaction's own
settings, saved in `settings.ini` in the per-user folder:

| Option | What it does | Default |
|---|---|---|
| Keypad on 789/UIO/JKL | 7 8 9 / U I O / J K L act as the numeric keypad in TAC and BUG (Shift dives in TAC); BUG also takes the number row | On |
| Copy protection | The leader line-up after the briefing counts; a wrong answer means a practice mission | Off |
| Sound | Automatic (MT-32 if its ROMs are found, else AdLib), Roland MT-32, AdLib, PC speaker or Off | Automatic |
| MT-32 clean mixing | Mixes all of the MT-32's partials in phase. The real chip (and the emulator) adds half of them in opposite phase, which can silence a two-partial sound such as TAC's gunshot while the music holds partials; Off is the faithful sound | Off |
| Full screen | Desktop only; Alt+Enter also switches it | Off |

Scripted test runs (`--shot`) ignore the saved settings and use the defaults.

As in the original, inside any text window Alt-A switches the animations, Alt-S the
sound (each plays a short sound, one for on and one for off; turning the sound
off is silent) and Alt-Q asks whether to quit.

## Sound

The original has one sound driver per device (`RSOUND.CA` for the Roland MT-32,
`ASOUND.CA` for the AdLib, `ISOUND.CA` for the PC speaker, `NSOUND.CA` for none),
and the music and effects are inside them. Covert Reaction runs ports of those
drivers (`src/audio`) on the data of your own copy's `.CA` files, at the
drivers' own timer rate (60.88 Hz, 300 Hz for the speaker's and AdLib's noise
effects), and emulates the device they drive:

| Device | Emulation | Needs |
|---|---|---|
| Roland MT-32 (what the game was composed for) | [Munt](https://github.com/munt/munt)'s libmt32emu | your MT-32 (or CM-32L / LAPC-I) control and PCM ROMs |
| AdLib | [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) (OPL2 mode) | nothing |
| PC speaker | the PIT's square wave | nothing |

The Options menu picks the device (Automatic: the MT-32 if its ROMs are found,
else the AdLib) and switches at once; `--sound auto|mt32|adlib|speaker|none` on
the command line overrides it for one run. A device that can't be used falls back
to the next one (MT-32, AdLib, PC speaker, none). Alt+S turns the sound off and on
in the mini-games, as in the original. Scripted test runs (`--shot`) have no sound.
In a browser the sound starts with the first click or key.

**MT-32 ROMs.** The ROMs are Roland's and copyrighted: they are never part of
Covert Reaction and it never downloads them. Put your own dumps in one of these
places (first found wins):

1. `--mt32-roms <directory>` on the command line
2. the `COVERT_MT32_ROMS` environment variable
3. the game directory, or a `MT32`, `MT32/roms`, `mt32-roms` or `roms` folder in it
   or next to it
4. the per-user data folder, or `MT32` in it

Any file names work: the ROMs are recognized by their contents (libmt32emu's
list of known dumps), so Munt/DOSBox names (`MT32_CONTROL.ROM`, `MT32_PCM.ROM`,
`CM32L_CONTROL.ROM`, `CM32L_PCM.ROM`), MAME names (`mt32_ctrl_1_07.rom`,
`mt32_pcm.rom`, `cm32l_ctrl_1_02.rom`, ...) and split halves (`_a`/`_b`, `_l`/`_h`)
all do. With several sets, the original MT-32 is preferred (control ROM 1.07, then
1.06, 1.05, 1.04, BlueRidge, 2.0x), then the CM-32L. `reaction-tool sound-roms` lists
what it finds; the game prints the device it uses on stderr.

`reaction-tool sound-events DEVICE N` prints what a driver sends to its device for
`play_sound(N)` (MIDI bytes, OPL register writes, speaker divisors), and
`reaction-tool sound-wav DEVICE OUT.wav N` renders it. `tests/msc-sound` checks the
ports against the original drivers run in DOSBox-X.

## The game files

Covert Reaction needs an installed copy of the original game (the directory with
`TEXT.DTA`, `TITLE2.PAN`, `FACES.PIC`, ...; GOG and the original floppies both
work). It looks for it, in order:

1. `--data <directory>` on the command line
2. the `COVERT_DATA` environment variable
3. a folder named `Covert` next to the program (or in `Contents/Resources` of the
   macOS app), or `resources/Covert`
4. `Covert` in the per-user data folder (`~/Library/Application Support/Covert Reaction`
   on macOS)

File names are matched case-insensitively, so a copy from DOS, Windows or a
case-sensitive Linux file system all work.

## Layout

| Directory | What it is |
|---|---|
| `src/core` | platform-independent basics: finding and reading game files, indexed images and palettes |
| `src/formats` | the original file formats (pictures, fonts, animations, ...) |
| `src/game` | game logic, ported from the reconstructed original |
| `src/audio` | the sound drivers, ported, and the emulated MT-32, AdLib and PC speaker |
| `src/app` | the SDL2 application: window, input, the scene loop |
| `tools` | `reaction-tool`, a command-line inspector for the game files (`reaction-tool` lists its commands) |

The engine (`src/core`, `src/formats`, `src/game`, `src/audio`) has no SDL dependency, so tools
and tests can use it without a window. The screen is the original 320x200 256-colour
frame, shown at 4:3 with nearest-neighbour scaling; Alt+Enter toggles full screen.

## License

Covert Reaction is free software under the GNU General Public License, version 3 or
(at your option) any later version; see [LICENSE](LICENSE). Third-party libraries
keep their own licenses.

The license covers this project's code only. Covert Action itself (its game files,
and the MT-32 ROMs if you use them) belongs to its owners and is not included:
you supply your own copy.

### Third-party code

Fetched at build time (CMake FetchContent, pinned versions) and built as separate
libraries; none of their code is copied into this repository:

| Library | Version | License | Used for |
|---|---|---|---|
| [Munt](https://github.com/munt/munt) libmt32emu | 2.8.4 | LGPL-2.1-or-later | the MT-32 / CM-32L emulation |
| [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) | 765ec96 (2024) | LGPL-2.1-or-later | the AdLib's OPL2 |
| SDL2 | system | zlib | window, input, audio output |
