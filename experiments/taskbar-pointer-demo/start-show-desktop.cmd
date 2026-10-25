@echo off
setlocal
cd /d "%~dp0"
python taskbar_demo.py --show-desktop %*
set "demo_result=%errorlevel%"
echo.
if not "%demo_result%"=="0" echo Demo exited with code %demo_result%.
pause
exit /b %demo_result%
