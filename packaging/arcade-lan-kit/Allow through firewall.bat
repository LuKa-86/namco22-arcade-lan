@echo off
rem ==========================================================================
rem  Allow through firewall
rem
rem  Run this ONCE on each cabinet PC (it asks for administrator rights).
rem  It lets every program in this package talk to the other cabinets, so
rem  Windows stops asking "allow or block?" the first time each game starts.
rem
rem  For each .exe in this folder and its sub-folders it removes any old
rem  Windows Firewall rules for that program (including a "block" left by
rem  clicking Cancel on the question) and adds one rule: allow incoming
rem  connections to that program on Private and Public networks. A network
rem  with no internet (a cabinet LAN on its own router) is usually counted
rem  as Public by Windows, which is why Public is included.
rem
rem  Run it again after moving this folder, or after adding a new game.
rem  To remove the rules: Windows Defender Firewall > Advanced settings >
rem  Inbound Rules, the ones named "Polygonizer Arcade LAN - ...".
rem ==========================================================================
setlocal
net session >nul 2>&1
if errorlevel 1 (
    echo Asking Windows for administrator rights ...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
cd /d "%~dp0"
set n=0
for /r %%F in (*.exe) do (
    netsh advfirewall firewall delete rule name=all program="%%~fF" >nul 2>&1
    netsh advfirewall firewall add rule name="Polygonizer Arcade LAN - %%~nF" dir=in action=allow program="%%~fF" enable=yes profile=private,public >nul
    if errorlevel 1 (echo   could not add a rule for %%~nxF) else (echo   allowed: %%~nxF & set /a n+=1 >nul)
)
echo.
echo Done: %n% programs allowed through Windows Firewall.
pause
