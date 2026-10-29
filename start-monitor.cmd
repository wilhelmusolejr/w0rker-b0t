@echo off
rem Starts the Telegram monitor with no window (only one runs at a time). setup-bot.cmd runs this at sign-in.
rem It shows in Task Manager as monitor.exe; its log is logs\monitor.log.
if not exist "%~dp0TutorialAutomation\build\monitor.exe" (
    echo Build monitor.exe first. See TutorialAutomation\README.md.
    exit /b 2
)
powershell -NoProfile -Command "Start-Process -WindowStyle Hidden -WorkingDirectory '%~dp0TutorialAutomation\build' -FilePath '%~dp0TutorialAutomation\build\monitor.exe'"
