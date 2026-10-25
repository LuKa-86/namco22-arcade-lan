@echo off
rem Four linked copies of Tokyo Wars on this PC. Click a window to control that cabinet with the keyboard.
rem Only cabinet 1 has sound. The windows open on top of each other: drag them apart.
cd /d "%~dp0"
start "" TokyoWars-Link.exe roms --window 1 --link 1
start "" TokyoWars-Link.exe roms --window 1 --link 2 --mute
start "" TokyoWars-Link.exe roms --window 1 --link 3 --mute
start "" TokyoWars-Link.exe roms --window 1 --link 4 --mute
