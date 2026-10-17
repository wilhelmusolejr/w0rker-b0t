@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0TutorialAutomation\build\tutorial.exe" (
    echo Build tutorial.exe first. See TutorialAutomation\README.md.
    pause
    exit /b 2
)
"%~dp0TutorialAutomation\build\tutorial.exe" --config "%~dp0TutorialAutomation\scripts\class-demo.json" %*
set "tutorial_result=%errorlevel%"
echo.
echo Class demo exited with code %tutorial_result%.
if "%tutorial_result%"=="2" pause
exit /b %tutorial_result%
