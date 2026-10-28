[CmdletBinding()]
param(
    [string]$Config = '',
    [switch]$ValidateOnly,
    [switch]$DryRun,
    # Test-only: run against WPS Spreadsheets, whose Excel automation is incomplete.
    [switch]$AllowWps
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
$invariant = [Globalization.CultureInfo]::InvariantCulture
$utf8 = New-Object Text.UTF8Encoding($false)

function Resolve-JobPath([string]$Base, [string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $Base $Path))
}
function Assert-Range($Value, [string]$Name, [int]$Minimum, [int]$Maximum) {
    if (@($Value).Count -ne 2 -or $Value[0] -isnot [int] -or $Value[1] -isnot [int] -or
        $Value[0] -lt $Minimum -or $Value[1] -gt $Maximum -or $Value[0] -gt $Value[1]) {
        throw "$Name must be an ordered pair of integers in $Minimum..$Maximum."
    }
}
function Assert-Integer($Value, [string]$Name, [int]$Minimum, [int]$Maximum) {
    if ($Value -isnot [int] -or $Value -lt $Minimum -or $Value -gt $Maximum) {
        throw "$Name must be an integer in $Minimum..$Maximum."
    }
}
function Quote-Script([string]$Text) {
    return '"' + $Text.Replace('\', '\\').Replace('"', '\"').Replace("`t", '\t').Replace("`n", '\n').Replace("`r", '\r') + '"'
}
function Column-Name([int]$Number) {
    # This manifest deliberately limits worksheets to A:Z.
    return [string][char](64 + $Number)
}
function Is-Formula($Value) {
    return $null -ne $Value -and $null -ne $Value.PSObject.Properties['formula']
}
function Cell-Text($Value) {
    if ($null -eq $Value) { return '' }
    if (Is-Formula $Value) { return [string]$Value.formula }
    if ($Value -is [string]) { return $Value }
    return [Convert]::ToString($Value, $invariant)
}
function New-PlaybackScript($Sheet, [int[]]$RowNumbers, [string]$Caption) {
    $lines = New-Object 'Collections.Generic.List[string]'
    $lines.Add('ACTIVATE_WINDOW ' + (Quote-Script $Caption))
    $lines.Add(('TYPING_PACE {0} {1} {2} {3} AUTO' -f $job.typing.character_ms[0], $job.typing.character_ms[1], $job.typing.newline_ms[0], $job.typing.newline_ms[1]))
    $lines.Add(('TYPING_BREAKS {0} {1} {2} {3}' -f $job.typing.pause_every_chars[0], $job.typing.pause_every_chars[1], $job.typing.pause_ms[0], $job.typing.pause_ms[1]))
    $lines.Add(('ACTION_PACE {0} {1} {2} {3} AUTO' -f $job.actions.pause_ms[0], $job.actions.pause_ms[1], $job.actions.mouse_move_ms[0], $job.actions.mouse_move_ms[1]))
    foreach ($rowNumber in $RowNumbers) {
        $values = @($Sheet.rows[$rowNumber - 1])
        while ($values.Count -gt 0 -and [string]::IsNullOrEmpty((Cell-Text $values[$values.Count - 1]))) {
            if ($values.Count -eq 1) { $values = @() } else { $values = $values[0..($values.Count - 2)] }
        }
        if ($values.Count -eq 0) { continue }
        $texts = @($values | ForEach-Object { Cell-Text $_ })
        $lines.Add('SHORTCUT CTRL G')
        $lines.Add('SHORTCUT CTRL A')
        $lines.Add('TYPE ' + (Quote-Script ('A' + $rowNumber)))
        $lines.Add('KEY ENTER')
        $lines.Add('TYPE ' + (Quote-Script ($texts -join "`t")))
        $lines.Add('KEY ENTER')
    }
    $lines.Add('END')
    return ($lines -join "`n") + "`n"
}
function Rgb([int]$Red, [int]$Green, [int]$Blue) {
    return $Red + 256 * $Green + 65536 * $Blue
}
function Get-ExcelExecutableFromCommand([string]$Command, [bool]$AllowWps = $false) {
    $expanded = [Environment]::ExpandEnvironmentVariables($Command)
    $match = [regex]::Match($expanded, '^\s*(?:"(?<path>[^"]+)"|(?<path>.+?\.exe))(?:\s|$)', [Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (-not $match.Success) { throw 'Excel has no usable registered executable. Install or repair Microsoft Excel desktop.' }
    $path = $match.Groups['path'].Value
    $program = [IO.Path]::GetFileName($path)
    if ($program -ine 'EXCEL.EXE' -and -not ($AllowWps -and $program -ieq 'et.exe')) {
        $label = if ($program -ieq 'et.exe') { 'WPS Spreadsheets' } else { $program }
        throw "Excel.Application is registered to $label. This playback launcher requires Microsoft Excel desktop. The 10 .xlsx references are ready; -DryRun works without Excel."
    }
    return $path
}
function Get-MicrosoftExcelPath([bool]$AllowWps = $false) {
    $registration = [Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey('Excel.Application\CLSID')
    if ($null -eq $registration) { throw 'Microsoft Excel desktop is not installed. The 10 .xlsx references are ready; -DryRun works without Excel.' }
    try { $classId = [string]$registration.GetValue('') } finally { $registration.Dispose() }
    $server = [Microsoft.Win32.Registry]::ClassesRoot.OpenSubKey("CLSID\$classId\LocalServer32")
    if ($null -eq $server) { throw 'Excel has no registered executable. Install or repair Microsoft Excel desktop.' }
    try { $command = [string]$server.GetValue('') } finally { $server.Dispose() }
    $path = Get-ExcelExecutableFromCommand $command $AllowWps
    if (-not [IO.File]::Exists($path)) { throw 'The registered Microsoft Excel executable is missing. Install or repair Microsoft Excel desktop.' }
    return $path
}
function Format-BlankSheet($Sheet, $Model) {
    $last = Column-Name $Model.widths.Count
    $bottom = $Model.rows.Count
    $area = $Sheet.Range("A1:$last$bottom")
    $area.Font.Name = 'Calibri'
    $area.Font.Size = 11
    $area.Font.Color = Rgb 36 51 71
    $area.WrapText = $true
    $area.VerticalAlignment = -4108
    $area.RowHeight = 31
    for ($c = 1; $c -le $Model.widths.Count; $c++) {
        $letter = Column-Name $c
        $Sheet.Columns.Item("${letter}:${letter}").ColumnWidth = $Model.widths[$c - 1]
    }
    for ($r = 1; $r -le $bottom; $r++) {
        if ($r -ge 5 -and $r % 2 -eq 0) { $Sheet.Range("A${r}:$last$r").Interior.Color = Rgb 240 246 250 }
        $values = @($Model.rows[$r - 1])
        for ($c = 1; $c -le $values.Count; $c++) {
            # Text formatting prevents routes and date labels being auto-converted by Excel.
            if ($values[$c - 1] -is [string]) {
                $Sheet.Cells.Item($r, $c).NumberFormat = '@'
            }
        }
    }
    foreach ($r in @(1, 2)) { [void]$Sheet.Range("A${r}:$last$r").Merge() }
    $title = $Sheet.Range("A1:${last}1")
    $title.Font.Size = 17
    $title.Font.Bold = $true
    $title.Font.Color = Rgb 255 255 255
    $title.Interior.Color = Rgb 24 53 77
    $title.RowHeight = 38
    $notice = $Sheet.Range("A2:${last}2")
    $notice.Font.Size = 10
    $notice.Font.Italic = $true
    $notice.Font.Color = Rgb 89 103 120
    $notice.RowHeight = 38
    $header = $Sheet.Range("A4:${last}4")
    $header.Interior.Color = Rgb 22 125 141
    $header.Font.Color = Rgb 255 255 255
    $header.Font.Bold = $true
    foreach ($c in $Model.currency_columns) {
        $letter = Column-Name $c
        $Sheet.Range("${letter}5:$letter$bottom").NumberFormat = '#,##0.00;[Red]-#,##0.00'
    }
    foreach ($r in $Model.currency_rows) { $Sheet.Range("B$r").NumberFormat = '#,##0.00;[Red]-#,##0.00' }
    [void]$Sheet.Activate()
    $excel.ActiveWindow.DisplayGridlines = $false
    $excel.ActiveWindow.SplitRow = 4
    $excel.ActiveWindow.FreezePanes = $true
    $excel.ActiveWindow.Zoom = $(if ($Model.filter) { 70 } else { 100 })
    [void]$Sheet.Range('A1').Select()
}
function Get-MissingRows($Sheet, $Model, [bool]$AllowMissing) {
    $last = Column-Name $Model.widths.Count
    $bottom = $Model.rows.Count
    $used = $Sheet.UsedRange
    if ($used.Row + $used.Rows.Count - 1 -gt $bottom -or $used.Column + $used.Columns.Count - 1 -gt $Model.widths.Count) {
        throw "Unexpected cells outside $($Model.name)!A1:$last$bottom; the workbook is preserved."
    }
    # Range.Formula returns every constant as a string, so formulas are compared
    # through Formula and constants through their typed Value2.
    $range = $Sheet.Range("A1:$last$bottom")
    $formulas = $range.Formula
    $values = $range.Value2
    for ($r = 1; $r -le $bottom; $r++) {
        $expectedRow = @($Model.rows[$r - 1])
        $missing = $false
        for ($c = 1; $c -le $Model.widths.Count; $c++) {
            $expected = if ($c -le $expectedRow.Count) { $expectedRow[$c - 1] } else { $null }
            $formula = $formulas.GetValue($r, $c)
            $value = $values.GetValue($r, $c)
            $empty = $null -eq $formula -or $formula -ceq ''
            $wanted = Cell-Text $expected
            if ($empty -and $wanted -ne '' -and $AllowMissing) { $missing = $true; continue }
            $match = if ($wanted -eq '') { $empty }
                elseif (Is-Formula $expected) { [string]::Equals([string]$formula, $wanted, [StringComparison]::OrdinalIgnoreCase) }
                elseif ($expected -is [string]) { $value -is [string] -and $value -ceq $expected }
                else { $value -is [double] -and [Math]::Abs($value - [double]$expected) -lt 0.0000001 }
            if (-not $match) { throw "Content conflict at $($Model.name)!$(Column-Name $c)$r; the workbook is preserved. Expected: $wanted; found: $formula" }
        }
        if ($missing) { $r }
    }
}
function Assert-Workbook($Workbook, $Model, [bool]$AllowMissing) {
    if ($Workbook.Worksheets.Count -ne $Model.sheets.Count) { throw "Unexpected worksheet count in $($Model.file)." }
    for ($i = 1; $i -le $Model.sheets.Count; $i++) {
        $sheet = $Workbook.Worksheets.Item($i)
        if ($sheet.Name -cne $Model.sheets[$i - 1].name) { throw "Unexpected worksheet name in $($Model.file)." }
        [void]@(Get-MissingRows $sheet $Model.sheets[$i - 1] $AllowMissing)
    }
}
function Assert-CalculatedValues($Workbook, $Model) {
    for ($i = 1; $i -le $Model.sheets.Count; $i++) {
        $sheet = $Workbook.Worksheets.Item($i)
        $definition = $Model.sheets[$i - 1]
        $last = Column-Name $definition.widths.Count
        $values = $sheet.Range("A1:$last$($definition.rows.Count)").Value2
        for ($r = 1; $r -le $definition.rows.Count; $r++) {
            $row = @($definition.rows[$r - 1])
            for ($c = 1; $c -le $row.Count; $c++) {
                if (-not (Is-Formula $row[$c - 1])) { continue }
                $wanted = $row[$c - 1].cached
                $actual = $values.GetValue($r, $c)
                $match = if ($wanted -is [string]) { [string]$actual -ceq $wanted }
                    else { $actual -is [ValueType] -and [Math]::Abs([double]$actual - [double]$wanted) -lt 0.000001 }
                if (-not $match) { throw "Formula result differs at $($sheet.Name)!$(Column-Name $c)$r." }
            }
        }
    }
}
function Save-State {
    $temporary = $statePath + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    [IO.File]::WriteAllText($temporary, (ConvertTo-Json -InputObject $state -Depth 8), $utf8)
    if ([IO.File]::Exists($statePath)) { [IO.File]::Replace($temporary, $statePath, $null) }
    else { [IO.File]::Move($temporary, $statePath) }
}
function Invoke-Player([string]$Text) {
    [IO.File]::WriteAllText($activeScript, $Text, $utf8)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    & $player --script $activeScript --countdown $script:countdown | Out-Host
    $code = $LASTEXITCODE
    $timer.Stop()
    $script:countdown = 0
    if ($code -ne 0) { throw "Excel playback stopped (player exit $code). Saved checkpoints remain available." }
    return $timer.Elapsed.TotalSeconds
}

$excel = $null
$autoComplete = $null
$workbook = $null
$exitCode = 0
$lock = $null
try {
    if ([string]::IsNullOrWhiteSpace($Config)) { $Config = Join-Path $PSScriptRoot 'excel-job.json' }
    $configPath = [IO.Path]::GetFullPath($Config)
    $base = Split-Path -Parent $configPath
    $job = Get-Content -Raw -LiteralPath $configPath -Encoding UTF8 | ConvertFrom-Json
    if ($job.version -ne 1) { throw 'Unsupported Excel job version.' }
    Assert-Integer $job.countdown_seconds 'countdown_seconds' 0 3600
    Assert-Integer $job.checkpoint_rows 'checkpoint_rows' 1 12
    Assert-Range $job.typing.character_ms 'typing.character_ms' 1 10000
    Assert-Range $job.typing.newline_ms 'typing.newline_ms' 1 60000
    Assert-Range $job.typing.pause_every_chars 'typing.pause_every_chars' 1 100000
    Assert-Range $job.typing.pause_ms 'typing.pause_ms' 0 3600000
    Assert-Range $job.actions.pause_ms 'actions.pause_ms' 0 3600000
    Assert-Range $job.actions.mouse_move_ms 'actions.mouse_move_ms' 0 60000
    Assert-Range $job.browser.work_interval_seconds 'browser.work_interval_seconds' 1 86400
    Assert-Range $job.browser.visit_seconds 'browser.visit_seconds' 1 3600
    if ($job.browser.enabled -isnot [bool]) { throw 'browser.enabled must be true or false.' }
    if ($job.browser.enabled -and @($job.browser.urls).Count -eq 0) { throw 'Enabled browser visits need at least one URL.' }
    foreach ($url in $job.browser.urls) {
        $uri = $null
        if (-not [Uri]::TryCreate($url, [UriKind]::Absolute, [ref]$uri) -or $uri.Scheme -notin @('http', 'https') -or
            $url.Length -gt 219 -or $url -match '[\s"\\]') { throw "Invalid browser URL: $url" }
    }
    $references = Resolve-JobPath $base $job.references
    $destination = Resolve-JobPath $base $job.destination
    if (($destination.TrimEnd('\') + '\').StartsWith($references.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
        ($references.TrimEnd('\') + '\').StartsWith($destination.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Reference and playback directories must be separate.'
    }
    $manifest = Get-Content -Raw -LiteralPath (Join-Path $references 'manifest.json') -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.version -ne 1 -or $manifest.workbooks.Count -lt 10) { throw 'Expected at least ten workbook references.' }
    $seen = @{}
    foreach ($model in $manifest.workbooks) {
        if ($model.file -cnotmatch '^[0-9]{2}_[A-Za-z0-9_]+\.xlsx$' -or $seen.ContainsKey($model.file)) { throw 'Invalid or duplicate workbook filename.' }
        $seen[$model.file] = $true
        if ($model.sha256 -cnotmatch '^[a-f0-9]{64}$' -or
            (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $references $model.file)).Hash.ToLowerInvariant() -cne $model.sha256) {
            throw "Reference checksum differs: $($model.file). Regenerate references and manifest together."
        }
        if ($model.sheets.Count -ne 2 -or $model.sheets[0].name -cne 'Summary' -or $model.sheets[1].name -cne 'Price Checks') { throw 'Expected Summary and Price Checks sheets.' }
        foreach ($sheet in $model.sheets) {
            if ($sheet.widths.Count -lt 2 -or $sheet.widths.Count -gt 26 -or $sheet.rows.Count -lt 5 -or $sheet.rows.Count -gt 1000) { throw 'Unsupported worksheet dimensions.' }
            foreach ($row in $sheet.rows) {
                if (@($row).Count -gt $sheet.widths.Count) { throw 'Worksheet row exceeds its column count.' }
                foreach ($cell in $row) {
                    $text = Cell-Text $cell
                    if ($text.Length -gt 2000 -or $text -match '[\x00-\x1F]') { throw 'Cell contains unsupported control characters or exceeds 2000 characters.' }
                    if ((Is-Formula $cell) -and ($cell.formula -notlike '=*' -or $null -eq $cell.PSObject.Properties['cached'])) { throw 'Formula needs an expression and cached result.' }
                }
            }
        }
    }
    $statePath = Join-Path $destination '.excel-playback-state.json'
    $state = [pscustomobject]@{ version = 1; files = @() }
    if (Test-Path -LiteralPath $statePath) {
        $state = Get-Content -Raw -LiteralPath $statePath -Encoding UTF8 | ConvertFrom-Json
        if ($state.version -ne 1) { throw 'Unsupported Excel progress version.' }
    }
    foreach ($model in $manifest.workbooks) {
        $entry = @($state.files | Where-Object { $_.file -ceq $model.file })
        $stagePath = Join-Path $destination ($model.file -replace '\.xlsx$', '.in-progress.xlsx')
        $finalPath = Join-Path $destination $model.file
        if ($entry.Count -gt 1 -or ($entry.Count -eq 1 -and $entry[0].sha256 -cne $model.sha256)) { throw "Source changed for an existing playback: $($model.file). Use a new destination." }
        if (((Test-Path -LiteralPath $finalPath) -or (Test-Path -LiteralPath $stagePath)) -and $entry.Count -ne 1) { throw "Existing workbook is not owned by this job: $($model.file). Use a new destination." }
        if ((Test-Path -LiteralPath $finalPath) -and (Test-Path -LiteralPath $stagePath)) { throw "Both finished and in-progress copies exist: $($model.file). Resolve the conflict before playback." }
    }
    $sheetCount = 0
    $quoteCount = 0
    foreach ($model in $manifest.workbooks) {
        $sheetCount += $model.sheets.Count
        $quoteCount += $model.sheets[1].rows.Count - 4
    }
    Write-Output ("Excel references: {0} files, {1} sheets, {2} dummy comparisons." -f $manifest.workbooks.Count, $sheetCount, $quoteCount)
    Write-Output "References: $references"
    Write-Output "Playback output: $destination"
    if ($ValidateOnly) { Write-Output 'Valid. No Excel opened and no desktop input generated.'; return }
    $workspace = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $player = Join-Path $PSScriptRoot '..\build\tutorial.exe'
    if (-not (Test-Path -LiteralPath $player)) { throw 'Build TutorialAutomation/build/tutorial.exe first. See TutorialAutomation/README.md.' }
    $plan = Join-Path $workspace '.tmp\excel-playback-plan'
    [void][IO.Directory]::CreateDirectory($plan)
    foreach ($model in $manifest.workbooks) {
        foreach ($sheet in $model.sheets) {
            $path = Join-Path $plan ($model.file.Replace('.xlsx', '') + '-' + $sheet.name.Replace(' ', '-') + '.txt')
            [IO.File]::WriteAllText($path, (New-PlaybackScript $sheet (1..$sheet.rows.Count) 'Excel Reference Playback'), $utf8)
            # Windows PowerShell turns redirected native stderr into a terminating
            # error under 'Stop', which would hide the player's own message.
            $ErrorActionPreference = 'Continue'
            try { $result = (& $player --validate $path 2>&1 | Out-String).Trim() } finally { $ErrorActionPreference = 'Stop' }
            if ($LASTEXITCODE -ne 0) { throw "Player rejected $path : $result" }
        }
    }
    if ($DryRun) {
        Write-Output "Dry-run: all $sheetCount worksheet scripts passed native player validation. Review them in $plan"
        Write-Output 'No Excel opened, desktop input generated, or playback workbooks created.'
        return
    }
    [void](Get-MicrosoftExcelPath $AllowWps.IsPresent)
    if ($AllowWps) { Write-Warning 'WPS test mode: results do not prove Microsoft Excel playback.' }
    [void][IO.Directory]::CreateDirectory($destination)
    # An exclusive file handle prevents two launchers writing to the same job.
    $lock = [IO.File]::Open((Join-Path $destination '.excel-playback.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    # Reload after acquiring the lock, since another launcher may have finished
    # while this one was validating its input scripts.
    if (Test-Path -LiteralPath $statePath) {
        $state = Get-Content -Raw -LiteralPath $statePath -Encoding UTF8 | ConvertFrom-Json
        if ($state.version -ne 1) { throw 'Unsupported Excel progress version.' }
    }
    foreach ($model in $manifest.workbooks) {
        $owned = @($state.files | Where-Object { $_.file -ceq $model.file })
        $final = Join-Path $destination $model.file
        $partial = Join-Path $destination ($model.file -replace '\.xlsx$', '.in-progress.xlsx')
        if ($owned.Count -gt 1 -or ($owned.Count -eq 1 -and $owned[0].sha256 -cne $model.sha256) -or
            (((Test-Path -LiteralPath $final) -or (Test-Path -LiteralPath $partial)) -and $owned.Count -ne 1) -or
            ((Test-Path -LiteralPath $final) -and (Test-Path -LiteralPath $partial))) {
            throw "Playback files changed during startup: $($model.file). Review the destination before restarting."
        }
    }
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $true
    $excel.EnableEvents = $false
    $excel.AutomationSecurity = 3
    $excel.WindowState = -4137
    # AutoComplete would let Tab accept a longer earlier entry from the same
    # column. It is a saved user option, so the original is restored on exit.
    try {
        $autoComplete = $excel.EnableAutoComplete
        $excel.EnableAutoComplete = $false
    } catch {
        if (-not $AllowWps) { throw }
        Write-Warning 'WPS does not expose EnableAutoComplete; typed text may be auto-completed.'
    }
    $language = 9
    try { $language = $excel.LanguageSettings.LanguageID(2) % 1024 } catch { if (-not $AllowWps) { throw } }
    if ($language -ne 9 -or $excel.International(3) -cne '.' -or $excel.International(5) -cne ',') {
        throw 'Playback requires English desktop Excel with a dot decimal separator and comma formula separator.'
    }
    $caption = 'Excel Reference ' + [guid]::NewGuid().ToString('N').Substring(0, 12)
    $excel.Caption = $caption
    $countdown = $job.countdown_seconds
    $activeScript = Join-Path $plan ('active-' + [guid]::NewGuid().ToString('N') + '.txt')
    $workSeconds = 0.0
    $nextVisit = Get-Random -Minimum $job.browser.work_interval_seconds[0] -Maximum ($job.browser.work_interval_seconds[1] + 1)
    $previousUrl = ''
    foreach ($model in $manifest.workbooks) {
        $stagePath = Join-Path $destination ($model.file -replace '\.xlsx$', '.in-progress.xlsx')
        $finalPath = Join-Path $destination $model.file
        if (Test-Path -LiteralPath $finalPath) {
            $workbook = $excel.Workbooks.Open($finalPath, 0, $true)
            Assert-Workbook $workbook $model $false
            [void]$excel.Calculate()
            Assert-CalculatedValues $workbook $model
            [void]$workbook.Close($false)
            $workbook = $null
            Write-Output "SKIP COMPLETE $($model.file)"
            continue
        }
        if (Test-Path -LiteralPath $stagePath) {
            $workbook = $excel.Workbooks.Open($stagePath, 0, $false)
            if ($workbook.ReadOnly) { throw "Close the existing demo workbook before resuming: $stagePath" }
            Assert-Workbook $workbook $model $true
            Write-Output "RESUME $($model.file)"
        } else {
            if (@($state.files | Where-Object { $_.file -ceq $model.file }).Count -eq 0) {
                $state.files = @($state.files) + @([pscustomobject]@{ file = $model.file; sha256 = $model.sha256 })
                Save-State
            }
            $workbook = $excel.Workbooks.Add(-4167)
            $workbook.Worksheets.Item(1).Name = $model.sheets[0].name
            [void]$workbook.Worksheets.Add([Type]::Missing, $workbook.Worksheets.Item(1))
            $workbook.Worksheets.Item(2).Name = $model.sheets[1].name
            for ($i = 1; $i -le $model.sheets.Count; $i++) { Format-BlankSheet $workbook.Worksheets.Item($i) $model.sheets[$i - 1] }
            if (Test-Path -LiteralPath $stagePath) { throw "Output appeared during startup: $stagePath" }
            [void]$workbook.SaveAs($stagePath, 51)
            Write-Output "CREATE $($model.file)"
        }
        # WPS ignores Application.Caption and titles its automation window just
        # "WPS Office"; the player still refuses if more than one window matches.
        if ($AllowWps) { $caption = 'WPS Office' }
        for ($i = 1; $i -le $model.sheets.Count; $i++) {
            $sheet = $workbook.Worksheets.Item($i)
            $definition = $model.sheets[$i - 1]
            $pending = @(Get-MissingRows $sheet $definition $true)
            for ($offset = 0; $offset -lt $pending.Count; $offset += $job.checkpoint_rows) {
                $end = [Math]::Min($offset + $job.checkpoint_rows - 1, $pending.Count - 1)
                $chunk = @($pending[$offset..$end])
                [void]$sheet.Activate()
                $workSeconds += Invoke-Player (New-PlaybackScript $definition $chunk $caption)
                $stillMissing = @(Get-MissingRows $sheet $definition $true)
                foreach ($r in $chunk) { if ($stillMissing -contains $r) { throw "Incomplete row $($sheet.Name)!$r after playback." } }
                [void]$workbook.Save()
                Write-Output ("CHECKPOINT {0} / {1}: verified through row {2}" -f $model.file, $sheet.Name, $chunk[-1])
                if ($job.browser.enabled -and $workSeconds -ge $nextVisit) {
                    $choices = @($job.browser.urls | Where-Object { $_ -cne $previousUrl })
                    if ($choices.Count -eq 0) { $choices = @($job.browser.urls) }
                    $url = $choices[(Get-Random -Minimum 0 -Maximum $choices.Count)]
                    $duration = 1000 * (Get-Random -Minimum $job.browser.visit_seconds[0] -Maximum ($job.browser.visit_seconds[1] + 1))
                    $visit = 'ACTIVATE_WINDOW ' + (Quote-Script $caption) + "`nBROWSER_SCROLL " + (Quote-Script $url) + " $duration 0`nEND`n"
                    [void](Invoke-Player $visit)
                    $previousUrl = $url
                    $workSeconds = 0
                    $nextVisit = Get-Random -Minimum $job.browser.work_interval_seconds[0] -Maximum ($job.browser.work_interval_seconds[1] + 1)
                }
            }
        }
        Assert-Workbook $workbook $model $false
        [void]$excel.Calculate()
        Assert-CalculatedValues $workbook $model
        $checks = $workbook.Worksheets.Item('Price Checks')
        if (-not $checks.AutoFilterMode) { [void]$checks.Range("A4:K$($model.sheets[1].rows.Count)").AutoFilter() }
        $results = $checks.Range("I5:I$($model.sheets[1].rows.Count)")
        [void]$results.FormatConditions.Delete()
        $labels = @('BROKER HIGHER', 'BROKER LOWER', 'EQUAL', 'REVIEW')
        $colors = @((Rgb 252 228 228), (Rgb 226 240 217), (Rgb 227 237 247), (Rgb 255 242 204))
        for ($rule = 0; $rule -lt $labels.Count; $rule++) {
            $condition = $results.FormatConditions.Add(1, 3, ('"' + $labels[$rule] + '"'))
            $condition.Interior.Color = $colors[$rule]
        }
        [void]$workbook.Worksheets.Item(1).Activate()
        [void]$workbook.Save()
        [void]$workbook.Close($false)
        $workbook = $null
        # File.Move refuses to replace an existing final workbook.
        [IO.File]::Move($stagePath, $finalPath)
        Write-Output "COMPLETE $finalPath"
    }
    Write-Output "All $($manifest.workbooks.Count) Excel playback workbooks completed and verified."
} catch {
    Write-Error -Message $_.Exception.Message -ErrorAction Continue
    $exitCode = 2
} finally {
    if ($null -ne $workbook) {
        try { [void]$workbook.Close($false) } catch {
            Write-Warning 'Excel is still editing a cell. Press Esc and close this demo window without saving before restarting; the last saved checkpoint is retained.'
        }
    }
    if ($null -ne $excel) {
        if ($null -ne $autoComplete) { try { $excel.EnableAutoComplete = $autoComplete } catch { Write-Warning 'Could not restore the Excel AutoComplete option.' } }
        try { [void]$excel.Quit() } catch { Write-Warning 'Close the demo Excel window before restarting.' }
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($excel)
    }
    if ($null -ne $lock) { $lock.Dispose() }
}
if ($exitCode -ne 0) { exit $exitCode }
