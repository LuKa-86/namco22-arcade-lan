#!/bin/bash
# Cyber Cycles: Ghidra (tables + seeds) -> p-code -> lifted C -> build. Run from anywhere:  cybrcycc/tools/regen.sh
# Needs the Ghidra project made by the first import (see docs in CMakeLists.txt) in $WORK.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
WORK=/path/to/n22tools/work
GH=/path/to/n22tools/ghidra_12.1.4_PUBLIC
export JAVA_HOME='C:\path\to\n22tools\jdk-21.0.12.1+1'
export PATH="/path/to/n22tools/jdk-21.0.12.1+1/bin:$PATH"
cp "$ROOT/tools/gen/ghidra/"*.java "$WORK/scripts/"
touch "$HERE/seeds.txt"; cp "$HERE/seeds.txt" "$WORK/cc_seeds.txt"
( cd "$WORK" && "$GH/support/analyzeHeadless.bat" 'C:\path\to\n22tools\work' cybrcycc -process cybrcycc_main.bin -noanalysis \
    -scriptPath 'C:\path\to\n22tools\work\scripts' \
    -postScript SeedTables.java 'C:\path\to\n22tools\work\cc_seeds.txt' \
    -postScript DumpPcode.java 'C:\path\to\n22tools\work\cybrcycc.pcode' > cc_regen.log 2>&1 )
grep -E "SeedTables:|DumpPcode:" "$WORK/cc_regen.log" | sed 's/^INFO *//; s/(GhidraScript).*//'
cp "$WORK/cybrcycc.pcode" "$HERE/cybrcycc.pcode"
( cd "$ROOT" && python -I tools/gen/lift68k.py cybrcycc/cybrcycc.pcode cybrcycc/gen cc 8 cybrcycc/cybrcycc_main.bin | grep -v "untranslated\|CALLOTHER" )
MSYSTEM=MINGW64 /c/msys64/usr/bin/bash.exe -lc "cd '$HERE' && ninja -C build cc 2>&1 | grep -E 'error|Linking'"
