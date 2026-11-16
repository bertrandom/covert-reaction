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
