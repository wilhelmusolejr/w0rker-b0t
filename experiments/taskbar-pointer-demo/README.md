# Standalone taskbar pointer demo

This UI targeting experiment locates taskbar controls through Windows accessibility
and demonstrates pointer movement, optional app activation, and showing the desktop.
Cursor paths and pauses are presentation settings for reviewing the interaction.

This experiment does not import or change the TutorialAutomation player. It uses
Python's standard library and Windows' built-in PowerShell/.NET accessibility
support. No pip installs, screenshot processing, AI model, or network connection
are required. Windows 10/11 and Python 3 are needed.

Double-click `start-demo.cmd` to hover over Google Chrome after a three-second
countdown. It does not click by default. Keep Chrome pinned and the primary
taskbar visible. Run the script inside the desktop/RDP session being controlled.

From this folder in PowerShell:

```powershell
python taskbar_demo.py --list
python taskbar_demo.py --inspect
python taskbar_demo.py
python taskbar_demo.py --click
python taskbar_demo.py --app "File Explorer"
```

Double-click `start-show-desktop.cmd` to test showing the desktop instead. Each
run randomly chooses Win+D or moving smoothly to the bottom-right Show Desktop
button and left-clicking it (equal probability when both methods are available).
This action exits after showing the desktop; it does not open Chrome. The shortcut
is **Win+D**, not Ctrl+D.

```powershell
python taskbar_demo.py --show-desktop
python taskbar_demo.py --show-desktop keyboard
python taskbar_demo.py --show-desktop mouse
python taskbar_demo.py --show-desktop random --seed 7
```

`--show-desktop` performs the selected shortcut or click without needing `--click`.
It first checks whether the desktop is already active and skips the toggle if so.
After acting, it waits up to five seconds for the desktop to become active. It
never repeats the toggle automatically, since that could restore the windows.
The check uses the foreground shell window, not image recognition.

The mouse route looks up the English accessible name `Show Desktop` and reads its
current rectangle; it does not assume a saved bottom-right coordinate. Enable
**Settings > Personalization > Taskbar > Taskbar behaviors > Select the far corner
of the taskbar to show the desktop**. If the button is unavailable, random mode
falls back to Win+D; explicitly requested mouse mode reports an error. Keyboard
mode works without that taskbar button.

`--list` and `--inspect` are read-only. Hold Escape to stop movement or the
countdown; Ctrl+C also stops the demo. Escape is checked between lookup calls;
an accessibility lookup can take up to 20 seconds before timing out.

## How it works

1. A short-lived hidden PowerShell helper reads the primary taskbar's visible,
   enabled accessibility buttons. It returns names and current screen rectangles
   as JSON. It never presses Win+T or invokes a button.
2. Python matches the requested app name to exactly one button and picks a point
   close to its center. Positions come from the current desktop, not the reference
   screenshot or saved x,y values.
3. Python calls Windows' `SetCursorPos` in small steps along a slightly curved path.
   A smooth easing function accelerates and slows the pointer; brief pauses and
   small variations make the demonstration less mechanical. This is a visual
   approximation, not a validated model of human movement.
4. With `--click`, a second helper lookup confirms the target still contains the
   destination. Python checks that another window does not cover that position
   and that the pointer has not moved, then sends button down/up via `SendInput`.

The demo uses a normal left-click: a running app can activate or show grouped
window previews. It does not force a new Chrome window. It supports the primary
taskbar only, requires a visible icon, and stops on missing/ambiguous matches.
Auto-hidden taskbars must be revealed first. Keep the session connected and
unlocked. RDP display scaling, latency, and frame rate can affect what the viewer
sees. The click path adds a helper startup delay while rechecking the target.

All movement and clicking in this experiment are orchestrated by Python through
Windows API calls. The production player continues using its existing C code.

## Memory and a separate controller computer

Python does not open a collection of apps to recognize an icon. A screenshot,
if used in a later experiment, is image data in memory. A 1920x1080 image at four
bytes per pixel occupies about 7.9 MiB before processing copies. Template matching
adds arrays and temporary buffers but requires no AI model. Process screenshots
one at a time and release old frames to keep the working set bounded.

This demo launches one Python process plus a temporary hidden PowerShell process
per lookup. No image processing happens here. Browser tabs and the applications
being demonstrated may dominate the total memory use. Assess 8 GB vs 16 GB using
the actual app workload; 16 GB gives more headroom. These are not measured RAM
requirements or a guarantee for a particular workload.

A separate controller is feasible:

```text
RDP session: capture screenshot + frame ID + physical screen dimensions
    -> main computer: inspect image / locate target / decide action
    -> return: same frame ID + target name + screen x,y + action
RDP session: validate current target -> move smoothly -> click -> capture result
```

The controller must use the RDP session screenshot, not coordinates from the main
computer's scaled RDP viewer. Resolution, monitor origin, and DPI changes invalidate
old coordinates. Reject old or mismatched replies and recheck before clicking;
wait for the result after each action. Use an authenticated encrypted connection.
The local player must run inside the connected interactive session.

For this taskbar experiment, local accessibility keeps the setup simple. For broader
visual reasoning with a local AI model, putting the model on the main computer
can save RAM/GPU resources on the RDP machine. Offloading adds network and inference
delay and does not itself change the appearance of mouse movement. A future
controller should return a target/action; the local player should generate the
movement smoothly rather than receiving every intermediate cursor position.
