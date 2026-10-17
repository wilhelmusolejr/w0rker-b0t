param(
    [string]$ReferenceDirectory = (Join-Path $PSScriptRoot '..\..\References\scripts'),
    [string]$Destination = 'C:\Users\hinatardp\Desktop\Project for vehicle',
    [string]$OutputPath = (Join-Path $PSScriptRoot 'reference-coding.json'),
    [string]$UrlListPath = (Join-Path $PSScriptRoot '..\..\References\Apollo Vehicle URLs AU and NZ.txt')
)
$ErrorActionPreference = 'Stop'
$referenceRoot = (Resolve-Path -LiteralPath $ReferenceDirectory).Path
$configPath = [IO.Path]::GetFullPath($OutputPath)
$configDirectory = Split-Path -Parent $configPath
$urlListFullPath = (Resolve-Path -LiteralPath $UrlListPath).Path
$configUri = New-Object Uri(($configDirectory.TrimEnd('\') + '\'))
$urlListUri = New-Object Uri($urlListFullPath)
$urlListRelativePath = [Uri]::UnescapeDataString($configUri.MakeRelativeUri($urlListUri).ToString())
$utf8 = New-Object Text.UTF8Encoding($false, $true)
$sha = [Security.Cryptography.SHA256]::Create()
try {
    $files = @(Get-ChildItem -LiteralPath $referenceRoot -File | Where-Object {
        $_.Extension -ceq '.py' -or $_.Extension -ceq '.cjs'
    } | Sort-Object Name | ForEach-Object {
        $bytes = [IO.File]::ReadAllBytes($_.FullName)
        $content = $utf8.GetString($bytes)
        if ($content.IndexOf([char]0) -ge 0) { throw "Embedded NUL in $($_.Name)" }
        if ($content.Length -gt 0 -and $content[0] -eq [char]0xFEFF) {
            throw "UTF-8 BOM is not supported in a reference snapshot: $($_.Name)"
        }
        $hash = ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
        $from = New-Object Uri(($configDirectory.TrimEnd('\') + '\'))
        $sourceUri = New-Object Uri($_.FullName)
        [ordered]@{
            enabled = $true
            source = [Uri]::UnescapeDataString($from.MakeRelativeUri($sourceUri).ToString())
            name = $_.Name
            sha256 = $hash
            content = $content
        }
    })
} finally { $sha.Dispose() }
if ($files.Count -eq 0) { throw 'No .py or .cjs files found.' }
$config = [ordered]@{
    version = 1
    destination = [IO.Path]::GetFullPath($Destination).Replace('\', '/')
    progress_file = '../../logs/reference-coding.state'
    countdown_seconds = 10
    seed = 0
    line_endings = 'normalize_lf'
    typing = [ordered]@{
        character_ms = @(140, 300)
        newline_ms = @(900, 2200)
        pause_every_chars = @(160, 320)
        pause_ms = @(6000, 14000)
    }
    actions = [ordered]@{
        pause_ms = @(1400, 3000)
        mouse_move_ms = @(1000, 1800)
    }
    browser = [ordered]@{
        enabled = $true
        coding_interval_seconds = @(300, 600)
        visit_seconds = @(120, 180)
        url_list_file = $urlListRelativePath
        research_file = '../../References/Vehicle Research Queries.txt'
        launch = 'taskbar'
        topics = @(
            [ordered]@{ query = 'Python data structures documentation site:docs.python.org'; url = 'https://docs.python.org/3/tutorial/datastructures.html' },
            [ordered]@{ query = 'Python functions documentation site:docs.python.org'; url = 'https://docs.python.org/3/tutorial/controlflow.html' },
            [ordered]@{ query = 'Python pathlib documentation site:docs.python.org'; url = 'https://docs.python.org/3/library/pathlib.html' },
            [ordered]@{ query = 'Python json documentation site:docs.python.org'; url = 'https://docs.python.org/3/library/json.html' },
            [ordered]@{ query = 'JavaScript functions documentation MDN'; url = 'https://developer.mozilla.org/en-US/docs/Web/JavaScript/Guide/Functions' },
            [ordered]@{ query = 'JavaScript promises documentation MDN'; url = 'https://developer.mozilla.org/en-US/docs/Web/JavaScript/Guide/Using_promises' },
            [ordered]@{ query = 'JavaScript objects documentation MDN'; url = 'https://developer.mozilla.org/en-US/docs/Web/JavaScript/Guide/Working_with_objects' },
            [ordered]@{ query = 'JavaScript arrays documentation MDN'; url = 'https://developer.mozilla.org/en-US/docs/Web/JavaScript/Guide/Indexed_collections' }
        )
    }
    files = $files
}
# Explicit UTF-8 without BOM, including exact original content inside JSON strings.
$json = $config | ConvertTo-Json -Depth 12
[IO.File]::WriteAllText($configPath, $json + "`n", $utf8)
$characters = ($files | ForEach-Object { $_.content.Length } | Measure-Object -Sum).Sum
Write-Output "Created $configPath : $($files.Count) files, $characters characters."
Write-Output 'This is a snapshot. Regenerate deliberately when reference files change.'
