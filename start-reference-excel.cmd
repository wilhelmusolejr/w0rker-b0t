@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0TutorialAutomation\scripts\Start-ExcelReference.ps1" %*
set "excel_result=%errorlevel%"
echo.
echo Excel reference playback exited with code %excel_result%.
if not "%excel_result%"=="0" (
    echo Review the error above. Check files with start-reference-excel.cmd -DryRun.
    pause
)
exit /b %excel_result%
