@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0TutorialAutomation\build\tutorial.exe" (
    echo Build tutorial.exe first. See TutorialAutomation\README.md.
    pause
    exit /b 2
)
rem Usage: start-apps.cmd [notepad^|explorer^|calculator^|images^|random] [MINUTES] [PROFILE_ID]
rem Defaults: a random app, 2 minutes, profile 1. Tries one side-app break on its own.
set "app=%~1"
set "minutes=%~2"
set "profile=%~3"
if "%minutes%"=="" set "minutes=2"
if "%profile%"=="" set "profile=1"
if "%app%"=="" set "app=random"
if /i "%app%"=="random" (
    "%~dp0TutorialAutomation\build\tutorial.exe" --apps %profile% --minutes %minutes%
) else (
    "%~dp0TutorialAutomation\build\tutorial.exe" --apps %profile% --app %app% --minutes %minutes%
)
set "apps_result=%errorlevel%"
echo.
echo Side-app test exited with code %apps_result%.
pause
exit /b %apps_result%
