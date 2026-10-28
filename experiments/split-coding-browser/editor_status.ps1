param([Parameter(Mandatory=$true)][long]$WindowHandle,
    [Parameter(Mandatory=$true)][string]$FileName, [switch]$FocusEditor)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
try {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    $root = [System.Windows.Automation.AutomationElement]::FromHandle([intptr]$WindowHandle)
    $visibleButtons = @($root.FindAll([System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.PropertyCondition]::new(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::Button)) | Where-Object { -not $_.Current.IsOffscreen })
    $edits = @($root.FindAll([System.Windows.Automation.TreeScope]::Descendants,
        [System.Windows.Automation.PropertyCondition]::new(
            [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
            [System.Windows.Automation.ControlType]::Edit)) | Where-Object { -not $_.Current.IsOffscreen })
    if ($FocusEditor) {
        $editors = @($edits | Where-Object { $_.Current.Name -eq $FileName -and $_.Current.IsEnabled })
        if ($editors.Count -eq 1) { $editors[0].SetFocus() }
    }
    $focused = [System.Windows.Automation.AutomationElement]::FocusedElement
    $rootId = $root.GetRuntimeId() -join '.'
    $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
    $ancestor = $focused
    $inside = $false
    for ($depth=0; $null -ne $ancestor -and $depth -lt 60; $depth++) {
        if (($ancestor.GetRuntimeId() -join '.') -eq $rootId) { $inside=$true; break }
        $ancestor = $walker.GetParent($ancestor)
    }
    $info = $focused.Current
    $editable = $inside -and $info.IsEnabled -and $info.HasKeyboardFocus -and
        $info.ControlType -eq [System.Windows.Automation.ControlType]::Edit -and
        $info.Name -eq $FileName
    $response = @{ok=$true; inside=$inside; editable=$editable; name=$info.Name;
        type=$info.ControlType.ProgrammaticName; title=$root.Current.Name
        buttons=@($visibleButtons | Select-Object -First 35 | ForEach-Object { $_.Current.Name })
        edits=@($edits | Select-Object -First 10 | ForEach-Object { $_.Current.Name })}
} catch {
    $response = @{ok=$false; error=$_.Exception.Message}
}
[Console]::WriteLine((ConvertTo-Json -InputObject $response -Compress))
