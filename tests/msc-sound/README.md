# Differential test: the sound drivers against the original ones

`SNDDUMP.C` loads one of the original drivers (`RSOUND.CA`, `ASOUND.CA`,
`ISOUND.CA` from the game files) the way COVERT does, patches its output
routines into software interrupts that it logs (the MT-32 driver's MPU-401 data
writes, the AdLib driver's OPL register writes, the speaker driver's PIT
channel 2 and port 61h writes), masks the hardware timer and runs the driver's
own timer interrupt handler once per tick. It prints, tick by tick, what the
driver sent, in the format of `reaction-tool sound-events`; `cases.js` lists the
cases (every effect and tune, pause/resume, the car engines, the generated
break-in score with its parameters, effects interrupting music) and compares the
two outputs, including which timer handler (60 or 300 Hz) is installed.

    tests/msc-sound/run.sh [BUILD_DIR]

needs DOSBox-X, covert-decomp's `toolchain/msc51` next to this repository and the
game files in `resources/Covert` (or `COVERT_DATA`). The driver's memory past its
file image (its BSS) starts zeroed, as in the port.
