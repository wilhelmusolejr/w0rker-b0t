# Compatibility entry point for existing shortcuts.
& (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'setup-monitor.ps1')
