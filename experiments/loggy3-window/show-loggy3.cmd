@echo off
setlocal
cd /d "%~dp0"
python loggy3_window.py show %*
set "demo_result=%errorlevel%"
echo.
pause
exit /b %demo_result%
