@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0TutorialAutomation\build\tutorial.exe" (
    echo Build tutorial.exe first. See TutorialAutomation\README.md.
    if not defined TUTORIAL_BACKGROUND pause
    exit /b 2
)
rem Usage: start-session.cmd [PROFILE_ID] [more options]
rem   --duration-minutes 20 runs this session for about 20 active minutes, independent of Loggy3.
rem   Without that option, today's Loggy3 total controls a 10h15-10h45 target. If Loggy3 fails,
rem   a local clock finishes the remaining time. Settings: TutorialAutomation\profiles\session.json.
rem   F8 pause/resume | F9/F12 stop at once | Telegram /stop finishes and ends the session.
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
