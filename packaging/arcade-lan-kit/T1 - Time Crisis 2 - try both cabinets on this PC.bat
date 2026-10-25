@echo off
rem Both Time Crisis 2 cabinets on THIS PC, linked to each other: the left (red) player and the right (blue) player, a window each.
rem The mouse aims and shoots in the window it is over (left button = shoot, right button or Z = pedal, 5 = coin, 4 coins a credit).
rem Do not drag a window while playing: a window being dragged stops its game, and the other cabinet carries on without it.
cd /d "%~dp0Time Crisis 2"
set "TC2_LINK=L:23011:127.0.0.1:23012"
set "TC2_CABINET_DIR=try-left"
set "TC2_WINPOS=left"
start "" TimeCrisis2-Link.exe --window
set "TC2_LINK=R:23012:127.0.0.1:23011"
set "TC2_CABINET_DIR=try-right"
set "TC2_WINPOS=right"
start "" TimeCrisis2-Link.exe --window
