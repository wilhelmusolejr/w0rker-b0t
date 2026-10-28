param(
    [Parameter(Mandatory=$true)][long]$WindowHandle,
    [ValidateSet('snapshot','probe','invoke')][string]$Action = 'snapshot',
    [string]$ExpectedName = '',
    [string]$TargetId = '',
    [double]$X = 0,
    [double]$Y = 0
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
try {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    Add-Type -AssemblyName WindowsBase
    $root = [System.Windows.Automation.AutomationElement]::FromHandle([intptr]$WindowHandle)
    if ($root.Current.Name -notmatch '^Loggy3(?:\s|$)') { throw 'The target is not the Loggy3 main window.' }
    $condition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Button)
    $recordControls = @(
        foreach ($button in $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $condition)) {
            $info = $button.Current
            if (-not $info.IsOffscreen -and $info.ClassName -match '(?:^|\s)btn-record(?:\s|$)') { $button }
        }
    )
    $buttons = @(
        foreach ($button in $recordControls) {
            $info = $button.Current
            $invoke = $null
            $supported = $button.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$invoke)
            [ordered]@{
                name=$info.Name; class=$info.ClassName; enabled=$info.IsEnabled; offscreen=$info.IsOffscreen
                invoke_supported=$supported; id=($button.GetRuntimeId() -join '.')
                left=$info.BoundingRectangle.Left; top=$info.BoundingRectangle.Top
                right=$info.BoundingRectangle.Right; bottom=$info.BoundingRectangle.Bottom
            }
        }
    )
    $timers = @()
    if ($recordControls.Count -eq 1) {
        $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
        $parent = $walker.GetParent($recordControls[0])
        for ($depth=0; $null -ne $parent -and $depth -lt 5; $depth++) {
            if ($parent.Current.ClassName -match '(?:^|\s)bottom-bar(?:\s|$)') {
                $texts = $parent.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                    [System.Windows.Automation.PropertyCondition]::new(
                        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
                        [System.Windows.Automation.ControlType]::Text))
                $timers = @($texts | ForEach-Object {
                    if (-not $_.Current.IsOffscreen -and $_.Current.Name -match '^(?:\d{1,3}:\d{2}:\d{2}|--:--:--)$') {
                        $_.Current.Name
                    }
                })
                break
            }
            $parent = $walker.GetParent($parent)
        }
    }
    $invoked = $false
    if ($Action -ne 'snapshot') {
        if ($recordControls.Count -ne 1) { throw 'Expected one current recording control; no action was sent.' }
        $button = $recordControls[0]
        $info = $button.Current
        if (-not $info.IsEnabled -or $info.IsOffscreen -or $info.Name -cne $ExpectedName) {
            throw 'The recording control state changed; no action was sent.'
        }
        if ($ExpectedName -notin @('Start','Start recording','Stop','Stop recording')) {
            throw 'The requested recording control is not recognized.'
        }
        if ($TargetId -and ($button.GetRuntimeId() -join '.') -ne $TargetId) {
            throw 'The recording control identity changed.'
        }
        if ($Action -eq 'probe') {
            $rect = $info.BoundingRectangle
            if ($X -lt $rect.Left -or $X -ge $rect.Right -or $Y -lt $rect.Top -or $Y -ge $rect.Bottom) {
                throw 'The recording button moved away from the hover point.'
            }
            $hit = [System.Windows.Automation.AutomationElement]::FromPoint([System.Windows.Point]::new($X,$Y))
            $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
            $matched = $false
            for ($depth=0; $null -ne $hit -and $depth -lt 30; $depth++) {
                if (($hit.GetRuntimeId() -join '.') -eq ($button.GetRuntimeId() -join '.')) { $matched=$true; break }
                $hit = $walker.GetParent($hit)
            }
            if (-not $matched) { throw 'Another element covers the recording button.' }
        } else {
            $invoke = $null
            if (-not $button.TryGetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern, [ref]$invoke)) {
                throw 'The recording control does not support Invoke.'
            }
            $invoke.Invoke()
            $invoked = $true
        }
    }
    $response = @{ok=$true; buttons=@($buttons); timers=@($timers); invoked=$invoked}
} catch {
    $response = @{ok=$false; error=$_.Exception.Message}
}
[Console]::WriteLine((ConvertTo-Json -InputObject $response -Compress -Depth 5))
