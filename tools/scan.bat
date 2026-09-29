@echo off
rem Drop a printed card image (printer_N.png) here to hold it in front of the camera.
rem Double-click without a file to take the card away.
if "%~1"=="" (
    python "%~dp0make_scan.py" --clear
) else (
    python "%~dp0make_scan.py" "%~1"
)
pause
