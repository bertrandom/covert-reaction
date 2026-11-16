# Differential test: CHASE city maps and car AI against the original C

`CHASEGEN.C` is covert-decomp's CHASE code (src/CHASE/CITY.C `generate_city_map`,
src/CHASE/DRIVE.C `car_select_screen`'s car offers, `init_cars`, `chase_loop`,
`car_in_view`, `road_open`) with the drawing, sound and keyboard removed,
compiled with the real Microsoft C 5.1 and run in DOSBox-X. For every city
(0-15) x difficulty (0-3) x mode (0 follow, 2 chased) it dumps the city map (a
hash), the 16 buildings, the start and destination, the team cars' specs, then
plays the chase with nobody steering for up to 3000 frames, dumping the three
cars every 250 frames, and the end state with the next `rand()`. Odd
difficulties play at night (limited view), the alert level is city * 7.

`redux-tool chase-gen` prints the same dump from the C++ port (game/Chase).

Run it like tests/msc: with covert-decomp's `toolchain/msc51` mounted as C: and
this directory (CRLF line endings) as D:, `RUN.BAT` compiles with
`CL /AS /Gs /Oas` and writes `OUT.TXT`; diff it with `redux-tool chase-gen`.
