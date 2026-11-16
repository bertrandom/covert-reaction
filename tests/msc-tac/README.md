# Differential test: TAC building generation against the original C

`TACGEN.C` is covert-decomp's `generate_building` and its helpers (src/TAC/ROOM.C,
`generate_building` .. `set_wall`, copied verbatim) with TAC.C's `obj_types` table,
compiled with the real Microsoft C 5.1 and run in DOSBox-X. For every city x
organization (two game seeds, varying difficulties) plus kidnap, rescue (with a
captured agent), bomb and suspect-inside variants (576 buildings), it seeds
`srand` the way TAC's `main()` does, `(city + 13) * (org + 7) * 23 + (seed & 3FFFh)`,
generates, and dumps the rooms, the doors, hashes of the object/room/wall grids,
the captured-agent and suspect tiles and the next `rand()`.

`redux-tool tac-gen` prints the same dump from the C++ port (game/TacBuilding);
the two were byte-identical for all 576 buildings when the port was written.

`wall_map` is placed in a struct between `guards_left` and `cur_org` because
`split_rooms` reads a few bytes on either side of it, as in TAC's DGROUP (those
reads turn out never to decide anything; see game/TacBuilding.h).

Run it like tests/msc-bug: with covert-decomp's `toolchain/msc51` mounted as C:
and a copy of this directory (CRLF line endings) as D:, `RUN.BAT` compiles with
`CL /AS /Gs /Oas` and writes `OUT.TXT`; diff it (CRs stripped) with
`redux-tool tac-gen`.
