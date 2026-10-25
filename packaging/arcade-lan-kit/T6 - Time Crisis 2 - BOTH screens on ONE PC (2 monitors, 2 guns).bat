@echo off
rem Time Crisis 2's twin cabinet run by ONE PC: the left (red) player full screen on the left monitor, the right (blue) player on the
rem right monitor, linked to each other on this PC. Each screen reads only its own light gun or mouse:
rem   the FIRST gun to shoot becomes the red (left) player's, the NEXT one the blue (right) player's. They are then saved in
rem   "Time Crisis 2\guns.cfg" and stay on their sides (delete that file to choose again).
rem Gun buttons: trigger = shoot, right / middle button = pedal. Off the screen = reload.
rem With one monitor, the two games open as two windows side by side instead.
rem Esc opens the menu on the screen whose window has the focus; File > Exit (or Alt+F4) closes that screen's game.
cd /d "%~dp0Time Crisis 2"
set "TC2_OWN_GUN=1"
set "TC2_LINK=L:23021:127.0.0.1:23022"
set "TC2_CABINET_DIR=screen-left"
set "TC2_MONITOR=1"
start "" TimeCrisis2-Link.exe --window
set "TC2_LINK=R:23022:127.0.0.1:23021"
set "TC2_CABINET_DIR=screen-right"
set "TC2_MONITOR=2"
start "" TimeCrisis2-Link.exe --window
