@echo off
rem Four hidden copies of Rave Racer link up on this PC and race each other by themselves for 100 seconds,
rem then each one's log is checked and the result is shown as PASS or FAIL.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "tools\rr-4-cabinet-check.ps1"
echo.
pause
