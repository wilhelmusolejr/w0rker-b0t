$ErrorActionPreference = 'Stop'

# Register the already installed portable build tools for the current user.
# Existing PATH entries are preserved. No administrator privileges are needed.
$toolDirectories = @(
    (Join-Path $PSScriptRoot '.tools\w64devkit\bin'),
    (Join-Path $PSScriptRoot '.tools\cmake-ready\cmake-3.31.6-windows-x86_64\bin')
)
$codeDirectory = Join-Path $env:LOCALAPPDATA 'Programs\Microsoft VS Code\bin'
if (Test-Path -LiteralPath (Join-Path $codeDirectory 'code.cmd')) {
    $toolDirectories += $codeDirectory
}
foreach ($directory in $toolDirectories) {
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        throw "Tool directory missing: $directory"
    }
}
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$existing = @($userPath -split ';' | Where-Object { $_ })
$updated = @($existing)
foreach ($directory in $toolDirectories) {
    if (-not ($updated | Where-Object { $_.TrimEnd('\') -ieq $directory.TrimEnd('\') })) {
        $updated += $directory
    }
}
$newPath = $updated -join ';'
if ($newPath -ne $userPath) {
    [Environment]::SetEnvironmentVariable('Path', $newPath, 'User')
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TutorialEnvironmentNotification {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr SendMessageTimeout(IntPtr window, uint message,
        UIntPtr wparam, string lparam, uint flags, uint timeout, out UIntPtr result);
}
'@
    $notificationResult = [UIntPtr]::Zero
    [void][TutorialEnvironmentNotification]::SendMessageTimeout(
        [IntPtr]0xffff, 0x001a, [UIntPtr]::Zero, 'Environment', 2, 5000,
        [ref]$notificationResult)
    Write-Host 'Added build tools to the current user PATH.'
} else {
    Write-Host 'Build tools are already registered in the current user PATH.'
}
$env:Path = ($toolDirectories -join ';') + ';' + $env:Path
& gcc.exe --version
if ($LASTEXITCODE -ne 0) { throw 'GCC verification failed' }
& cmake.exe --version
if ($LASTEXITCODE -ne 0) { throw 'CMake verification failed' }
& mingw32-make.exe --version
if ($LASTEXITCODE -ne 0) { throw 'Make verification failed' }
Write-Host 'Setup complete. Restart terminals and VS Code to load the updated PATH.'
