@echo off
setlocal
cd /d "%~dp0"
"%~dp0TutorialAutomation\build\tutorial.exe" --script "%~dp0TutorialAutomation\scripts\vehicle_coding.txt" --countdown 10
set "tutorial_result=%errorlevel%"
echo.
echo Playback exited with code %tutorial_result%.
exit /b %tutorial_result%
