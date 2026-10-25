@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (
    python browser_explorer.py --prompt-url
) else (
    python browser_explorer.py %*
)
set "demo_result=%errorlevel%"
echo.
echo Browsing test exited with code %demo_result%.
pause
exit /b %demo_result%
