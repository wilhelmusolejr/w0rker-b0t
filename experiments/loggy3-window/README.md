# L3 show / minimize prototype

This application-control experiment tests taskbar targeting, window state, and
accessibility-based recording controls. Window-only, state-inspection, and actual
recording commands are documented separately below.

Python prototype using the Windows taskbar accessibility information and Windows
window APIs. No package installation, images, OCR, or AI are needed. It shares
the neighboring `taskbar-pointer-demo` input helper and `browser-explorer/motion.py`.

Double-click one of these launchers:

- **start-loggy3-demo.cmd**: click L3's taskbar button, show/maximize its window,
  wait five seconds, then minimize it.
- **show-loggy3.cmd**: show/maximize L3 and leave it visible.
- **minimize-loggy3.cmd**: minimize L3 and leave it running.

There is a three-second countdown. Esc, F9, F12, or Ctrl+C stop the script.
Keep the Windows/RDP desktop connected and unlocked. Keep L3's button visible
on the primary taskbar; pin it there if you want the script to launch a closed app.

From the workspace root:

```powershell
python experiments/loggy3-window/loggy3_window.py
python experiments/loggy3-window/loggy3_window.py show
python experiments/loggy3-window/loggy3_window.py minimize
python experiments/loggy3-window/loggy3_window.py demo --hold 10
python experiments/loggy3-window/loggy3_window.py --inspect
```

The taskbar button is found by its current accessible name. The script
reads its rectangle and rechecks it before a real mouse click. It identifies the
main app window by its title and process (`loggy3-gui.exe` or `loggy3.exe`), then
maximizes it through Windows. If necessary, it first moves the app onto the monitor
containing the pointer when the show action started. It verifies visibility,
maximized state, monitor, and foreground activation before reporting success.

Showing an already active window skips the taskbar click so it is not accidentally
minimized. Minimizing an already hidden/minimized app is also a no-op. The minimize
action uses Windows' window state API, so it does not first bring a hidden app back
onto the screen. The app remains running; no recording controls are pressed.

Multiple matching main windows, a blocking app dialog, or a window that does not
support maximization produce an explicit error. The primary taskbar is supported;
managing separate Windows virtual desktops is outside this prototype's scope.

Validated live on 2026-10-26: clicked the running L3 taskbar button, confirmed
the main window was maximized and active on the current monitor, waited three
seconds, then confirmed it was minimized and still running. The internal
`com.loggy3.recorder-siw` window is excluded from main-window selection.
Launching a completely closed app requires its taskbar shortcut to be pinned;
the live check used the already running app.

Windows API references: [ShowWindow states](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showwindow),
[foreground activation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow).

## Recording status, Start, and Stop

L3 exposes its WebView UI through Windows UI Automation. The visible recording
button has the `btn-record` class, is inside `bottom-bar`, and supports Invoke.
The read-only inspection on 2026-10-26 found an enabled **Start** button and the
idle timer **--:--:--**. Session-history rows are excluded from current-state checks.

Additional launchers:

- **status-loggy3.cmd**: show L3 and read its recording state.
- **start-loggy3-recording.cmd**: start only if the current UI state is stopped.
- **stop-loggy3-recording.cmd**: stop only if the current UI state is recording.

```powershell
python experiments/loggy3-window/loggy3_recording.py status
python experiments/loggy3-window/loggy3_recording.py record --dry-run
python experiments/loggy3-window/loggy3_recording.py record --hover-only --hover-seconds 8
python experiments/loggy3-window/loggy3_recording.py record
python experiments/loggy3-window/loggy3_recording.py stop
python experiments/loggy3-window/loggy3_recording.py record --minimize-after
```

`--hover-only` moves the cursor to the current recording button without clicking.
It forces read-only mode, which blocks Invoke requests. The live preview hovered
over Start for eight seconds and confirmed recording remained stopped.

These controls use UI Automation Invoke directly; the recording action does not
depend on a stored mouse coordinate. Immediately before invoking, the helper
checks that exactly one enabled recording control still has the expected label.
It sends one action and waits for the new UI-reported state. It does not retry
the toggle if confirmation times out. Unknown, disabled, or ambiguous controls
produce a report/error without a recording action.

`record` starts a real L3 recording session, following the app's configured
storage/upload behavior. `--dry-run` only reports the planned action. Esc/F9/F12
stop the automation; they do not automatically stop an ongoing L3 recording.
Use the Stop command or L3 itself for that.

Live validation covered status detection, the Start dry run, and cursor-only
preview. The eight automated
recording-policy tests cover Start/Stop planning, repeated commands, history
exclusion, disabled/unknown controls, and missing Invoke support. A real recording
start/stop transition has not been exercised by these tests. Stop detection expects
the current recording control to expose **Stop** or **Stop recording**; an unfamiliar
label is reported as unknown and is not invoked.

```powershell
python -m unittest discover -s experiments/loggy3-window -p test_loggy3_recording.py
```
