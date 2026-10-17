@echo off
setlocal
cd /d "%~dp0"
"%~dp0TutorialAutomation\build\tutorial.exe" --script "%~dp0TutorialAutomation\scripts\browser_scrolling.txt" --countdown 10
set "tutorial_result=%errorlevel%"
echo.
echo Browser playback exited with code %tutorial_result%.
exit /b %tutorial_result%
