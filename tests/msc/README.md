# Differential test: case generation against the original C

`HARNESS.C` is covert-decomp's `generate_case` and the GAMELIB routines it calls
(drawing removed), compiled with the real Microsoft C 5.1 and run in DOSBox-X. It
dumps every table the case generator writes, and the next `rand()`, for each
crime × world × difficulty × seed. `reaction-tool case-gen ... --raw` prints the same
dump from the C++ port; the two must be byte-identical (they were for all 936 cases
when the port was written).

To run it you need covert-decomp's `toolchain/msc51` (MSC 5.1) and its
`tools/dosrun.sh`; compile `HARNESS.C` with `CL /AS /Gs /Oas` in DOSBox-X with the
game data directory mounted, run it (`RUN.BAT`), and diff its output with the
port's.
