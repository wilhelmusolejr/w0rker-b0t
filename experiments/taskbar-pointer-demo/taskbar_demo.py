"""Standalone Windows taskbar pointer demo. Python standard library only."""

import argparse
import base64
import ctypes
from ctypes import wintypes
import json
import math
import os
import random
import subprocess
import sys
import time


# This helper only reads accessibility metadata; it sends no keys or clicks.
# Query the primary taskbar specifically, rather than searching the whole desktop.
TASKBAR_QUERY = r"""
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
try {
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$root = [System.Windows.Automation.AutomationElement]::RootElement
$condition = [System.Windows.Automation.PropertyCondition]::new(
    [System.Windows.Automation.AutomationElement]::ClassNameProperty, 'Shell_TrayWnd')
$taskbar = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $condition)
if ($null -eq $taskbar) { throw 'Primary taskbar unavailable in this desktop session.' }
$buttonCondition = [System.Windows.Automation.PropertyCondition]::new(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Button)
$buttons = $taskbar.FindAll(
    [System.Windows.Automation.TreeScope]::Descendants, $buttonCondition)
$result = @(
    foreach ($button in $buttons) {
        try {
            $info = $button.Current
            $rect = $info.BoundingRectangle
            if ($info.IsOffscreen -or -not $info.IsEnabled -or
                -not $info.Name -or $rect.IsEmpty -or
                $rect.Width -lt 4 -or $rect.Height -lt 4) { continue }
            [pscustomobject]@{
                name = $info.Name
                left = $rect.Left
                top = $rect.Top
                right = $rect.Right
                bottom = $rect.Bottom
                taskbar = $taskbar.Current.NativeWindowHandle
            }
        } catch [System.Windows.Automation.ElementNotAvailableException] { continue }
    }
)
ConvertTo-Json -InputObject $result -Compress -Depth 3
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
"""


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [
        ("dx", wintypes.LONG), ("dy", wintypes.LONG),
        ("mouseData", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD), ("dwExtraInfo", ctypes.c_size_t),
    ]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [
        ("wVk", wintypes.WORD), ("wScan", wintypes.WORD),
        ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD),
        ("dwExtraInfo", ctypes.c_size_t),
    ]


class HARDWAREINPUT(ctypes.Structure):
    _fields_ = [
        ("uMsg", wintypes.DWORD), ("wParamL", wintypes.WORD),
        ("wParamH", wintypes.WORD),
    ]


class INPUTUNION(ctypes.Union):
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT), ("hi", HARDWAREINPUT)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("data", INPUTUNION)]


def windows_api():
    api = ctypes.WinDLL("user32", use_last_error=True)
    signatures = {
        "SetProcessDpiAwarenessContext": ([wintypes.HANDLE], wintypes.BOOL),
        "GetCursorPos": ([ctypes.POINTER(wintypes.POINT)], wintypes.BOOL),
        "SetCursorPos": ([ctypes.c_int, ctypes.c_int], wintypes.BOOL),
        "GetAsyncKeyState": ([ctypes.c_int], wintypes.SHORT),
        "GetForegroundWindow": ([], wintypes.HWND),
        "GetShellWindow": ([], wintypes.HWND),
        "GetClassNameW": ([wintypes.HWND, wintypes.LPWSTR, ctypes.c_int], ctypes.c_int),
        "WindowFromPoint": ([wintypes.POINT], wintypes.HWND),
        "GetAncestor": ([wintypes.HWND, wintypes.UINT], wintypes.HWND),
        "MonitorFromPoint": ([wintypes.POINT, wintypes.DWORD], wintypes.HANDLE),
        "SendInput": ([wintypes.UINT, ctypes.POINTER(INPUT), ctypes.c_int], wintypes.UINT),
    }
    for name, (args, result) in signatures.items():
        function = getattr(api, name)
        function.argtypes, function.restype = args, result
    # UI Automation reports physical pixels. Use the same coordinate space.
    if not api.SetProcessDpiAwarenessContext(wintypes.HANDLE(-4)):
        raise ctypes.WinError(ctypes.get_last_error())
    return api


def query_buttons():
    encoded = base64.b64encode(TASKBAR_QUERY.encode("utf-16-le")).decode("ascii")
    system_root = os.environ.get("SystemRoot", r"C:\Windows")
    powershell = os.path.join(
        system_root, "System32", "WindowsPowerShell", "v1.0", "powershell.exe")
    result = subprocess.run(
        [powershell, "-NoLogo", "-NoProfile", "-NonInteractive", "-STA",
         "-EncodedCommand", encoded],
        capture_output=True, encoding="utf-8-sig", errors="replace", timeout=20,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    if result.returncode:
        raise RuntimeError("Taskbar lookup failed: " + result.stderr.strip())
    return json.loads(result.stdout.strip())


def find_button(buttons, name):
    matches = [b for b in buttons if name.casefold() in b["name"].casefold()]
    if len(matches) != 1:
        raise RuntimeError(
            f"Expected one visible taskbar button containing {name!r}; found "
            f"{len(matches)}. Use --list to see available names. "
            "Pin the app and keep the primary taskbar visible.")
    return matches[0]


def cancelled(api):
    if api.GetAsyncKeyState(0x1B) & 0x8000:
        raise KeyboardInterrupt


def pause(api, seconds):
    deadline = time.perf_counter() + seconds
    while time.perf_counter() < deadline:
        cancelled(api)
        time.sleep(min(0.02, max(0, deadline - time.perf_counter())))


def pointer_position(api):
    point = wintypes.POINT()
    if not api.GetCursorPos(ctypes.byref(point)):
        raise ctypes.WinError(ctypes.get_last_error())
    return point.x, point.y


def inside(button, x, y):
    return button["left"] <= x < button["right"] and button["top"] <= y < button["bottom"]


def verify_destination(api, button, x, y):
    if not inside(button, x, y):
        raise RuntimeError("The target moved. Run the demo again to locate it.")
    point = wintypes.POINT(x, y)
    hit = api.WindowFromPoint(point)
    if not hit or api.GetAncestor(hit, 2) != button["taskbar"]:
        raise RuntimeError("Another window covers the taskbar target; stopping.")


def move_pointer(api, destination, duration, rng):
    start_x, start_y = pointer_position(api)
    dx, dy = destination[0] - start_x, destination[1] - start_y
    # A small sideways bend, capped for longer journeys; no wandering or overshoot.
    distance = math.hypot(dx, dy)
    bend = rng.uniform(-1, 1) * min(35, distance * 0.06)
    normal_x, normal_y = (-dy / distance, dx / distance) if distance else (0, 0)
    started = time.perf_counter()
    while True:
        cancelled(api)
        t = min(1, (time.perf_counter() - started) / duration)
        eased = t * t * t * (10 + t * (-15 + 6 * t))
        offset = 4 * eased * (1 - eased) * bend
        x = round(start_x + dx * eased + normal_x * offset)
        y = round(start_y + dy * eased + normal_y * offset)
        if not api.MonitorFromPoint(wintypes.POINT(x, y), 0):
            raise RuntimeError("Movement would leave the connected monitors; stopping.")
        if not api.SetCursorPos(x, y):
            raise ctypes.WinError(ctypes.get_last_error())
        if t == 1:
            break
        time.sleep(0.01)


def send_button(api, flag):
    event = INPUT(type=0, data=INPUTUNION(mi=MOUSEINPUT(dwFlags=flag)))
    if api.SendInput(1, ctypes.byref(event), ctypes.sizeof(INPUT)) != 1:
        raise RuntimeError("Windows did not accept the mouse button event.")


def left_click(api, rng):
    cancelled(api)
    send_button(api, 0x0002)
    try:
        pause(api, rng.uniform(0.06, 0.11))
    finally:
        send_button(api, 0x0004)


def desktop_visible(api):
    foreground = api.GetForegroundWindow()
    if not foreground:
        return False
    if foreground == api.GetShellWindow():
        return True
    name = ctypes.create_unicode_buffer(128)
    return bool(api.GetClassNameW(foreground, name, len(name))) and name.value in ("Progman", "WorkerW")


def send_key(api, key, up=False):
    flags = (0x0002 if up else 0) | (0x0001 if key == 0x5B else 0)
    event = INPUT(type=1, data=INPUTUNION(ki=KEYBDINPUT(wVk=key, dwFlags=flags)))
    if api.SendInput(1, ctypes.byref(event), ctypes.sizeof(INPUT)) != 1:
        raise RuntimeError("Windows did not accept the keyboard event.")


def press_win_d(api, rng):
    cancelled(api)
    send_key(api, 0x5B)
    try:
        pause(api, rng.uniform(0.05, 0.10))
        send_key(api, ord("D"))
        try:
            pause(api, rng.uniform(0.06, 0.11))
        finally:
            send_key(api, ord("D"), up=True)
    finally:
        send_key(api, 0x5B, up=True)


def wait_for_desktop(api):
    deadline = time.perf_counter() + 5
    while time.perf_counter() < deadline:
        cancelled(api)
        if desktop_visible(api):
            return
        time.sleep(0.05)
    raise RuntimeError("Desktop did not become active within five seconds; no toggle will be retried.")


def show_desktop(api, method, duration, rng):
    if desktop_visible(api):
        print("Desktop is already showing; no shortcut or click needed.")
        return
    chosen = rng.choice(("keyboard", "mouse")) if method == "random" else method
    print(f"Show desktop method: {chosen}", flush=True)
    if chosen == "mouse":
        buttons = query_buttons()
        matches = [b for b in buttons if b["name"].casefold() == "show desktop"]
        if len(matches) != 1:
            if method != "random":
                raise RuntimeError(
                    "Expected one visible 'Show Desktop' button. Enable 'Select the far corner "
                    "of the taskbar to show the desktop' in Taskbar behaviors, or use keyboard mode. "
                    "This demo matches the English button name.")
            print("Show Desktop button unavailable; using Win+D instead.", flush=True)
            chosen = "keyboard"
        else:
            button = matches[0]
            x = round((button["left"] + button["right"]) / 2)
            y = round((button["top"] + button["bottom"]) / 2)
            print(f"Moving to Show Desktop at ({x}, {y}).", flush=True)
            verify_destination(api, button, x, y)
            pause(api, rng.uniform(0.25, 0.65))
            move_pointer(api, (x, y), duration or rng.uniform(1.0, 1.7), rng)
            pause(api, rng.uniform(0.18, 0.32))
            # UI layout may change during the movement or the lookup.
            refreshed = find_button(query_buttons(), "Show Desktop")
            cancelled(api)
            verify_destination(api, refreshed, x, y)
            if pointer_position(api) != (x, y):
                raise RuntimeError("The mouse moved after arrival; stopping before clicking.")
            if desktop_visible(api):
                print("Desktop became active; skipping the click to avoid restoring windows.")
                return
            left_click(api, rng)
    if chosen == "keyboard":
        pause(api, rng.uniform(0.25, 0.65))
        if desktop_visible(api):
            print("Desktop became active; skipping Win+D to avoid restoring windows.")
            return
        press_win_d(api, rng)
    wait_for_desktop(api)
    # The shell can take focus before its minimize animation has finished.
    pause(api, rng.uniform(0.4, 0.6))
    print("Desktop is now showing.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", default="Google Chrome", help="part of the accessible app name")
    parser.add_argument("--list", action="store_true", help="list primary taskbar buttons; no movement")
    parser.add_argument("--inspect", action="store_true", help="print the target location; no movement")
    parser.add_argument("--click", action="store_true", help="left-click after moving (default: hover only)")
    parser.add_argument("--show-desktop", nargs="?", const="random",
                        choices=("random", "keyboard", "mouse"),
                        help="show the desktop and exit; default: randomly choose Win+D or a mouse click")
    parser.add_argument("--delay", type=float, default=3, help="countdown in seconds (default: 3)")
    parser.add_argument("--duration", type=float, help="movement duration; default: random 1.0-1.7 seconds")
    parser.add_argument("--seed", type=int, help="repeat the timing/path random choices")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This demo requires Windows 10/11 and an active, unlocked desktop.")
    if not math.isfinite(args.delay) or not 0 <= args.delay <= 60:
        parser.error("--delay must be between 0 and 60 seconds")
    if args.duration is not None and (not math.isfinite(args.duration) or not 0.2 <= args.duration <= 10):
        parser.error("--duration must be between 0.2 and 10 seconds")
    if args.click and (args.list or args.inspect):
        parser.error("--click cannot be combined with --list or --inspect")
    if args.show_desktop and (args.click or args.list or args.inspect):
        parser.error("--show-desktop cannot be combined with --click, --list, or --inspect")
    api = windows_api()
    if args.list:
        print(json.dumps(query_buttons(), indent=2, ensure_ascii=True))
        return
    rng = random.Random(args.seed)
    if args.show_desktop:
        print(f"Show desktop in {args.delay:g}s. Hold Esc to stop.", flush=True)
        pause(api, args.delay)
        show_desktop(api, args.show_desktop, args.duration, rng)
        return
    if not args.inspect:
        print(f"{'Click' if args.click else 'Hover only'} in {args.delay:g}s. Hold Esc to stop.", flush=True)
        pause(api, args.delay)
    button = find_button(query_buttons(), args.app)
    cancelled(api)
    x = round((button["left"] + button["right"]) / 2)
    y = round((button["top"] + button["bottom"]) / 2)
    # Keep variation small and comfortably inside the button.
    if not args.inspect:
        spread_x = max(0, min(5, int((button["right"] - button["left"]) / 8)))
        spread_y = max(0, min(3, int((button["bottom"] - button["top"]) / 8)))
        x += rng.randint(-spread_x, spread_x)
        y += rng.randint(-spread_y, spread_y)
    print(f"Found {button['name']!r}: destination ({x}, {y}); bounds "
          f"({button['left']}, {button['top']}, {button['right']}, {button['bottom']}).", flush=True)
    if args.inspect:
        return
    verify_destination(api, button, x, y)
    pause(api, rng.uniform(0.25, 0.65))
    move_pointer(api, (x, y), args.duration or rng.uniform(1.0, 1.7), rng)
    pause(api, rng.uniform(0.18, 0.32))
    verify_destination(api, button, x, y)
    if pointer_position(api) != (x, y):
        raise RuntimeError("The mouse did not remain at the destination; stopping.")
    if args.click:
        # Re-read accessibility after movement, rather than trusting old coordinates.
        refreshed = find_button(query_buttons(), args.app)
        cancelled(api)
        if refreshed["name"] != button["name"]:
            raise RuntimeError("The target identity changed; stopping before clicking.")
        verify_destination(api, refreshed, x, y)
        if pointer_position(api) != (x, y):
            raise RuntimeError("The mouse was moved after arrival; stopping before clicking.")
        left_click(api, rng)
        print("Left-click sent. A running app may activate or show window previews.")
    else:
        print("Hover complete. No click sent. Add --click to test clicking.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.", file=sys.stderr)
        sys.exit(130)
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        sys.exit(1)
