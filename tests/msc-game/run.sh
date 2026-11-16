#!/bin/bash
# Differential test of src/game/Sim (GAME's world simulation) against covert-decomp's
# C compiled with MSC 5.1 in DOSBox-X. See README.md.
#
#   tests/msc-game/run.sh [CASES]
#
# CASES: a file with one case per line, "CRIME WORLD DIFFICULTY SEED DAYS SCAN"
# (SCAN: none, local, intl or both); default: tests/msc-game/cases.txt.
# Environment: DECOMP (covert-decomp), COVERT_DATA (the game directory), TOOL
# (reaction-tool), WORK (scratch directory), DOSBOX (dosbox-x).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DECOMP="${DECOMP:-$ROOT/../covert-decomp}"
DATA="${COVERT_DATA:-$ROOT/resources/Covert}"
TOOL="${TOOL:-$ROOT/build-sim/reaction-tool}"
WORK="${WORK:-$ROOT/build-sim/msc-game}"
CASES="${1:-$HERE/cases.txt}"
MAC_DOSBOX=/Applications/dosbox-x.app/Contents/MacOS/dosbox-x
if [ -z "$DOSBOX" ]; then
  if [ -x "$MAC_DOSBOX" ]; then DOSBOX="$MAC_DOSBOX"; else DOSBOX="$(command -v dosbox-x)"; fi
fi

mkdir -p "$WORK"
rm -f "$WORK"/*
crlf() { tr -d '\r' < "$1" | sed 's/$/\r/' > "$2"; }
for d in GAME GAMELIB MPSLIB; do
  for f in "$DECOMP/src/$d"/*.[CH]; do crlf "$f" "$WORK/$(basename "$f" | tr a-z A-Z)"; done
done
for f in "$HERE"/*.C; do crlf "$f" "$WORK/$(basename "$f")"; done

scan_bits() { case "$1" in local) echo 1;; intl) echo 2;; both) echo 3;; city) echo 4;; *) echo 0;; esac; }

dosbox() {   # run $WORK/DOSRUN.BAT, killed after $1 seconds
  "$DOSBOX" -silent -fastlaunch -nogui -defaultconf \
    -set "dosbox machine=svga_s3" -set "cpu cycles=max" -set "dos ver=3.30" \
    -c "MOUNT C \"$DECOMP/toolchain/msc51\"" -c "MOUNT D \"$WORK\"" -c "MOUNT E \"$DATA\"" \
    -c "SET PATH=C:\\BIN" -c "SET INCLUDE=C:\\INCLUDE" -c "SET LIB=C:\\LIB" -c "SET TMP=D:\\" \
    -c "D:" -c "CALL D:\\DOSRUN.BAT" -c "EXIT" > /dev/null 2>&1 &
  local pid=$! t=0
  while kill -0 "$pid" 2> /dev/null; do
    sleep 1; t=$((t + 1))
    if [ "$t" -ge "$1" ]; then kill "$pid" 2> /dev/null; echo "DOSBox timed out after $1 s"; break; fi
  done
  wait "$pid" 2> /dev/null || true
}

{
  printf '@ECHO OFF\r\nD:\r\n'
  for f in TIME CHRONO CLUE MSGTXTG PERSON TRAVEL CITY SUSPECTS SCREENS; do
    printf 'CL /c /AS /Gs /Oas /Zl %s.C >> D:\\BUILD.LOG\r\n' "$f"
  done
  printf 'CL /c /AS /Gs /Oas HARNESS.C HDATA.C >> D:\\BUILD.LOG\r\n'
  printf 'LINK /NOI /MAP /STACK:16384 HARNESS+HDATA+TIME+CHRONO+CLUE+MSGTXTG+PERSON+TRAVEL+CITY+SUSPECTS+SCREENS,HARNESS,HARNESS,SLIBCE; >> D:\\BUILD.LOG\r\n'
} > "$WORK/DOSRUN.BAT"
dosbox 300
if grep -iE "error|warning|unresolved" "$WORK/BUILD.LOG" > /dev/null 2>&1; then
  tr -d '\r' < "$WORK/BUILD.LOG" | grep -iE -A3 "error|warning|unresolved" | head -40
  echo "harness build failed (see $WORK/BUILD.LOG)"; exit 1
fi
[ -f "$WORK/HARNESS.EXE" ] || { echo "harness build failed (see $WORK/BUILD.LOG)"; exit 1; }

printf '@ECHO OFF\r\nE:\r\n' > "$WORK/DOSRUN.BAT"
n=0
while read -r crime world diff seed days scan; do
  [ -z "$crime" ] || [ "${crime#\#}" != "$crime" ] && continue
  n=$((n + 1))
  "$TOOL" sim "$crime" "$world" "$diff" "$seed" "$days" --scan "$scan" --raw \
    --save-init "$WORK/IN$n.SVE" > "$WORK/EXP$n.TXT"
  printf 'D:\\HARNESS.EXE D:\\IN%d.SVE %d %d %d > D:\\OUT%d.TXT\r\n' "$n" "$((seed & 0x7FFF))" "$days" \
    "$(scan_bits "$scan")" "$n" >> "$WORK/DOSRUN.BAT"
  echo "$n: $crime $world $diff $seed $days $scan" >> "$WORK/CASES.TXT"
done < "$CASES"

dosbox "${TIMEOUT:-600}"

fail=0
for i in $(seq 1 "$n"); do
  tr -d '\r' < "$WORK/OUT$i.TXT" > "$WORK/GOT$i.TXT" 2>/dev/null || true
  if cmp -s "$WORK/EXP$i.TXT" "$WORK/GOT$i.TXT"; then
    echo "ok   $(sed -n "${i}p" "$WORK/CASES.TXT")"
  else
    fail=$((fail + 1))
    echo "DIFF $(sed -n "${i}p" "$WORK/CASES.TXT"): diff $WORK/EXP$i.TXT $WORK/GOT$i.TXT"
  fi
done
echo "$((n - fail))/$n identical"
[ "$fail" -eq 0 ]
