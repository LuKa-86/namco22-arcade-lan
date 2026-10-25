@echo off
rem Tokyo Wars, 2 players on ONE PC: a split screen, one linked copy of the game per player.
rem Every player has a game controller. Plug the controllers in BEFORE starting.
rem Only player 1 has sound. A controller: Back/Select = coin, Start = start; right-stick click = menu.
rem The copies keep in step with each other, so a slow moment holds every picture briefly instead of dropping a player.
cd /d "%~dp0"
start "" TokyoWars-Link.exe roms --window 1 --link 2 --link-cabinets 2 --party --pad 2 --mute
start "" TokyoWars-Link.exe roms --window 1 --link 1 --link-cabinets 2 --party --pad 1
