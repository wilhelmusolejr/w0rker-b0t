# One-time setup for the optional Telegram monitor. Run through setup-monitor.cmd.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$switch = Join-Path $root 'switch-to-console.cmd'
$monitor = Join-Path $root 'start-monitor.cmd'

# The scheduled task lets /start move the desktop session to the console without a later prompt.
$action = New-ScheduledTaskAction -Execute 'conhost.exe' -Argument "--headless cmd.exe /c `"`"$switch`" --quiet`"" -WorkingDirectory $root
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Minutes 5)
Register-ScheduledTask -TaskPath '\TutorialAutomation\' -TaskName 'SwitchToConsole' -Action $action -Principal $principal -Settings $settings -Force | Out-Null
Write-Host "Scheduled task \TutorialAutomation\SwitchToConsole is ready (runs switch-to-console.cmd as administrator)."

# Start the monitor when this Windows account signs in.
$startup = [Environment]::GetFolderPath('Startup')
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut((Join-Path $startup 'TutorialAutomation monitor.lnk'))
$link.TargetPath = 'conhost.exe'
$link.Arguments = "--headless cmd.exe /c `"`"$monitor`"`""
$link.WorkingDirectory = $root
$link.WindowStyle = 7
$link.Save()
Write-Host "The Telegram monitor now starts at sign-in (shortcut in $startup)."
