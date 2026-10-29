@echo off
rem Starts the Telegram monitor (only one runs at a time). setup-bot.cmd runs this at sign-in.
cd /d "%~dp0TutorialAutomation\build"
if not exist monitor.exe (
    echo Build monitor.exe first. See TutorialAutomation\README.md.
    exit /b 2
)
start "Telegram monitor" /min monitor.exe
