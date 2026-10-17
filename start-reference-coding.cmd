@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0TutorialAutomation\build\tutorial.exe" (
    echo Build tutorial.exe first. See TutorialAutomation\README.md.
    pause
    exit /b 2
)
"%~dp0TutorialAutomation\build\tutorial.exe" --config "%~dp0TutorialAutomation\scripts\job.json" %*
set "tutorial_result=%errorlevel%"
echo.
echo Reference playback exited with code %tutorial_result%.
if "%tutorial_result%"=="2" (
    echo Startup checks failed. Read the error above before restarting.
    echo Saved-file conflicts are preserved; do not delete completed reference files.
    pause
)
exit /b %tutorial_result%
