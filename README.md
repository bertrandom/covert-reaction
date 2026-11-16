# Covert Reaction

A modern, portable remake of Sid Meier's Covert Action (MicroProse, 1990) in C++20
and SDL2, for macOS, Linux, Windows and the web.

It plays with the files of your own copy of the original game: pictures,
animations, fonts, text and the sound drivers' music are loaded at run time, so
no game data is part of this repository. The game logic is ported from the
byte-exact reconstruction of the original source in
[covert-decomp](../covert-decomp), and the rules follow that code rather than
guesswork: case generation, the world simulation, the four mini-games'
generators and the sound drivers give byte-identical results to the original
for the same random seed, checked against the original C compiled with
Microsoft C 5.1 and run in DOSBox-X (`tests/`).

## Status

The whole game is playable, from the title to retirement:

- **Front end:** title and credits, new character, plot and case briefings, the
  leader line-up, the Hall of Fame, practice missions, saving and loading.
- **The city loop:** travel, the hotel, the CIA (intelligence scans, the research
  office, coded messages, accusing double agents), buildings (wiretap, break in,
  watch, follow), data files, dossiers, clue cards, the teletype, time and events.
- **Mini-games**, both in practice and in a case, where what you find changes the
  case: break-ins (TAC), car chases (CHASE), wiretaps and car tracers (BUG) and
  code breaking (CODE).
- **End of case:** debriefing, timeline, scoring, promotion, the next case and
  retirement.
- **Sound:** the original's MT-32, AdLib and PC speaker drivers.

Not done yet:

- Joystick or gamepad support (the original has a joystick option).
- Modern extras: remappable keys, save anywhere, a faster pace, higher-resolution
  text.
- A few paths are ported but have not been played through yet: being captured,
  prisoner exchanges and jailbreaks.

[ROADMAP.md](ROADMAP.md) has the details.

## Quick start (macOS)

```bash
brew install cmake sdl2
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

Put your copy of the game in `resources/Covert` (see [The game files](#the-game-files)),
then:

```bash
open "build/Covert Reaction.app"
```

## Building

Needs CMake 3.20+, a C++20 compiler and SDL2 (or `sdl2-compat`). The first build
downloads two sound libraries (see [Third-party code](#third-party-code)), so it
needs an internet connection.

Linux (Debian/Ubuntu):

```bash
sudo apt install cmake g++ libsdl2-dev
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

Windows (Visual Studio 2022, SDL2 through vcpkg):

```bash
vcpkg install sdl2
```

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake && cmake --build build --config Release
```

On macOS the result is `build/Covert Reaction.app` (bundle identifier
`org.bert.covertreaction`); elsewhere `build/covert-reaction`. The app isn't
code-signed: it runs on the Mac that built it, but sharing it would need signing
and notarization.

### Web

The game also builds to WebAssembly with Emscripten (`brew install emscripten`, or
the [emsdk](https://emscripten.org/docs/getting_started/downloads.html)), using
Emscripten's own SDL2 port:

```bash
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release && cmake --build build-web -j8
```

This makes `build-web/index.html`, `index.js` and `index.wasm`. Serve the folder
over HTTP and open <http://localhost:8080/>:

```bash
python3 -m http.server -d build-web 8080
```

The page asks each player for their own copy of Covert Action (pick the game's
folder or drop it on the page). The files are kept in the browser's storage and
never uploaded, so they are asked for only once; "Forget the game files" removes
them. Saved games and settings are kept there too. Sound starts with the first
click or key, as browsers require.

For local testing only, `-DREACTION_WEB_PRELOAD_DATA=ON` packs `resources/Covert`
into the build and starts straight away. That build contains the original,
copyrighted game files: never publish or share it.

## The game files

Covert Reaction needs an installed copy of the original game: the folder with
`TEXT.DTA`, `TITLE2.PAN`, `FACES.PIC` and so on. GOG's release and the original
floppies both work. It looks, in order, in:

1. `--data <folder>` on the command line
2. the `COVERT_DATA` environment variable
3. a folder named `Covert` next to the program or next to the macOS app (or in its
   `Contents/Resources`), and `resources/Covert` in this repository
4. `Covert` in the per-user folder (see [Saved games and settings](#saved-games-and-settings))

File names are matched case-insensitively.

## Playing

The keys are the original's (see the original manual and reference card). A few
notes:

- **Text windows and menus:** Up/Down move, Enter, Space or F1 choose, Esc
  cancels; the mouse also works. As in the original, inside any text window
  Alt-A switches animations, Alt-S sound and Alt-J the joystick bit (each with a
  short sound), and Alt-Q asks whether to quit.
- **No numeric keypad?** In TAC and BUG, 7 8 9 / U I O / J K L work as the
  keypad (7 8 9 / 4 5 6 / 1 2 3), and BUG also takes the number row. In TAC
  these walk and Shift+key dives (the arrows and Home/End/PgUp/PgDn dive, as in
  the original); I (keypad 5) toggles sneaking.
- **Function keys on a Mac:** TAC's actions are on F1-F10, which a Mac's top row
  only sends with Fn held down, unless "Use F1, F2, etc. keys as standard function
  keys" is on in System Settings › Keyboard.
- **Full screen:** Alt+Enter (desktop).

### Options

The main menu has an **Options** item, not in the original, for Covert Reaction's
own settings:

| Option | What it does | Default |
|---|---|---|
| Keypad on 789/UIO/JKL | The letter keypad above, in TAC and BUG | On |
| Copy protection | The leader line-up after the briefing counts: a wrong face means a practice mission, as in the unpatched 1990 release (GOG's release removes the check) | Off |
| Sound | Automatic (the MT-32 if its ROMs are found, else the AdLib), Roland MT-32, AdLib, PC speaker or Off | Automatic |
| Animations | The animations behind the briefings and the city's windows (Alt-A changes it too) | On |
| MT-32 clean mixing | Mixes all of the MT-32's partials in phase. The real chip adds half of them in opposite phase, which can silence a sound such as TAC's gunshot while the music holds partials; Off is the faithful sound | Off |
| Full screen | Desktop only | Off |

### Sound

The original has one sound driver per device (`RSOUND.CA` for the Roland MT-32,
`ASOUND.CA` for the AdLib, `ISOUND.CA` for the PC speaker), and the music and
effects are inside them. Covert Reaction runs ports of those drivers on your
copy's `.CA` files and emulates the device they drive:

| Device | Emulation | Needs |
|---|---|---|
| Roland MT-32 (what the music was written for) | [Munt](https://github.com/munt/munt)'s libmt32emu | your MT-32 (or CM-32L / LAPC-I) control and PCM ROMs |
| AdLib | [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) | nothing |
| PC speaker | a square wave | nothing |

`--sound auto|mt32|adlib|speaker|none` overrides the Options setting for one run.
A device that can't be used falls back to the next one.

**MT-32 ROMs** are Roland's and copyrighted: Covert Reaction never includes or
downloads them. Put your own dumps in one of these places (the first found wins):

1. `--mt32-roms <folder>` on the command line
2. the `COVERT_MT32_ROMS` environment variable
3. the game folder, or an `MT32`, `MT32/roms`, `mt32-roms` or `roms` folder in it
   or next to it
4. the per-user folder, or `MT32` in it

The ROMs are recognized by their contents, so any file names work (Munt/DOSBox
names such as `MT32_CONTROL.ROM`, MAME names such as `mt32_ctrl_1_07.rom`, split
halves). With several sets, the original MT-32 is preferred (control ROM 1.07
first), then the CM-32L. `reaction-tool sound-roms` lists what it finds.

### Saved games and settings

Saved games, the Hall of Fame and the Options settings are kept in the per-user
folder:

| System | Folder |
|---|---|
| macOS | `~/Library/Application Support/Covert Reaction` |
| Windows | `%APPDATA%\Covert Reaction` |
| Linux | `~/.local/share/Covert Reaction` |

The four save slots are `cv0.sve` to `cv3.sve`, in the original's format: saves
from the DOS game load (drop them in this folder; a slot with no file here is
also looked for in the game folder). The Hall of Fame is `fame.dta`, the settings
`settings.ini`. `COVERT_REACTION_USER_DIR` uses another folder instead.

### Differences from the original

Kept on purpose:

- There are no disks to choose: the save and load menus go straight to the slots.
- "End Game" and retirement go back to the main menu instead of quitting to DOS.
- Esc in the practice difficulty menu goes back (the original carries on with an
  invalid difficulty).
- The code-breaking screen also takes capital letters.
- The activity report skips a division by zero that crashes the original.
- Copy protection is off by default, as in GOG's release.

## Development

| Folder | What it is |
|---|---|
| `src/core` | basics: finding and reading game files, images and palettes, settings |
| `src/formats` | the original file formats: pictures, fonts, text windows, animations, catalogs |
| `src/game` | the game logic, ported from the original: case generation, the simulation, the mini-games, end of case, saved games |
| `src/audio` | the sound drivers, ported, and the emulated MT-32, AdLib and PC speaker |
| `src/app` | the SDL2 application: window, input, the scenes |
| `tools` | `reaction-tool`, a command-line tool for the game files and the game logic |
| `tests` | harnesses that compare the port with the original's C and drivers (MSC 5.1, DOSBox-X); each has a README |
| `web` | the web page around the WebAssembly build |

The engine (`src/core`, `src/formats`, `src/game`, `src/audio`) has no SDL
dependency, so the tools and tests use it without a window. The screen is the
original 320x200 256-colour frame, shown at 4:3 with nearest-neighbour scaling.

**`reaction-tool`** (run it alone for the full list) decodes files (`pic`, `pan`,
`font-window`, `text`, `world`, `crime`), runs the game logic without a screen
(`case-gen`, `sim`, `endcase`, `tac-gen`, `chase-gen`, `bug-gen`, `tac-case`,
`bug-case`) and the sound drivers (`sound-events`, `sound-wav`, `sound-roms`).

**Scripted runs** start a scene, press keys at given ticks and save the screen
after N ticks (70 ticks a second), without opening a window or making any sound:

```bash
"build/Covert Reaction.app/Contents/MacOS/Covert Reaction" --scene city-street --shot out.bmp --ticks 900 --input "300:Down 330:Down 360:Return"
```

Scenes include `title`, `main-menu`, `new-character`, `city`, `city-street`, `tac`,
`chase`, `bug`, `code` and `endcase`. Scripted runs ignore the saved settings.
Useful environment variables: `COVERT_SEED` and `COVERT_GAME_SEED` (fixed random
seeds), `COVERT_CITY_SEED` and `COVERT_CITY_LEVEL` (the test case), `REACTION_TAC_*`
and `REACTION_BUG_*` (practice setups), `COVERT_COPY_PROTECTION=1`,
`COVERT_SOUND_LOG=1` (log every sound requested) and `REACTION_MT32_DEBUG=1`.

## License

Covert Reaction is free software under the GNU General Public License, version 3
or (at your option) any later version; see [LICENSE](LICENSE).

The license covers this project's code only. Covert Action itself (its game files,
and the MT-32 ROMs if you use them) belongs to its owners and is not included:
you supply your own copy.

### Third-party code

Fetched at build time (CMake FetchContent, pinned versions) and built as separate
libraries; none of their code is in this repository:

| Library | Version | License | Used for |
|---|---|---|---|
| [Munt](https://github.com/munt/munt) libmt32emu | 2.8.4 | LGPL-2.1-or-later | the MT-32 / CM-32L emulation |
| [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) | 765ec96 (2024) | LGPL-2.1-or-later | the AdLib's OPL2 |
| SDL2 | system | zlib | window, input, audio output |
