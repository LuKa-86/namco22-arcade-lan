@echo off
rem Rave Racer, 2 players on ONE PC: a split screen, one linked copy of the game per player.
rem Player 1 has the keyboard, the others a game controller each. Plug the controllers in BEFORE starting.
rem Only player 1 has sound. A controller: Back/Select = coin, right trigger = gas, left stick = steer; Start = menu.
rem The copies keep in step with each other, so a slow moment holds every picture briefly instead of dropping a player.
cd /d "%~dp0"
start "" RaveRacer-Link.exe roms --window 1 --link 2 --link-cabinets 2 --party --pad 1 --mute
start "" RaveRacer-Link.exe roms --window 1 --link 1 --link-cabinets 2 --party --pad 0
