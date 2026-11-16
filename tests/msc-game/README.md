# Differential test: GAME's world simulation against the original C

`src/game/Sim` ports GAME.EXE's world simulation (TIME.C, CHRONO.C, GAMELIB's
CLUE.C / MSGTEXT.C / PERSON.C, TRAVEL.C and the rules behind the city screens).
This test runs covert-decomp's **own sources, unchanged** — `TIME.C`, `CHRONO.C`,
`CLUE.C`, `MSGTXTG.C`, `PERSON.C`, `TRAVEL.C` (plus `CITY.C`, `SUSPECTS.C`,
`SCREENS.C` for the city actions), compiled with the real Microsoft C 5.1
(`CL /c /AS /Gs /Oas /Zl`, as covert-decomp's rebuild of GAME.EXE) — linked with:

- `HARNESS.C`: the program around them: it loads the starting world, runs GAME's
  new-game sequence (`main` → `mission_briefing`), then N days of
  `advance_time(1440)` with optional CIA scans / city actions, and prints a dump of
  every table after the briefing and after each day. `random()` is MSC's `rand()`
  with its state printed. The screens are stubs, except those whose text matters:
  `incoming_message` prints the teletype (`TT title|text`), `show_menu` the window
  (`MN text`), and `get_key` the text buffer at every wait for a key (`WK text`).
  `drv_char_width` gives FONTS.CV's widths (the clue cards' layout decides how many
  random() calls they make). `set_exit_state` prints `END` and the final dump.
- `HDATA.C`: the tables, laid out so that the original's reads outside them hit
  the same neighbours as in GAME's DGROUP (`persons[-1]`, `msgs[-1]`, `msgs[128]`;
  check `HARNESS.MAP`).

`redux-tool sim CRIME WORLD DIFFICULTY SEED DAYS --scan S --raw` prints the same
lines from the port; `--save-init FILE` writes the world it starts from (a case
from the ported `CaseGenerator`, as a saved game), which is the harness's input.
The two outputs must be byte-identical.

## Running

```bash
cmake -S . -B build-sim && cmake --build build-sim -j8 --target redux-tool
tests/msc-game/run.sh                 # tests/msc-game/cases.txt
tests/msc-game/run.sh my-cases.txt    # lines: CRIME WORLD DIFFICULTY SEED DAYS SCAN
```

It needs covert-decomp next to this repository (or `DECOMP=...`) for the sources and
`toolchain/msc51`, DOSBox-X (`DOSBOX=...`), and the game data (`COVERT_DATA=...`,
default `resources/Covert`, mounted read-only as E:). The work directory
(`WORK`, default `build-sim/msc-game`) gets the sources, the objects, `IN<n>.SVE`,
`EXP<n>.TXT` (port) and `GOT<n>.TXT` (harness). SCAN is `none`, `local`, `intl`,
`both` or `city`. `city` runs one scripted city action after each day, the same in
`HARNESS.C` (`city_day`, calling the original `research_office`, `watch_building`,
`interrogate_suspect`, `success_briefing`, `captured_screen`, `fly_to_city`,
`show_prose`, ... and inlining the menu screens' few lines) and `tools/cmd_sim.cpp`
(`cityDay`): travel, the hotel lounge, the research office, watching a building,
interrogating (or turning) a participant, accusing the CIA contact / being captured
(with and without the prisoner exchange), a wiretap placed at a participant's
building, a break-in's success briefing.

## Status

When the port was written: the 38 cases of `cases.txt` and 196 more (all 13 crimes,
the 3 worlds, the 4 difficulties, random seeds, 32-40 days, i.e. to the end of the
case, every SCAN) were byte-identical: tables, random state, and every teletype,
text window and key wait text, day by day.

Not covered by the harness: the screens' own drawing, `chronology_screen`,
`coded_messages_screen` (its `compose_intercept_text` is ported but only reached
through CODE), `show_person_clues`, `hotel_rest`, the city loop's returns from the
mini-games (`city_loop`'s `last_exit` cases), and states the scripted actions rarely
reach (jailbreaks, prisoner exchanges).
