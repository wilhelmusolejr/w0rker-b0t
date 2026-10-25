param([Parameter(Mandatory=$true)][long]$BrowserWindow)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName WindowsBase
$script:elements = @{}
$script:frame = 0
$walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
$script:roleProperty = [System.Windows.Automation.AutomationProperty]::LookupById(30101)
$script:ariaProperty = [System.Windows.Automation.AutomationProperty]::LookupById(30102)

function Identity($element) { return ($element.GetRuntimeId() -join '.') }
function Property($element, $property) {
    if ($null -eq $property) { return '' }
    $value = $element.GetCurrentPropertyValue($property, $true)
    if ($value -is [string]) { return $value }
    return ''
}
function Role($element, $info) {
    $role = Property $element $script:roleProperty
    if ($role) { return $role }
    # The inbox .NET UIA library may not register newer ARIA property IDs.
    # Use exposed control descriptions and standard legacy/window patterns too.
    $localized = ([string]$info.LocalizedControlType).ToLowerInvariant()
    if ($localized -match '^heading(?:\s|$)') { return 'heading' }
    if ($localized -match '^(dialog|alert dialog|modal dialog)$') { return 'dialog' }
    if ($localized -match '^navigation(?: landmark)?$') { return 'navigation' }
    if ($localized -match '^form(?: landmark)?$') { return 'form' }
    if ($info.ControlType -eq [System.Windows.Automation.ControlType]::Menu) { return 'menu' }
    if ($info.ControlType -eq [System.Windows.Automation.ControlType]::MenuBar) { return 'menubar' }
    if ($info.ControlType -eq [System.Windows.Automation.ControlType]::Tab) { return 'tablist' }
    if ($info.ControlType -eq [System.Windows.Automation.ControlType]::Tree) { return 'tree' }
    if ($info.ControlType -eq [System.Windows.Automation.ControlType]::Window -or
        $info.ControlType -eq [System.Windows.Automation.ControlType]::Pane -or
        $info.ControlType -eq [System.Windows.Automation.ControlType]::Custom) {
        $pattern = $null
        if ($element.TryGetCurrentPattern([System.Windows.Automation.WindowPattern]::Pattern, [ref]$pattern) -and $pattern.Current.IsModal) {
            return 'dialog'
        }
        $pattern = $null
        if ($element.TryGetCurrentPattern([System.Windows.Automation.LegacyIAccessiblePattern]::Pattern, [ref]$pattern)) {
            if ($pattern.Current.Role -eq 18) { return 'dialog' }
            if ($pattern.Current.Role -eq 11) { return 'menu' }
        }
    }
    return ''
}
function Describe($element) {
    $info = $element.Current
    $rect = $info.BoundingRectangle
    $value = ''
    $expanded = ''
    $selected = $false
    $pattern = $null
    if ($element.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$pattern)) {
        $value = $pattern.Current.Value
    }
    if (-not $value -and ($info.ControlType -eq [System.Windows.Automation.ControlType]::Hyperlink -or
        $info.ControlType -eq [System.Windows.Automation.ControlType]::Document)) {
        $pattern = $null
        if ($element.TryGetCurrentPattern([System.Windows.Automation.LegacyIAccessiblePattern]::Pattern, [ref]$pattern)) {
            $value = $pattern.Current.Value
        }
    }
    $pattern = $null
    if ($element.TryGetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern, [ref]$pattern)) {
        $expanded = [string]$pattern.Current.ExpandCollapseState
    }
    $pattern = $null
    if ($element.TryGetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern, [ref]$pattern)) {
        $selected = $pattern.Current.IsSelected
    }
    return [ordered]@{
        id = (Identity $element); name = $info.Name; type = $info.ControlType.ProgrammaticName.Replace('ControlType.', '')
        role = (Role $element $info); aria = (Property $element $script:ariaProperty)
        localized_type = $info.LocalizedControlType
        value = $value; expanded = $expanded; selected = $selected
        enabled = $info.IsEnabled; offscreen = $info.IsOffscreen
        left = $rect.Left; top = $rect.Top; right = $rect.Right; bottom = $rect.Bottom
        scope = ''; navigation = $false; in_form = $false
    }
}
function Context($element, $depth = 0) {
    $identity = Identity $element
    if ($script:contexts.ContainsKey($identity)) { return $script:contexts[$identity] }
    $context = @{scope=''; navigation=$false; in_form=$false}
    if ($depth -lt 40 -and $identity -ne $script:documentId) {
        $parent = $walker.GetParent($element)
        if ($null -ne $parent) {
            $inherited = Context $parent ($depth + 1)
            $context = @{scope=$inherited.scope; navigation=$inherited.navigation; in_form=$inherited.in_form}
        }
    }
    $role = if ($script:roles.ContainsKey($identity)) { $script:roles[$identity] } else { Role $element $element.Current }
    if ($script:overlayIds.ContainsKey($identity)) { $context.scope = $identity }
    if ($role -eq 'form') { $context.in_form = $true }
    if ($role -in @('navigation','menubar','tablist','tree')) { $context.navigation = $true }
    $script:contexts[$identity] = $context
    return $context
}
function Snapshot($request) {
    $root = [System.Windows.Automation.AutomationElement]::FromHandle([intptr]$BrowserWindow)
    $condition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::Document)
    $documents = [System.Collections.Generic.List[object]]::new()
    foreach ($item in $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $condition)) {
        $documents.Add($item)
    }
    # Chromium can expose the renderer as a separate accessibility fragment
    # rather than attaching it beneath the browser's toolbar tree.
    foreach ($rendererHandle in $request.renderer_windows) {
        try {
            $renderer = [System.Windows.Automation.AutomationElement]::FromHandle([intptr][long]$rendererHandle)
            if ($renderer.Current.ControlType -eq [System.Windows.Automation.ControlType]::Document) {
                $documents.Add($renderer)
            }
            foreach ($item in $renderer.FindAll([System.Windows.Automation.TreeScope]::Descendants, $condition)) {
                $documents.Add($item)
            }
        } catch [System.Windows.Automation.ElementNotAvailableException] { continue }
    }
    $document = $null
    $area = 0
    foreach ($item in $documents) {
        $info = $item.Current
        $rect = $info.BoundingRectangle
        if (-not $info.IsOffscreen -and $rect.Width * $rect.Height -gt $area) {
            $document = $item; $area = $rect.Width * $rect.Height
        }
    }
    if ($null -eq $document) {
        throw ('The browser has no visible accessible web document yet (documents=' +
            $documents.Count + ', window=' + $root.Current.Name + ').')
    }
    $script:elements = @{}
    $script:frame += 1
    $description = Describe $document
    $url = $description.value
    $urlSource = 'document'
    if ($url -notmatch '^https?://') {
        $edits = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants,
            [System.Windows.Automation.PropertyCondition]::new(
                [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
                [System.Windows.Automation.ControlType]::Edit))
        foreach ($edit in $edits) {
            if ($edit.Current.Name -match 'Address and search bar|Search or enter address') {
                $url = (Describe $edit).value
                $urlSource = 'address-bar'
                break
            }
        }
    }
    if ($url -and $url -notmatch '^https?://' -and
        $url -notmatch '^(about|chrome|edge|file|javascript|data|blob|view-source):') {
        $scheme = if ($request.expected_scheme -in @('http','https')) { $request.expected_scheme } else { 'https' }
        $url = $scheme + '://' + $url
        $urlSource = 'formatted-address-bar'
    }
    $controls = [System.Collections.Generic.List[object]]::new()
    $overlays = [System.Collections.Generic.List[object]]::new()
    $headings = [System.Collections.Generic.List[string]]::new()
    $script:roles = @{}
    $script:contexts = @{}
    $script:overlayIds = @{}
    $script:documentId = $description.id
    $visibleCondition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::IsOffscreenProperty, $false)
    $all = $document.FindAll([System.Windows.Automation.TreeScope]::Descendants, $visibleCondition)
    $limit = [Math]::Min($all.Count, 4500)
    for ($index = 0; $index -lt $limit; $index++) {
        try {
            $element = $all[$index]
            $info = $element.Current
            if ($info.IsOffscreen -or -not $info.IsEnabled) { continue }
            $role = Role $element $info
            $script:roles[(Identity $element)] = $role
            if ($role -match '(^|\s)heading($|\s)' -and $headings.Count -lt 30) {
                $headings.Add($info.Name)
            }
            if ($role -match '^(dialog|alertdialog|menu)$') {
                $overlay = Describe $element
                $overlays.Add($overlay)
                $script:elements[$overlay.id] = $element
            }
            $type = $info.ControlType.ProgrammaticName
            if ($type -notmatch '^ControlType\.(Hyperlink|Button|TabItem|MenuItem|TreeItem)$') { continue }
            if ($controls.Count -ge 250) { continue }
            $entry = Describe $element
            if (-not $entry.name -or $entry.right - $entry.left -lt 4 -or $entry.bottom - $entry.top -lt 4) { continue }
            if ($entry.right -le $description.left -or $entry.left -ge $description.right -or
                $entry.bottom -le $description.top -or $entry.top -ge $description.bottom) { continue }
            $controls.Add($entry)
            $script:elements[$entry.id] = $element
        } catch [System.Windows.Automation.ElementNotAvailableException] { continue }
    }
    # Cache ancestry once per frame to find navigation/form context and the
    # nearest overlay without repeatedly traversing the same parent nodes.
    foreach ($overlay in $overlays) { $script:overlayIds[$overlay.id] = $true }
    foreach ($entry in $controls) {
        $context = Context $script:elements[$entry.id]
        $entry.scope = $context.scope
        $entry.navigation = $context.navigation
        $entry.in_form = $context.in_form
    }
    $tabCondition = [System.Windows.Automation.PropertyCondition]::new(
        [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
        [System.Windows.Automation.ControlType]::TabItem)
    $tabs = @(
        foreach ($tab in $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $tabCondition)) {
            $entry = Describe $tab
            if (-not $entry.offscreen -and $entry.bottom -le $description.top) {
                $script:elements[$entry.id] = $tab
                $entry
            }
        }
    )
    $scroll = -1
    $pattern = $null
    if ($document.TryGetCurrentPattern([System.Windows.Automation.ScrollPattern]::Pattern, [ref]$pattern)) {
        $scroll = $pattern.Current.VerticalScrollPercent
    }
    $visibleText = ''
    $pattern = $null
    if ($document.TryGetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern, [ref]$pattern)) {
        $visibleText = (@($pattern.GetVisibleRanges() | Select-Object -First 3 |
            ForEach-Object { $_.GetText(800) }) -join ' ')
    }
    return @{
        frame = $script:frame; url = $url; url_source = $urlSource; title = $description.name; document = $description
        controls = @($controls.ToArray()); overlays = @($overlays.ToArray())
        headings = @($headings.ToArray()); tabs = @($tabs); scroll = $scroll
        visible_text = $visibleText; truncated = ($all.Count -gt $limit)
    }
}
function Probe($request) {
    if ($request.frame -ne $script:frame -or -not $script:elements.ContainsKey([string]$request.id)) {
        throw 'The accessibility snapshot is stale.'
    }
    $element = $script:elements[[string]$request.id]
    $entry = Describe $element
    if ($entry.offscreen -or -not $entry.enabled) { throw 'The target is no longer visible and enabled.' }
    if ($request.name -ne $entry.name) { throw 'The target name changed.' }
    if ($null -ne $request.x) {
        $x = [double]$request.x; $y = [double]$request.y
        if ($x -lt $entry.left -or $x -ge $entry.right -or $y -lt $entry.top -or $y -ge $entry.bottom) {
            throw 'The target moved away from the requested point.'
        }
        $hit = [System.Windows.Automation.AutomationElement]::FromPoint([System.Windows.Point]::new($x, $y))
        $matched = $false
        for ($depth = 0; $null -ne $hit -and $depth -lt 40; $depth++) {
            if ((Identity $hit) -eq $entry.id) { $matched = $true; break }
            $hit = $walker.GetParent($hit)
        }
        if (-not $matched) { throw 'Another element covers the requested target.' }
    }
    return $entry
}
while ($null -ne ($line = [Console]::ReadLine())) {
    try {
        $request = $line | ConvertFrom-Json
        if ($request.command -eq 'snapshot') { $data = Snapshot $request }
        elseif ($request.command -eq 'probe') { $data = Probe $request }
        elseif ($request.command -eq 'quit') { break }
        else { throw 'Unknown accessibility request.' }
        $response = @{ok=$true; data=$data}
    } catch {
        $response = @{ok=$false; error=$_.Exception.Message}
    }
    [Console]::WriteLine((ConvertTo-Json -InputObject $response -Compress -Depth 9))
}
