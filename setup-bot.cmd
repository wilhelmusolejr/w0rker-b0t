@echo off
rem One-time setup for a bot machine (asks for administrator rights once):
rem  - a scheduled task so Telegram /start can move the session to the console with no prompt
rem  - the Telegram monitor starts automatically when you sign in
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
    exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-bot.ps1"
echo.
pause
