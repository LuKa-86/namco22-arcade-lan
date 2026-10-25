@echo off
rem Four linked copies of Rave Racer on this PC. Click a window to control that cabinet with the keyboard.
rem Only cabinet 1 has sound. The windows open on top of each other: drag them apart.
cd /d "%~dp0"
start "" RaveRacer-Link.exe roms --window 1 --link 1 --link-cabinets 4
start "" RaveRacer-Link.exe roms --window 1 --link 2 --link-cabinets 4 --mute
start "" RaveRacer-Link.exe roms --window 1 --link 3 --link-cabinets 4 --mute
start "" RaveRacer-Link.exe roms --window 1 --link 4 --link-cabinets 4 --mute
