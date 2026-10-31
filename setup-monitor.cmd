@echo off
rem One-time setup for optional Telegram remote control (administrator rights are requested once).
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
    exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-monitor.ps1"
echo.
pause
