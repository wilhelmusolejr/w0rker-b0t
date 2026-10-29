@echo off
setlocal
cd /d "%~dp0"
if not exist "%~dp0TutorialAutomation\build\tutorial.exe" (
    echo Build tutorial.exe first. See TutorialAutomation\README.md.
    pause
    exit /b 2
)
rem Usage: start-coding.cmd [PROFILE_ID]   (default: profile 1)
rem Recreates the next reference project from programming_files in the profile's project_path.
rem Run it again after a stop to continue from the last saved checkpoint.
set "profile=%~1"
if "%profile%"=="" set "profile=1"
"%~dp0TutorialAutomation\build\tutorial.exe" --code %profile%
set "code_result=%errorlevel%"
echo.
echo Coding session exited with code %code_result%.
if not "%code_result%"=="0" pause
exit /b %code_result%
