$ErrorActionPreference = 'Stop'
Get-ChildItem -LiteralPath 'C:\Users\hinatardp\Desktop\Project for vehicle' -File | Select-Object Name,Length,LastWriteTime | Format-Table
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$desktop = [System.Windows.Automation.AutomationElement]::RootElement
$focused = [System.Windows.Automation.AutomationElement]::FocusedElement
if ($focused) { Write-Output ("FOCUSED {0} | {1} | {2}" -f $focused.Current.Name, $focused.Current.ControlType.ProgrammaticName, $focused.Current.ClassName) }
$windows = $desktop.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)
foreach ($window in $windows) {
    $info = $window.Current
    if ($info.Name -notmatch 'Vehicle Coding Demo|Save As|Untitled') { continue }
    Write-Output ("WINDOW {0} | class {1} | PID {2} | HWND {3} | {4}" -f $info.Name, $info.ClassName, $info.ProcessId, $info.NativeWindowHandle, $info.BoundingRectangle)
    if ($true) {
        $children = $window.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
        foreach ($child in $children) {
            $entry = $child.Current
            if ($entry.IsOffscreen) { continue }
            if ($entry.ControlType -ne [System.Windows.Automation.ControlType]::Edit -and
                $entry.ControlType -ne [System.Windows.Automation.ControlType]::Window -and
                $entry.Name -notmatch 'Save|File name|Untitled|New Text|Change Language') { continue }
            $valuePattern = $null
            $valueText = ''
            if ($child.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$valuePattern)) {
                $valueText = $valuePattern.Current.Value
            }
            Write-Output ("  {0} | {1} | ID {2} | value {3} | {4}" -f $entry.ControlType.ProgrammaticName, $entry.Name, $entry.AutomationId, $valueText, $entry.BoundingRectangle)
        }
    }
}
