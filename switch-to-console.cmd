@echo off
rem Moves this RDP session to the physical console instead of disconnecting it.
rem The desktop stays unlocked and running, so playback continues and RustDesk can show it.
rem Your RDP window closes; reconnect with RDP or RustDesk later.
rem Afterwards the console display is kept at 1920x1080 (see logs\resolution.log).
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
    exit /b
)
for /f %%i in ('powershell -NoProfile -Command "(Get-Process -Id $PID).SessionId"') do set "session_id=%%i"
if not defined session_id (
    echo Could not determine the current session id.
    pause
    exit /b 1
)
"%windir%\System32\tscon.exe" %session_id% /dest:console
if errorlevel 1 (
    echo tscon failed. Run this from inside the RDP session as an administrator.
    pause
    exit /b 1
)
rem The console's own display is 640x480 on this VM. Keep it at the RDP layout from now on:
rem the watcher plugs in its own virtual monitor if needed and re-applies 1920x1080 whenever it
rem drifts, e.g. when RustDesk disconnects. Only one watcher runs at a time.
if not exist "%~dp0logs" mkdir "%~dp0logs"
start "Resolution watch" /min "%~dp0TutorialAutomation\build\set_resolution.exe" 1920 1080 --watch --log "%~dp0logs\resolution.log"
