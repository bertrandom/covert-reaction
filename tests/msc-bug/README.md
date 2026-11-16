# Differential test: BUG board generation against the original C

`BUGGEN.C` is covert-decomp's `build_board` (src/BUG/BOARD.C) with the signal
propagation of `board_status`, drawing removed, compiled with the real Microsoft
C 5.1 and run in DOSBox-X. For difficulties 0-3 and seeds 0-249 it dumps the
chips (with their placed/locked flags), the links between columns, the spare
chip, the number of swaps, the output signals, a hash of the trace bits and the
next `rand()`. The trace bits read from board.pic take no random numbers, so
both sides start from a blank picture.

`reaction-tool bug-gen` prints the same dump from the C++ port (game/Wiretap); the
two were byte-identical for all 1000 boards when the port was written.

Run it like tests/msc: with covert-decomp's `toolchain/msc51` mounted as C: and
this directory (CRLF line endings) as D:, `RUN.BAT` compiles with
`CL /AS /Gs /Oas` and writes `OUT.TXT`; diff it with `reaction-tool bug-gen`.
