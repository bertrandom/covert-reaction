#!/bin/bash
# Differential test of the sound driver ports: runs the original drivers in
# DOSBox-X (SNDDUMP.C, built with covert-decomp's MSC 5.1) and compares what
# they send to the device with `reaction-tool sound-events`.
#   tests/msc-sound/run.sh [BUILD_DIR]     (default: build; SND_DRIVERS=RAI picks drivers)
# Needs DOSBox-X, covert-decomp next to this repository (toolchain/msc51) and the
# game files in resources/Covert (or COVERT_DATA).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD="$(cd "${1:-$ROOT/build}" && pwd)"
DATA="${COVERT_DATA:-$ROOT/resources/Covert}"
TOOLCHAIN="${MSC51:-$ROOT/../covert-decomp/toolchain/msc51}"
DOSBOX="${DOSBOX:-/Applications/dosbox-x.app/Contents/MacOS/dosbox-x}"
[ -x "$DOSBOX" ] || DOSBOX="$(command -v dosbox-x)"
W="$BUILD/msc-sound"
mkdir -p "$W"
rm -f "$W"/*.TXT
node -e 'const fs=require("fs");fs.writeFileSync(process.argv[2],fs.readFileSync(process.argv[1],"utf8").replace(/\r?\n/g,"\r\n"))' \
  "$HERE/SNDDUMP.C" "$W/SNDDUMP.C"
node "$HERE/cases.js" bat "$W"
"$DOSBOX" -silent -fastlaunch -nogui -defaultconf \
  -set "dosbox machine=svga_s3" -set "cpu cycles=max" -set "dos ver=3.30" \
  -c "MOUNT C \"$TOOLCHAIN\"" -c "MOUNT D \"$W\"" -c "MOUNT E \"$DATA\"" \
  -c "SET PATH=C:\\BIN" -c "SET INCLUDE=C:\\INCLUDE" -c "SET LIB=C:\\LIB" -c "SET TMP=D:\\" \
  -c "D:" -c "CALL D:\\RUN.BAT" -c "EXIT" > /dev/null 2>&1 || true
node "$HERE/cases.js" compare "$W" "$BUILD/reaction-tool" "$DATA"
