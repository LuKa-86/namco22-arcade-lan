@echo off
rem Two linked copies of Tokyo Wars on this PC. Click a window to control that cabinet with the keyboard.
cd /d "%~dp0"
start "" TokyoWars-Link.exe roms --window 1 --link 1 --link-cabinets 2
start "" TokyoWars-Link.exe roms --window 1 --link 2 --link-cabinets 2 --mute
