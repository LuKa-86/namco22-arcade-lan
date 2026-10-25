@echo off
rem Time Crisis 2 on the PC in the RIGHT cabinet (the blue player), linked to the PC in the other cabinet on the network.
rem Switch both cabinets on together: each waits up to 60 s for the other at power-on (S on that screen = play alone).
rem A cabinet switched on later, or restarted, plays alone until both are restarted (as the real boards).
rem Esc opens the menu; File > Exit (or Alt+F4) closes the game. The link settings are in "Time Crisis 2\tc2_link.cfg".
cd /d "%~dp0Time Crisis 2"
set "TC2_SIDE=right"
start "" TimeCrisis2-Link.exe --window --fullscreen
