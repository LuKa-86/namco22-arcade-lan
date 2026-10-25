#!/bin/bash
# Put together the Arcade LAN kit (the folder copied to every cabinet PC) from the builds:
#
#   tools/make_arcade_kit.sh <kit folder> [<folder with the MAME zips>]
#
# Build first (MSYS2 MINGW64, see the README): tokyowar/build/tw.exe, raverace/build/rr.exe, tc2/build/tc2.exe. A game whose
# build is missing is left out. The ROM zips are copied into <kit>/roms when a folder is given (they are never part of this
# repository); Time Crisis 2 also needs its unpacked files in "<kit>/Time Crisis 2/extracted" (tc2/tools/setup_roms.py).
# Optional: Mesa's software OpenGL (opengl32.dll + libgallium_wgl.dll from a Mesa for Windows build) in <kit>/mesa, for a PC
# without a working OpenGL driver.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KIT="${1:?usage: tools/make_arcade_kit.sh <kit folder> [<folder with the MAME zips>]}"
ROMS="$2"
STRIP="$(command -v strip || true)"
mkdir -p "$KIT/tools" "$KIT/roms"

put() {   # put <built exe> <kit path>: copied and stripped (the debug symbols are 5-8x the program)
    if [ -f "$ROOT/$1" ]; then mkdir -p "$(dirname "$KIT/$2")"; cp "$ROOT/$1" "$KIT/$2"; [ -n "$STRIP" ] && "$STRIP" "$KIT/$2"; echo "  $2"
    else echo "  (no $1: left out)"; fi
}
echo "Programs:"
put tokyowar/build/tw.exe TokyoWars-Link.exe
put raverace/build/rr.exe RaveRacer-Link.exe
put tc2/build/tc2.exe "Time Crisis 2/TimeCrisis2-Link.exe"

cp "$ROOT/packaging/arcade-lan-kit/"*.bat "$ROOT/packaging/arcade-lan-kit/READ ME - LINK TEST.txt" "$KIT/"
cp "$ROOT/packaging/arcade-lan-kit/tools/"*.ps1 "$KIT/tools/"
cp "$ROOT/raverace/tools/link_race.rep" "$KIT/tools/rr_race.rep"     # the race the automatic 4-cabinet check plays
echo "Launchers, READ ME and tools: copied"

if [ -n "$ROMS" ]; then
    for z in tokyowar raverace namcoc74 timecrs2; do [ -f "$ROMS/$z.zip" ] && cp "$ROMS/$z.zip" "$KIT/roms/" && echo "  roms/$z.zip"; done
else
    echo "Put your own MAME zips (tokyowar.zip, raverace.zip + namcoc74.zip) in $KIT/roms"
fi
echo "Kit ready: $KIT"
