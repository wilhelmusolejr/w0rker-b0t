@echo off
setlocal
cd /d "%~dp0"
python loggy3_recording.py stop %*
set "demo_result=%errorlevel%"
echo.
pause
exit /b %demo_result%
