#!/bin/bash
# Starblade: Ghidra (tables + seeds) -> p-code -> lifted C -> build, re-reading the projects tools/import.sh made.
#   starblad/tools/regen.sh [m|s]      (both when no argument; seeds in starblad/seeds_m.txt / seeds_s.txt)
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
T="${N22TOOLS:?set N22TOOLS to the tools folder (ghidra_12.1.4_PUBLIC, jdk-21.0.12.1+1, work/)}"
TW="$(cygpath -w "$T")"
WORK=$T/work
GH=$T/ghidra_12.1.4_PUBLIC
export JAVA_HOME="$TW\jdk-21.0.12.1+1"
export PATH="$T/jdk-21.0.12.1+1/bin:$PATH"
cp "$ROOT/tools/gen/ghidra/"*.java "$WORK/scripts/"
for c in ${1:-m s}; do
    case $c in m) bin=starblad_master.bin; p=sbm; base=cm ;; s) bin=starblad_slave.bin; p=sbs; base=cs ;; esac
    touch "$HERE/seeds_$c.txt"; cp "$HERE/seeds_$c.txt" "$WORK/${p}_seeds.txt"
    ( cd "$WORK" && "$GH/support/analyzeHeadless.bat" "$TW\work" $p -process $p.bin -noanalysis -scriptPath "$TW\work\scripts" \
        -postScript SeedTables.java "$TW\work\\${p}_seeds.txt" \
        -postScript DumpPcode.java "$TW\work\\$p.pcode" > ${p}_regen.log 2>&1 )
    grep -E "DumpPcode:" "$WORK/${p}_regen.log" | sed 's/^INFO *//; s/(GhidraScript).*//'
    cp "$WORK/$p.pcode" "$HERE/$p.pcode"
    rm -f "$HERE"/gen/${base}_lifted*.c "$HERE"/gen/${base}_lifted.h
    ( cd "$ROOT" && python -I tools/gen/lift68k.py starblad/$p.pcode starblad/gen $base 4 starblad/$bin | grep -v "untranslated\|CALLOTHER" )
done
MSYSTEM=MINGW64 /c/msys64/usr/bin/bash.exe -lc "cd '$HERE' && ninja -C build cs21 2>&1 | grep -E 'error|Linking'"
