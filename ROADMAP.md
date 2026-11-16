# Roadmap

How Covert Redux gets from an engine skeleton to the whole game. The reference for
every step is the reconstructed original in `../covert-decomp` (byte-exact C and
assembly), so the port follows the original's rules rather than guessing them.

## How the original is organized

COVERT.EXE is a launcher that runs one program after another, passing the game
state in memory (`env.sve`): FINAL (title, main menu, new case, debriefing), GAME
(the city/office loop: travel, intelligence, messages, time), TAC (break-ins),
CHASE (car chases), BUG (wiretaps) and CODE (code breaking), with INTRO (hardware
menu, not needed here). Covert Redux is one program: each of those becomes a set of
scenes sharing one `World`.

## Phases

1. **Engine and formats** (in progress)
   - [x] skeleton: CMake/SDL2, indexed 320x200 frame, game-data lookup, `redux-tool`
   - [x] keyed text files (`TEXT.DTA`, `CLUES.TXT`, `PLOT.TXT`, `PROSE.DTA`)
   - [x] world tables, `crimeN.dta`/`worldN.dta`, saved-game layout
   - [x] the original's random numbers (MSC `rand`, `random(n)`)
   - [x] pictures (`.PIC`, LZW) and palettes
   - [x] fonts (`FONTS.CV`) and text drawing; text windows and menus (TEXTWIN.C)
   - [x] animations (`.PAN` bytecode player)
   - [x] input: keyboard, mouse, the menu conventions
   - [ ] sound (the effects the sound drivers play; AdLib/MT-32 music later)
2. **Title and front end** (FINAL.C): [x] title animation, credits, main menu,
   [x] new character (NEWCHAR.C), [x] plot briefing and leader line-up,
   [x] Hall of Fame, [x] load game (and the save menu: cv0-3.sve in the
   original's format, src/game/SaveGame.*), [ ] practice menu.
3. **Case generation** (CASE.C + GAMELIB CLUE.C): [x] a new case from the
   crime/world files, byte-identical to the original's C for the same seed
   (`tests/msc`).
4. **Mini-games**, each self-contained and playable in practice mode:
   [x] CODE (code breaking), [x] BUG (wiretaps), [x] CHASE (car chase),
   [x] TAC (break-ins). Their generators are byte-identical to the original's C
   (`tests/msc-*`); hooking them to a real case comes with the city loop.
5. **The city loop** (GAME): travel, CIA office, intelligence screens, messages and
   decoding, time and events, suspects and arrests.
6. **End of case** (ENDCASE.C): [x] debriefing, timeline, efficiency report,
   MasterMind clue, vacation, Hall of Fame, career, promotion, retirement and the
   continue/save menu (src/game/EndCase.*, src/app/EndCaseScene.cpp), [x] the next
   case's briefing and generate_case; [ ] hooked to the city loop (both ways).
7. **Modern extras**, behind options: window scaling and full screen (done),
   remappable keys, save anywhere, a faster pace, higher-resolution text.

## Verification

- Formats: decode every file of the original and look at the result
  (`redux-tool`), against the original routines' behaviour.
- Logic: the same inputs and random seed must give the original's results; where
  practical, compare against the original programs run in DOSBox-X.
