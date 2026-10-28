Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
$checks = 0
function Check([bool]$Condition, [string]$Message) {
    $script:checks++
    if (-not $Condition) { throw "FAILED: $Message" }
}
function Rejects([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Check $rejected $Message
}

# Load only pure helper function definitions, never the launcher's main body.
$sourcePath = Join-Path $PSScriptRoot '..\scripts\Start-ExcelReference.ps1'
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($sourcePath, [ref]$tokens, [ref]$errors)
Check ($errors.Count -eq 0) 'Launcher parses on Windows PowerShell'
foreach ($definition in $ast.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] }, $false)) {
    . ([scriptblock]::Create($definition.Extent.Text))
}
$invariant = [Globalization.CultureInfo]::InvariantCulture
$job = Get-Content -Raw (Join-Path $PSScriptRoot '..\scripts\excel-job.json') | ConvertFrom-Json
$manifest = Get-Content -Raw (Join-Path $PSScriptRoot '..\..\References\excel-simulation\manifest.json') | ConvertFrom-Json

function Excel-Value($Value) {
    # Value2 holds every number as a double; blank cells are null.
    if ($null -eq $Value -or ($Value -is [string] -and $Value -ceq '')) { return $null }
    if ($Value -is [string]) { return $Value }
    return [double]$Value
}
# Mirrors Excel: Formula is a string for every cell ('' when blank), even constants.
function Set-MockCell($Sheet, [int]$Row, [int]$Column, $Cell) {
    $Sheet.Grid.SetValue((Cell-Text $Cell), $Row, $Column)
    $Sheet.Values.SetValue((Excel-Value $(if (Is-Formula $Cell) { $Cell.cached } else { $Cell })), $Row, $Column)
}
function Mock-Sheet($Definition) {
    $size = [int[]]@($Definition.rows.Count, $Definition.widths.Count)
    $grid = [Array]::CreateInstance([object], $size, [int[]]@(1, 1))
    $values = [Array]::CreateInstance([object], $size, [int[]]@(1, 1))
    for ($r = 1; $r -le $size[0]; $r++) {
        for ($c = 1; $c -le $size[1]; $c++) { $grid.SetValue('', $r, $c) }
    }
    $sheet = [pscustomobject]@{
        Name = $Definition.name; Grid = $grid; Values = $values
        UsedRange = [pscustomobject]@{ Row = 1; Column = 1; Rows = [pscustomobject]@{ Count = $size[0] }; Columns = [pscustomobject]@{ Count = $size[1] } }
    }
    $sheet | Add-Member -MemberType ScriptMethod -Name Range -Value { param($address) [pscustomobject]@{ Formula = $this.Grid; Value2 = $this.Values } }
    for ($r = 1; $r -le $size[0]; $r++) {
        $row = @($Definition.rows[$r - 1])
        for ($c = 1; $c -le $row.Count; $c++) { Set-MockCell $sheet $r $c $row[$c - 1] }
    }
    return $sheet
}
# Finds the first plain numeric constant, which Excel's Formula reports as text.
function Find-NumberCell($Definition) {
    for ($r = 1; $r -le $Definition.rows.Count; $r++) {
        $row = @($Definition.rows[$r - 1])
        for ($c = 1; $c -le $row.Count; $c++) {
            if ($null -ne $row[$c - 1] -and $row[$c - 1] -isnot [string] -and -not (Is-Formula $row[$c - 1])) { return @($r, $c) }
        }
    }
    return $null
}

Check ((Cell-Text 980.5) -ceq '980.5') 'Invariant numeric entry'
Check ((Cell-Text ('header' | ForEach-Object { $_ })) -ceq 'header') 'Pipeline-wrapped strings are not formulas'
Check ((Cell-Text $manifest.workbooks[0].sheets[0].rows[5][1]) -like '=COUNTA*') 'Formula entry'
Check ((Column-Name 11) -ceq 'K') 'Column addressing'
Check ((Quote-Script 'a"b\c') -ceq '"a\"b\\c"') 'Script escaping'
Rejects { Assert-Range @(300, 140) 'reversed' 1 1000 } 'Reversed timing rejected'
Rejects { Assert-Range @(1.5, 3) 'fractional' 1 1000 } 'Fractional timing rejected'
Check ((Get-ExcelExecutableFromCommand '"C:\Program Files\Microsoft Office\EXCEL.EXE" /automation') -ceq 'C:\Program Files\Microsoft Office\EXCEL.EXE') 'Quoted Microsoft Excel registration'
Check ((Get-ExcelExecutableFromCommand 'C:\Program Files\Microsoft Office\EXCEL.EXE /automation') -ceq 'C:\Program Files\Microsoft Office\EXCEL.EXE') 'Unquoted Microsoft Excel registration'
Rejects { Get-ExcelExecutableFromCommand 'C:\Apps\WPS\et.exe /Automation' } 'WPS Excel alias rejected before app launch'
Rejects { Get-ExcelExecutableFromCommand '' } 'Missing Excel registration rejected'

foreach ($model in $manifest.workbooks) {
    foreach ($definition in $model.sheets) {
        $sheet = Mock-Sheet $definition
        Check (@(Get-MissingRows $sheet $definition $false).Count -eq 0) 'Complete sheet matches'
        Set-MockCell $sheet 1 1 ''
        $pending = @(Get-MissingRows $sheet $definition $true)
        Check ($pending.Count -eq 1 -and $pending[0] -eq 1) 'Missing row resumes at its address'
        Rejects { Get-MissingRows $sheet $definition $false } 'Missing cells fail final verification'
        Set-MockCell $sheet 1 1 'Unexpected user edit'
        Rejects { Get-MissingRows $sheet $definition $true } 'Edited text is preserved and rejected'
        $number = Find-NumberCell $definition
        if ($null -ne $number) {
            $sheet = Mock-Sheet $definition
            Check ($sheet.Grid.GetValue($number[0], $number[1]) -is [string]) 'Mock reports numeric Formula as text, like Excel'
            Check (@(Get-MissingRows $sheet $definition $false).Count -eq 0) 'Numeric constants verify through Value2'
            Set-MockCell $sheet $number[0] $number[1] 12345.67
            Rejects { Get-MissingRows $sheet $definition $true } 'Changed number rejected'
            Set-MockCell $sheet $number[0] $number[1] ([string]$definition.rows[$number[0] - 1][$number[1] - 1])
            Rejects { Get-MissingRows $sheet $definition $true } 'Number stored as text rejected'
        }
        $sheet = Mock-Sheet $definition
        $sheet.UsedRange.Rows.Count++
        Rejects { Get-MissingRows $sheet $definition $true } 'Extra rows rejected'
        $script = New-PlaybackScript $definition (1..$definition.rows.Count) 'Excel Reference Test'
        Check ($script.StartsWith('ACTIVATE_WINDOW "Excel Reference Test"')) 'Unique window target'
        Check ($script.EndsWith("END`n")) 'Script terminator'
        Check ($script.Contains("SHORTCUT CTRL G`nSHORTCUT CTRL A`nTYPE `"A1`"")) 'Go To replaces the previous reference'
        Check (-not $script.Contains('TYPE "A3"')) 'Blank spacer row is not entered'
    }
    $sheets = @($model.sheets | ForEach-Object { Mock-Sheet $_ })
    $collection = [pscustomobject]@{ Items = $sheets; Count = $sheets.Count }
    $collection | Add-Member -MemberType ScriptMethod -Name Item -Value { param($index) $this.Items[$index - 1] }
    $book = [pscustomobject]@{ Worksheets = $collection }
    Assert-Workbook $book $model $false
    Assert-CalculatedValues $book $model
    $sheets[1].Values.SetValue('INCORRECT', 5, 9)
    Rejects { Assert-CalculatedValues $book $model } 'Incorrect computed results rejected'
    Set-MockCell $sheets[1] 5 7 ([pscustomobject]@{ formula = '=1+1'; cached = 2 })
    Rejects { Assert-Workbook $book $model $false } 'Changed formula rejected'
}
# Native stdout must reach the console without contaminating the returned timer.
function Mock-ExcelPlayer { Write-Output 'Simulated player log'; $global:LASTEXITCODE = 0 }
$player = 'Mock-ExcelPlayer'
$activeScript = [IO.Path]::GetTempFileName()
$utf8 = New-Object Text.UTF8Encoding($false)
$countdown = 10
try {
    $elapsed = Invoke-Player "END`n"
    Check ($elapsed -is [double] -and $elapsed -ge 0) 'Player logs do not become the elapsed time'
    Check ($countdown -eq 0) 'Countdown runs once per job'
} finally { [IO.File]::Delete($activeScript) }
Write-Output "Excel playback helpers: $checks checks passed; no Excel opened or input generated."
