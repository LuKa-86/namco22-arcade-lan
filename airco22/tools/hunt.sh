#!/bin/bash
# Air Combat 22: run until the program reaches code that is not translated yet, add that address to seeds.txt, regenerate, again.
#   airco22/tools/hunt.sh [rounds] [frames] [extra ac.exe arguments ...]
# Stops when a run gets through all its frames without a trap, or the same address traps twice.
HERE="$(cd "$(dirname "$0")/.." && pwd)"
ROUNDS=${1:-20}; FRAMES=${2:-12000}; shift 2 2>/dev/null
cd "$HERE"
last=
for ((r = 1; r <= ROUNDS; r++)); do
    ./build/ac.exe "$(pwd -W)/extracted" --frames "$FRAMES" --autoplay "$@" > run.out 2>&1
    t=$(grep -a -m1 '^\[TRAP\]' build/airco22.log | sed -n 's/.*-> 0x\([0-9A-Fa-f]*\):.*/\1/p')
    if [ -z "$t" ]; then echo "round $r: no trap in $FRAMES frames"; grep -a "^\[AL\] frame" build/airco22.log | tail -1; exit 0; fi
    echo "round $r: $(grep -a -m1 '^\[TRAP\]' build/airco22.log)"
    if [ "$t" = "$last" ]; then echo "same address again -- stopping"; exit 1; fi
    last=$t
    echo "$t" >> seeds.txt
    bash tools/regen.sh > regen.out 2>&1 || { echo "regen failed"; tail -5 regen.out; exit 1; }
    grep -E "in all|functions," regen.out | tail -2
done
