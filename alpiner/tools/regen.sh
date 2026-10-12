#!/bin/bash
# Alpine Racer: Ghidra (tables + seeds) -> p-code -> lifted C -> build. Run from anywhere:  alpiner/tools/regen.sh
# Needs the Ghidra project made by the first import (see docs in CMakeLists.txt) in $WORK.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
WORK=/path/to/n22tools/work
GH=/path/to/n22tools/ghidra_12.1.4_PUBLIC
export JAVA_HOME='C:\path\to\n22tools\jdk-21.0.12.1+1'
export PATH="/path/to/n22tools/jdk-21.0.12.1+1/bin:$PATH"
cp "$ROOT/tools/gen/ghidra/"*.java "$WORK/scripts/"
touch "$HERE/seeds.txt"; cp "$HERE/seeds.txt" "$WORK/seeds.txt"
( cd "$WORK" && "$GH/support/analyzeHeadless.bat" 'C:\path\to\n22tools\work' alpiner -process alpiner_main.bin -noanalysis \
    -scriptPath 'C:\path\to\n22tools\work\scripts' \
    -postScript SeedTables.java 'C:\path\to\n22tools\work\seeds.txt' \
    -postScript DumpPcode.java 'C:\path\to\n22tools\work\alpiner.pcode' > ghidra_regen.log 2>&1 )
grep -E "SeedTables:|DumpPcode:" "$WORK/ghidra_regen.log" | sed 's/^INFO *//; s/(GhidraScript).*//'
cp "$WORK/alpiner.pcode" "$HERE/alpiner.pcode"
( cd "$ROOT" && python -I tools/gen/lift68k.py alpiner/alpiner.pcode alpiner/gen al 8 alpiner/alpiner_main.bin | grep -v "untranslated\|CALLOTHER" )
MSYSTEM=MINGW64 /c/msys64/usr/bin/bash.exe -lc "cd '$HERE' && ninja -C build al 2>&1 | grep -E 'error|Linking'"
