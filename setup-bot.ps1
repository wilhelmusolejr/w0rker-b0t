# One-time setup for a bot machine. Run through setup-bot.cmd (it asks for administrator rights once).
#  1. A scheduled task, "\TutorialAutomation\SwitchToConsole", that runs switch-to-console.cmd with
#     administrator rights. The Telegram monitor starts it on /start, so the session moves to the
#     console (and the resolution watcher starts) without a permission prompt nobody could answer.
#  2. A shortcut in the Startup folder, so the Telegram monitor starts whenever you sign in.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$switch = Join-Path $root 'switch-to-console.cmd'
$monitor = Join-Path $root 'start-monitor.cmd'

$action = New-ScheduledTaskAction -Execute 'cmd.exe' -Argument "/c `"`"$switch`" --quiet`"" -WorkingDirectory $root
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Minutes 5)
Register-ScheduledTask -TaskPath '\TutorialAutomation\' -TaskName 'SwitchToConsole' -Action $action -Principal $principal -Settings $settings -Force | Out-Null
Write-Host "Scheduled task \TutorialAutomation\SwitchToConsole is ready (runs switch-to-console.cmd as administrator)."

$startup = [Environment]::GetFolderPath('Startup')
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut((Join-Path $startup 'TutorialAutomation monitor.lnk'))
$link.TargetPath = $monitor
$link.WorkingDirectory = $root
$link.WindowStyle = 7   # minimized
$link.Save()
Write-Host "The Telegram monitor now starts at sign-in (shortcut in $startup)."
