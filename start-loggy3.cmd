@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0TutorialAutomation\build\tutorial.exe" (
    echo Build tutorial.exe first. See TutorialAutomation\README.md.
    pause
    exit /b 2
)
rem Usage: start-loggy3.cmd [status^|show^|minimize^|close^|record^|stop^|demo] [more options]
rem   start-loggy3.cmd record --dry-run      plan only, nothing pressed
rem   start-loggy3.cmd record --hover-only   rest the pointer on Start without clicking
rem   start-loggy3.cmd stop --minimize-after
rem Defaults: status, profile 1's mouse hand. status and --dry-run send no input.
set "action=%~1"
if "%action%"=="" set "action=status"
if not "%~1"=="" shift
"%~dp0TutorialAutomation\build\tutorial.exe" --loggy3 %action% %1 %2 %3 %4 %5 %6 %7 %8 %9
set "loggy_result=%errorlevel%"
echo.
echo Loggy3 %action% exited with code %loggy_result%.
pause
exit /b %loggy_result%
