# Covert Redux

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

The app is `build/Covert Redux.app`.

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

For local testing only, `-DREDUX_WEB_PRELOAD_DATA=ON` packs `resources/Covert`
into the build (`index.data`) and starts straight away. That build contains the
original, copyrighted game files: never publish or share it.

## The game files

Covert Redux needs an installed copy of the original game (the directory with
`TEXT.DTA`, `TITLE2.PAN`, `FACES.PIC`, ...; GOG and the original floppies both
work). It looks for it, in order:

1. `--data <directory>` on the command line
2. the `COVERT_DATA` environment variable
3. a folder named `Covert` next to the program (or in `Contents/Resources` of the
   macOS app), or `resources/Covert`
4. `Covert` in the per-user data folder (`~/Library/Application Support/CovertRedux`
   on macOS)

File names are matched case-insensitively, so a copy from DOS, Windows or a
case-sensitive Linux file system all work.

## Layout

| Directory | What it is |
|---|---|
| `src/core` | platform-independent basics: finding and reading game files, indexed images and palettes |
| `src/formats` | the original file formats (pictures, fonts, animations, ...) |
| `src/game` | game logic, ported from the reconstructed original |
| `src/app` | the SDL2 application: window, input, the scene loop |
| `tools` | `redux-tool`, a command-line inspector for the game files (`redux-tool` lists its commands) |

The engine (`src/core`, `src/formats`, `src/game`) has no SDL dependency, so tools
and tests can use it without a window. The screen is the original 320x200 256-colour
frame, shown at 4:3 with nearest-neighbour scaling; Alt+Enter toggles full screen.

## License

Covert Redux is free software under the GNU General Public License, version 3 or
(at your option) any later version; see [LICENSE](LICENSE). Third-party libraries
keep their own licenses.

The license covers this project's code only. Covert Action itself (its game files,
and the MT-32 ROMs if you use them) belongs to its owners and is not included:
you supply your own copy.
