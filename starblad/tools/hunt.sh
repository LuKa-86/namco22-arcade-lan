#!/bin/bash
# Starblade: run headless, take the first trap's target as a seed (master or slave), regen, again -- until a run has no trap.
#   N22TOOLS=... [HUNT_ARGS="--coin 1800 --start 1900 --play 2000"] starblad/tools/hunt.sh [FRAMES [ROUNDS]]
HERE="$(cd "$(dirname "$0")/.." && pwd)"
FR=${1:-3000}; RO=${2:-10}
for ((r = 1; r <= RO; r++)); do
    ( cd "$HERE/build" && rm -f starblad.log && S21_TRAPREGS=1 ./cs21.exe ../extracted --frames $FR --stats 500 $HUNT_ARGS > /dev/null 2>&1 )
    t=$(grep -m1 "^\[TRAP\]" "$HERE/build/starblad.log")
    [ -z "$t" ] && { echo "round $r: no trap in $FR frames"; tail -2 "$HERE/build/starblad.log"; exit 0; }
    echo "round $r: $t"
    a=$(echo "$t" | sed -n 's/.*-> 0x0*\([0-9A-Fa-f]*\).*/\1/p')
    # which CPU: the program whose p-code has an instruction at the trapping address ("at 0x...")
    at=$(echo "$t" | sed -n 's/.* at 0x0*\([0-9A-Fa-f]*\) .*/\1/p' | tr 'A-F' 'a-f')
    c=m; grep -q "^I $at " "$HERE/sbs.pcode" && ! grep -q "^I $at " "$HERE/sbm.pcode" && c=s
    grep -m1 "\] trap " "$HERE/build/starblad.log" | grep -q "cs21-slave" && c=s     # S21_TRAPREGS names the CPU outright
    grep -m1 "\] trap " "$HERE/build/starblad.log" | grep -q "cs21-master" && c=m
    [ -z "$a" ] && { echo "cannot read the target"; exit 1; }
    grep -qix "0*$a" "$HERE/seeds_$c.txt" 2>/dev/null && { echo "seed $a already in seeds_$c.txt: Ghidra did not take it"; exit 1; }
    echo "$a" >> "$HERE/seeds_$c.txt"
    "$HERE/tools/regen.sh" $c || exit 1
done
