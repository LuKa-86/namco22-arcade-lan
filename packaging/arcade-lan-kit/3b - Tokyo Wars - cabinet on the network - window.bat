@echo off
rem This PC is one Tokyo Wars cabinet of a linked set on the local network, in a window: fine for 2 cabinets, but with 3 or more use the FULL SCREEN launcher (a window can freeze for a moment and drop the link).
rem Set it up in the game: start it on every PC, then Esc > Network > "Find cabinets" and "Use the N found"
rem (or type every address by hand, with This cabinet = Auto). Then close the game and start it again on every PC.
rem Esc opens and closes the menu; File > Exit (or Alt+F4) closes the game. The settings are kept in link.cfg in this folder.
cd /d "%~dp0"
start "" TokyoWars-Link.exe roms --window
