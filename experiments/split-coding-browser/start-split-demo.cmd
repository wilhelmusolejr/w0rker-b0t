@echo off
setlocal
cd /d "%~dp0"
python split_demo.py %*
set "demo_result=%errorlevel%"
echo.
echo Split demo exited with code %demo_result%.
pause
exit /b %demo_result%
