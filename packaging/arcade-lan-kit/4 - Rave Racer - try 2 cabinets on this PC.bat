@echo off
rem Two linked copies of Rave Racer on this PC. Click a window to control that cabinet with the keyboard.
cd /d "%~dp0"
start "" RaveRacer-Link.exe roms --window 1 --link 1 --link-cabinets 2
start "" RaveRacer-Link.exe roms --window 1 --link 2 --link-cabinets 2 --mute
