@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0TutorialAutomation\build\tutorial.exe" (
    echo Build tutorial.exe first. See TutorialAutomation\README.md.
    if not defined TUTORIAL_BACKGROUND pause
    exit /b 2
)
rem Usage: start-session.cmd [PROFILE_ID] [more options]
rem   A whole recorded day: Loggy3 Start, coding and browsing (with other-app breaks) until today's
rem   recorded time in Loggy3 reaches the day's target (10h15-10h45), or Telegram /stop; then the
rem   ending and Loggy3 Stop. Settings: TutorialAutomation\profiles\session.json.
rem   start-session.cmd 1 --target-minutes 25 --coding-minutes 8 --browsing-minutes 4   (short test day)
rem   F8 pause/resume (Loggy3 keeps recording) | F9/F12 stop at once | Telegram /stop ends the day properly.
rem Then run switch-to-console.cmd if you want to close RDP while it runs.
set "profile=%~1"
if "%profile%"=="" set "profile=1"
if not "%~1"=="" shift
"%~dp0TutorialAutomation\build\tutorial.exe" --session %profile% %1 %2 %3 %4 %5 %6 %7 %8 %9
set "session_result=%errorlevel%"
echo.
echo Session exited with code %session_result%.
if not defined TUTORIAL_BACKGROUND pause
exit /b %session_result%
