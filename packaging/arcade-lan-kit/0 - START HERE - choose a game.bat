@echo off
rem One launcher for everything in this kit: pick a game, then how to play it. It runs the matching numbered launcher in this folder
rem (they all still work on their own too).
setlocal
cd /d "%~dp0"
title Namco link play kit

:main
cls
echo  ==============================================
echo    NAMCO LINK PLAY KIT
echo  ==============================================
echo.
echo    1  Tokyo Wars
echo    2  Rave Racer
echo    3  Time Crisis 2
echo    4  Tools (link lights / recordings on/off, network address, 4-cabinet check)
echo.
echo    Q  Quit
echo.
choice /c 1234Q /n /m "  Press a key: "
if errorlevel 5 goto :eof
if errorlevel 4 goto tools
if errorlevel 3 goto tc2
if errorlevel 2 goto rr
goto tw

:tw
cls
echo  TOKYO WARS
echo  ----------
echo    1  This PC is a cabinet on the network - FULL SCREEN (recommended)
echo    2  This PC is a cabinet on the network - window
echo    3  Try 2 linked cabinets on this PC
echo    4  Try 4 linked cabinets on this PC
echo    5  PARTY: 2 players on this PC - keyboard + 1 controller
echo    6  PARTY: 2 players on this PC - 2 controllers
echo    7  PARTY: 4 players on this PC - keyboard + 3 controllers
echo    8  PARTY: 4 players on this PC - 4 controllers
echo.
echo    B  Back
echo.
choice /c 12345678B /n /m "  Press a key: "
if errorlevel 9 goto main
if errorlevel 8 set "L=P4" & goto run
if errorlevel 7 set "L=P3" & goto run
if errorlevel 6 set "L=P2" & goto run
if errorlevel 5 set "L=P1" & goto run
if errorlevel 4 set "L=2" & goto run
if errorlevel 3 set "L=1" & goto run
if errorlevel 2 set "L=3b" & goto run
set "L=3a" & goto run

:rr
cls
echo  RAVE RACER
echo  ----------
echo    1  This PC is a cabinet on the network - FULL SCREEN (recommended)
echo    2  This PC is a cabinet on the network - window
echo    3  Try 2 linked cabinets on this PC
echo    4  Try 4 linked cabinets on this PC
echo    5  PARTY: 2 players on this PC - keyboard + 1 controller
echo    6  PARTY: 2 players on this PC - 2 controllers
echo    7  PARTY: 4 players on this PC - keyboard + 3 controllers
echo    8  PARTY: 4 players on this PC - 4 controllers
echo.
echo    B  Back
echo.
choice /c 12345678B /n /m "  Press a key: "
if errorlevel 9 goto main
if errorlevel 8 set "L=P8" & goto run
if errorlevel 7 set "L=P7" & goto run
if errorlevel 6 set "L=P6" & goto run
if errorlevel 5 set "L=P5" & goto run
if errorlevel 4 set "L=5" & goto run
if errorlevel 3 set "L=4" & goto run
if errorlevel 2 set "L=6b" & goto run
set "L=6a" & goto run

:tc2
cls
echo  TIME CRISIS 2
echo  -------------
echo    1  BOTH screens from this ONE PC (2 monitors, 2 guns)
echo    2  This PC is the LEFT cabinet (red player) - FULL SCREEN (recommended)
echo    3  This PC is the RIGHT cabinet (blue player) - FULL SCREEN (recommended)
echo    4  This PC is the LEFT cabinet (red player) - window
echo    5  This PC is the RIGHT cabinet (blue player) - window
echo    6  Try both cabinets on this PC (two windows)
echo.
echo    B  Back
echo.
choice /c 123456B /n /m "  Press a key: "
if errorlevel 7 goto main
if errorlevel 6 set "L=T1" & goto run
if errorlevel 5 set "L=T5" & goto run
if errorlevel 4 set "L=T4" & goto run
if errorlevel 3 set "L=T3" & goto run
if errorlevel 2 set "L=T2" & goto run
set "L=T6" & goto run

:tools
set "LIGHTS=?"
set "REC=?"
for /f "usebackq delims=" %%s in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\recordings.ps1" status`) do set "REC=%%s"
for /f "usebackq delims=" %%s in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\link-lights.ps1" status`) do set "LIGHTS=%%s"
cls
echo  TOOLS
echo  -----
echo    1  Show this PC's network address
echo    2  Rave Racer automatic 4-cabinet check (about 2 minutes)
echo    3  Link lights (network activity on screen), all games: now %LIGHTS% - press 3 to switch
echo    4  Session recordings (last game played, to replay a problem), all games: now %REC% - press 4 to switch
echo.
echo    B  Back
echo.
choice /c 1234B /n /m "  Press a key: "
if errorlevel 5 goto main
if errorlevel 4 (powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\recordings.ps1" toggle & echo. & pause & goto tools)
if errorlevel 3 (powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\link-lights.ps1" toggle & echo. & pause & goto tools)
if errorlevel 2 (call "%~dp07 - Rave Racer - automatic 4-cabinet check.bat" & echo. & pause & goto main)
call "%~dp08 - Show this PC's network address.bat"
goto main

:run
for %%f in ("%~dp0%L% - *.bat") do (
    echo.
    echo  Starting: %%~nf
    call "%%~ff"
    goto :eof
)
echo.
echo  Could not find the launcher "%L% - ..." in this folder.
pause
goto main
