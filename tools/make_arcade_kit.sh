#!/bin/bash
# Put together the Arcade LAN package (the folder copied to every cabinet PC) from the builds, in MSYS2 MINGW64:
#
#   tools/make_arcade_kit.sh <package folder> [<folder with your MAME zips>]
#
#   <package>\
#     Namco 22 Arcade LAN.exe        the launcher (launcher/): pick a game and how to play it; auto start
#     READ ME.txt
#     games\<Game>\                   <Name>-ArcadeLAN.exe, picture.bmp, roms\ (that game's zips), its settings
#     advanced\                       a .bat for every way to play (the launcher's buttons, for scripts and shortcuts)
#     mesa\                           optional: Mesa's opengl32.dll + libgallium_wgl.dll for a PC without an OpenGL driver
#
# Builds used (a missing one is left out): tokyowar/build/tw.exe, raverace/build/rr.exe, tc2/build/tc2.exe,
# cybrcycc/build/cc.exe, alpiner/build/al.exe, cybsled/build/cs21.exe, launcher/build/"Namco 22 Arcade LAN.exe".
# The zips are never part of this repository: with a zips folder each game's are copied into its roms\ (every game unpacks
# its own zips on its first start).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KIT="${1:?usage: tools/make_arcade_kit.sh <package folder> [<folder with your MAME zips>]}"
ZIPS="$2"
STRIP="$(command -v strip || true)"
mkdir -p "$KIT/games"

# game folder | program in the package | build | zips it needs (space separated)
GAMES=(
  "Tokyo Wars|TokyoWars-ArcadeLAN.exe|tokyowar/build/tw.exe|tokyowar.zip|tokyowar"
  "Rave Racer|RaveRacer-ArcadeLAN.exe|raverace/build/rr.exe|raverace.zip namcoc74.zip|raverace"
  "Ace Driver|AceDriver-ArcadeLAN.exe|acedriver/build/ad.exe|acedrive.zip namcoc74.zip|acedrive"
  "Time Crisis II|TimeCrisis2-ArcadeLAN.exe|tc2/build/tc2.exe|timecrs2.zip|timecrs2"
  "Cyber Cycles|CyberCycles-ArcadeLAN.exe|cybrcycc/build/cc.exe|cybrcycc.zip|cybrcycc"
  "Cyber Commando|CyberCommando-ArcadeLAN.exe|cybrcomm/build/cc.exe|cybrcomm.zip namcoc74.zip|cybrcomm"
  "Armadillo Racing|ArmadilloRacing-ArcadeLAN.exe|adillor/build/am.exe|adillor.zip|adillor"
  "Alpine Racer|AlpineRacer-ArcadeLAN.exe|alpiner/build/al.exe|alpinerjc.zip|alpiner"
  "Cyber Sled|CyberSled-ArcadeLAN.exe|cybsled/build/cs21.exe|cybsled.zip namcoc67.zip namcoc68.zip|cybsled"
)
for g in "${GAMES[@]}"; do
    IFS='|' read -r folder exe build zips pic <<< "$g"
    if [ ! -f "$ROOT/$build" ]; then echo "  $folder: no $build (left out)"; continue; fi
    d="$KIT/games/$folder"; mkdir -p "$d"
    cp "$ROOT/$build" "$d/$exe"; [ -n "$STRIP" ] && "$STRIP" "$d/$exe"      # (the debug symbols are 5-8x the program)
    cp "$ROOT/launcher/pictures/$pic.bmp" "$d/picture.bmp"
    if [ -n "$zips" ]; then mkdir -p "$d/roms"; fi
    have=""
    for z in $zips; do
        if [ -n "$ZIPS" ] && [ -f "$ZIPS/$z" ]; then cp "$ZIPS/$z" "$d/roms/"; have="$have $z"; fi
    done
    if [ "$folder" = "Time Crisis II" ]; then           # (it unpacks roms\timecrs2.zip into extracted\ itself on its first start)
        [ -f "$d/tc2_link.cfg" ] || printf '%s\r\n' "# Time Crisis II twin-cabinet link. The launcher sets the side (left / right)." \
            "# address: auto finds the other cabinet on the network by itself; or the other PC's address. port: the same on both PCs." \
            "link=on" "side=left" "address=auto" "port=23000" "wait=60" "lights=bottom-left" "record=on" > "$d/tc2_link.cfg"
    fi
    echo "  $folder: $exe${have:+, roms:$have}"
done

L="$ROOT/launcher/build/Namco 22 Arcade LAN.exe"
if [ -f "$L" ]; then
    cp "$L" "$KIT/"; [ -n "$STRIP" ] && "$STRIP" "$KIT/Namco 22 Arcade LAN.exe"
    rm -rf "$KIT/advanced"; "$KIT/Namco 22 Arcade LAN.exe" --write-advanced "$(cygpath -w "$KIT/advanced")"
    echo "  launcher + advanced\\ ($(ls "$KIT/advanced" | wc -l) .bat files)"
else
    echo "  (no launcher build: launcher/build/Namco 22 Arcade LAN.exe)"
fi
cp "$ROOT/packaging/arcade-lan-kit/READ ME.txt" "$KIT/"
echo "Package ready: $KIT"
[ -n "$ZIPS" ] || echo "Put each game's MAME zips in games\\<Game>\\roms (see READ ME.txt)."
