#!/bin/bash
# Starblade: a FRESH Ghidra project for each 68000 program (master, slave), then lifted C. Later rounds (new seeds): tools/regen.sh.
#   starblad/tools/import.sh [m|s]     (both when no argument)
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
# the tools folder: ghidra_12.1.4_PUBLIC, jdk-21.0.12.1+1 and the Ghidra project folder work/ (e.g. N22TOOLS=/d/Games/Emulation/n22tools)
T="${N22TOOLS:?set N22TOOLS to the tools folder (ghidra_12.1.4_PUBLIC, jdk-21.0.12.1+1, work/)}"
TW="$(cygpath -w "$T")"
WORK=$T/work
GH=$T/ghidra_12.1.4_PUBLIC
export JAVA_HOME="$TW\jdk-21.0.12.1+1"
export PATH="$T/jdk-21.0.12.1+1/bin:$PATH"
cp "$ROOT/tools/gen/ghidra/"*.java "$WORK/scripts/"
for c in ${1:-m s}; do
    case $c in m) bin=starblad_master.bin; p=sbm; base=cm ;; s) bin=starblad_slave.bin; p=sbs; base=cs ;; esac
    cp "$HERE/$bin" "$WORK/$p.bin"
    touch "$HERE/seeds_$c.txt"; cp "$HERE/seeds_$c.txt" "$WORK/${p}_seeds.txt"
    rm -rf "$WORK/$p.gpr" "$WORK/$p.rep"
    ( cd "$WORK" && "$GH/support/analyzeHeadless.bat" "$TW\work" $p -import "$TW\work\\$p.bin" \
        -loader BinaryLoader -processor 68000:BE:32:MC68020 -scriptPath "$TW\work\scripts" -preScript SeedVectors.java \
        -postScript SeedTables.java "$TW\work\\${p}_seeds.txt" \
        -postScript DumpPcode.java "$TW\work\\$p.pcode" > ${p}_import.log 2>&1 )
    grep -E "SeedTables:|DumpPcode:" "$WORK/${p}_import.log" | sed 's/^INFO *//; s/(GhidraScript).*//'
    cp "$WORK/$p.pcode" "$HERE/$p.pcode"
    rm -f "$HERE"/gen/${base}_lifted*.c "$HERE"/gen/${base}_lifted.h
    ( cd "$ROOT" && python -I tools/gen/lift68k.py starblad/$p.pcode starblad/gen $base 4 starblad/$bin | grep -v "untranslated\|CALLOTHER" )
done
