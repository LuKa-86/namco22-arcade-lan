@echo off
echo This PC's network address. You only need it if you type the addresses by hand:
echo "Find cabinets" on the game's Esc menu ^> Network page fills them in for you.
echo.
ipconfig | findstr /c:"IPv4"
echo.
pause
