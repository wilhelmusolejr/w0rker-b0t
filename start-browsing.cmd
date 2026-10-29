@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0TutorialAutomation\build\tutorial.exe" (
    echo Build tutorial.exe first. See TutorialAutomation\README.md.
    pause
    exit /b 2
)
rem Usage: start-browsing.cmd [PROFILE_ID] [MINUTES]   (defaults: profile 1, 10 minutes)
set "profile=%~1"
if "%profile%"=="" set "profile=1"
set "minutes=%~2"
if "%minutes%"=="" set "minutes=10"
"%~dp0TutorialAutomation\build\tutorial.exe" --browse %profile% --minutes %minutes%
set "browse_result=%errorlevel%"
echo.
echo Browsing session exited with code %browse_result%.
if not "%browse_result%"=="0" pause
exit /b %browse_result%
