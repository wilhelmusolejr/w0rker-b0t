"""Show/maximize Loggy3 through its taskbar button, or minimize its window."""

import argparse
import ctypes
from ctypes import wintypes
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import re
import sys
import time


HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'browser-explorer'))
from motion import click_point, pointer_path, rest_point


class MONITORINFO(ctypes.Structure):
    _fields_ = [('cbSize', wintypes.DWORD), ('rcMonitor', wintypes.RECT),
                ('rcWork', wintypes.RECT), ('dwFlags', wintypes.DWORD)]


def load_pointer():
    path = HERE.parent / 'taskbar-pointer-demo/taskbar_demo.py'
    spec = importlib.util.spec_from_file_location('loggy_pointer', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class LoggyWindow:
    def __init__(self, seed=None):
        self.pointer = load_pointer()
        self.api = self.pointer.windows_api()
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.rng = random.Random(seed)
        signatures = {
            'EnumWindows': ([ctypes.c_void_p, wintypes.LPARAM], wintypes.BOOL),
            'GetWindowTextW': ([wintypes.HWND, wintypes.LPWSTR, ctypes.c_int], ctypes.c_int),
            'GetWindowThreadProcessId': ([wintypes.HWND, ctypes.POINTER(wintypes.DWORD)], wintypes.DWORD),
            'GetWindow': ([wintypes.HWND, wintypes.UINT], wintypes.HWND),
            'IsWindow': ([wintypes.HWND], wintypes.BOOL),
            'IsWindowVisible': ([wintypes.HWND], wintypes.BOOL),
            'IsWindowEnabled': ([wintypes.HWND], wintypes.BOOL),
            'IsIconic': ([wintypes.HWND], wintypes.BOOL),
            'IsZoomed': ([wintypes.HWND], wintypes.BOOL),
            'GetWindowLongW': ([wintypes.HWND, ctypes.c_int], wintypes.LONG),
            'GetWindowRect': ([wintypes.HWND, ctypes.POINTER(wintypes.RECT)], wintypes.BOOL),
            'ShowWindowAsync': ([wintypes.HWND, ctypes.c_int], wintypes.BOOL),
            'SetForegroundWindow': ([wintypes.HWND], wintypes.BOOL),
            'MonitorFromWindow': ([wintypes.HWND, wintypes.DWORD], wintypes.HANDLE),
            'GetMonitorInfoW': ([wintypes.HANDLE, ctypes.POINTER(MONITORINFO)], wintypes.BOOL),
            'SetWindowPos': ([wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                              ctypes.c_int, ctypes.c_int, wintypes.UINT], wintypes.BOOL),
            'OpenInputDesktop': ([wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], wintypes.HANDLE),
            'GetUserObjectInformationW': ([wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                          wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL),
            'CloseDesktop': ([wintypes.HANDLE], wintypes.BOOL),
        }
        for name, (arguments, result) in signatures.items():
            function = getattr(self.api, name)
            function.argtypes, function.restype = arguments, result
        self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.kernel.OpenProcess.restype = wintypes.HANDLE
        self.kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD,
                                                         wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
        self.kernel.QueryFullProcessImageNameW.restype = wintypes.BOOL
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.ProcessIdToSessionId.argtypes = [wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
        self.next_desktop_check = 0

    def check(self):
        if any(self.api.GetAsyncKeyState(key) & 0x8000 for key in (0x1B, 0x78, 0x7B)):
            raise KeyboardInterrupt
        if time.monotonic() < self.next_desktop_check:
            return
        session = wintypes.DWORD()
        if not self.kernel.ProcessIdToSessionId(os.getpid(), ctypes.byref(session)):
            raise RuntimeError('Cannot determine the current Windows session.')
        wts = ctypes.WinDLL('wtsapi32', use_last_error=True)
        wts.WTSQuerySessionInformationW.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.c_int,
                                                   ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.DWORD)]
        wts.WTSQuerySessionInformationW.restype = wintypes.BOOL
        wts.WTSFreeMemory.argtypes = [ctypes.c_void_p]
        data, size = ctypes.c_void_p(), wintypes.DWORD()
        if not wts.WTSQuerySessionInformationW(None, session.value, 8, ctypes.byref(data), ctypes.byref(size)):
            raise RuntimeError('Cannot read the current Windows session state.')
        try:
            if ctypes.cast(data, ctypes.POINTER(ctypes.c_int)).contents.value != 0:
                raise RuntimeError('Keep the Windows/RDP session connected and active.')
        finally:
            wts.WTSFreeMemory(data)
        desktop = self.api.OpenInputDesktop(0, False, 1)
        if not desktop:
            raise RuntimeError('Unlock Windows and run the script inside the desktop being controlled.')
        try:
            name, needed = ctypes.create_unicode_buffer(128), wintypes.DWORD()
            if not self.api.GetUserObjectInformationW(desktop, 2, name, ctypes.sizeof(name), ctypes.byref(needed)) or name.value != 'Default':
                raise RuntimeError('The unlocked Default input desktop is required.')
        finally:
            self.api.CloseDesktop(desktop)
        self.next_desktop_check = time.monotonic() + 1

    def pause(self, seconds):
        finish = time.monotonic() + seconds
        while time.monotonic() < finish:
            self.check()
            time.sleep(min(0.03, max(0, finish - time.monotonic())))

    def wait(self, predicate, message, timeout=15):
        finish = time.monotonic() + timeout
        while time.monotonic() < finish:
            self.check()
            result = predicate()
            if result:
                return result
            self.pause(0.08)
        raise RuntimeError(message)

    def process_name(self, pid):
        process = self.kernel.OpenProcess(0x1000, False, pid)
        if not process:
            return ''
        try:
            path, length = ctypes.create_unicode_buffer(32768), wintypes.DWORD(32768)
            if self.kernel.QueryFullProcessImageNameW(process, 0, path, ctypes.byref(length)):
                return Path(path.value).name.casefold()
            return ''
        finally:
            self.kernel.CloseHandle(process)

    def windows(self):
        matches, processes = [], {}
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

        @callback_type
        def visit(hwnd, _):
            title = ctypes.create_unicode_buffer(512)
            self.api.GetWindowTextW(hwnd, title, len(title))
            if not re.match(r'^loggy3(?:\s|$)', title.value, re.I) or self.api.GetWindow(hwnd, 4):
                return True
            pid = wintypes.DWORD()
            self.api.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value not in processes:
                processes[pid.value] = self.process_name(pid.value)
            if processes[pid.value] in ('loggy3-gui.exe', 'loggy3.exe'):
                matches.append({'hwnd': hwnd, 'pid': pid.value, 'title': title.value,
                                'process': processes[pid.value], **self.state(hwnd)})
            return True
        self.api.EnumWindows(visit, 0)
        return matches

    def window(self):
        matches = self.windows()
        if len(matches) > 1:
            raise RuntimeError('More than one Loggy3 main window matched; close extra windows and retry.')
        return matches[0]['hwnd'] if matches else None

    def state(self, hwnd):
        return {'visible': bool(self.api.IsWindowVisible(hwnd)), 'minimized': bool(self.api.IsIconic(hwnd)),
                'maximized': bool(self.api.IsZoomed(hwnd)), 'foreground': self.api.GetForegroundWindow() == hwnd,
                'can_maximize': bool(self.api.GetWindowLongW(hwnd, -16) & 0x10000)}

    def move(self, x, y):
        path = pointer_path(self.pointer.pointer_position(self.api), (x, y), self.rng)
        started = time.monotonic()
        while True:
            self.check()
            t = min(1, (time.monotonic() - started) / path.duration)
            px, py = path.at(t)
            point = wintypes.POINT(round(px), round(py))
            if not self.api.MonitorFromPoint(point, 0):
                raise RuntimeError('The pointer path would leave connected monitors.')
            if not self.api.SetCursorPos(point.x, point.y):
                raise ctypes.WinError(ctypes.get_last_error())
            if t == 1:
                return
            self.pause(0.01)

    def click_taskbar(self):
        button = self.pointer.find_button(self.pointer.query_buttons(), 'Loggy3')
        x, y = click_point(button, self.rng)
        self.pointer.verify_destination(self.api, button, x, y)
        print(f"Clicking taskbar button: {button['name']}", flush=True)
        self.move(x, y)
        fresh = self.pointer.find_button(self.pointer.query_buttons(), 'Loggy3')
        px, py = self.pointer.pointer_position(self.api)
        if max(abs(px - x), abs(py - y)) > 2:
            raise RuntimeError('The mouse moved before the taskbar click; stopping.')
        self.pointer.verify_destination(self.api, fresh, px, py)
        self.check()
        self.pointer.left_click(self.api, self.rng)

    def move_to_monitor(self, hwnd, monitor):
        if self.api.MonitorFromWindow(hwnd, 2) == monitor:
            return
        self.api.ShowWindowAsync(hwnd, 9)
        self.wait(lambda: not self.api.IsIconic(hwnd) and not self.api.IsZoomed(hwnd),
                  'Loggy3 did not restore before moving to the current monitor.')
        info = MONITORINFO(cbSize=ctypes.sizeof(MONITORINFO))
        rect = wintypes.RECT()
        if not self.api.GetMonitorInfoW(monitor, ctypes.byref(info)) or not self.api.GetWindowRect(hwnd, ctypes.byref(rect)):
            raise ctypes.WinError(ctypes.get_last_error())
        work = info.rcWork
        width = min(max(320, rect.right - rect.left), work.right - work.left)
        height = min(max(240, rect.bottom - rect.top), work.bottom - work.top)
        x, y = work.left + (work.right - work.left - width) // 2, work.top + (work.bottom - work.top - height) // 2
        if not self.api.SetWindowPos(hwnd, None, x, y, width, height, 0x0014):
            raise ctypes.WinError(ctypes.get_last_error())

    def show(self):
        self.check()
        x, y = self.pointer.pointer_position(self.api)
        monitor = self.api.MonitorFromPoint(wintypes.POINT(x, y), 2)
        hwnd = self.window()
        if not hwnd or not self.state(hwnd)['foreground'] or self.state(hwnd)['minimized']:
            self.click_taskbar()
        else:
            print('Loggy3 is already active; skipping the taskbar toggle.', flush=True)
        hwnd = self.wait(self.window, 'Loggy3 did not expose a main window after the taskbar click.')
        if not self.api.IsWindowEnabled(hwnd):
            raise RuntimeError('Loggy3 has an active modal dialog; resolve it before maximizing.')
        self.wait(lambda: self.api.IsWindowVisible(hwnd) and not self.api.IsIconic(hwnd),
                  'The Loggy3 taskbar click did not show its window.')
        self.move_to_monitor(hwnd, monitor)
        self.api.ShowWindowAsync(hwnd, 3)
        self.api.SetForegroundWindow(hwnd)
        if not self.state(hwnd)['can_maximize']:
            raise RuntimeError('Loggy3 is showing, but this window does not support maximization.')
        self.wait(lambda: self.api.IsWindowVisible(hwnd) and not self.api.IsIconic(hwnd)
                  and self.api.IsZoomed(hwnd) and self.api.GetForegroundWindow() == hwnd
                  and self.api.MonitorFromWindow(hwnd, 2) == monitor,
                  'Loggy3 did not become visible, maximized, and active on the current monitor.')
        print('Loggy3 is visible, maximized, and active on the current monitor.', flush=True)
        rect = wintypes.RECT()
        self.api.GetWindowRect(hwnd, ctypes.byref(rect))
        rx, ry = rest_point({'left': rect.left, 'top': rect.top + 40, 'right': rect.right, 'bottom': rect.bottom}, self.rng)
        hit = self.api.WindowFromPoint(wintypes.POINT(rx, ry))
        if hit and self.api.GetAncestor(hit, 2) == hwnd:
            self.move(rx, ry)
        return hwnd

    def minimize(self, hwnd=None):
        self.check()
        hwnd = hwnd or self.window()
        if not hwnd:
            print('No Loggy3 window is showing; nothing to minimize.', flush=True)
            return
        if self.api.IsIconic(hwnd) or not self.api.IsWindowVisible(hwnd):
            print('Loggy3 is already minimized or hidden.', flush=True)
            return
        if not self.api.IsWindowEnabled(hwnd):
            raise RuntimeError('Loggy3 has an active modal dialog; resolve it before minimizing.')
        self.api.ShowWindowAsync(hwnd, 6)
        self.wait(lambda: self.api.IsWindow(hwnd) and (self.api.IsIconic(hwnd) or not self.api.IsWindowVisible(hwnd)),
                  'Loggy3 did not minimize.')
        print('Loggy3 is minimized. The app is still running.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('demo', 'show', 'minimize'), default='demo', nargs='?')
    parser.add_argument('--hold', type=float, default=5, help='seconds to show the app before minimizing in demo mode')
    parser.add_argument('--countdown', type=float, default=3)
    parser.add_argument('--seed', type=int)
    parser.add_argument('--inspect', action='store_true', help='read taskbar/window state without changing it')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Windows 10/11 is required.')
    if not math.isfinite(args.hold) or not 0 <= args.hold <= 300 or not math.isfinite(args.countdown) or not 0 <= args.countdown <= 60:
        parser.error('hold: 0-300 seconds; countdown: 0-60 seconds.')
    controller = LoggyWindow(args.seed)
    controller.check()
    if args.inspect:
        buttons = [b for b in controller.pointer.query_buttons() if 'loggy3' in b['name'].casefold()]
        print(json.dumps({'taskbar': buttons, 'windows': controller.windows()}, indent=2))
        return
    print(f'{args.action} starts in {args.countdown:g}s. Esc/F9/F12 stop.', flush=True)
    controller.pause(args.countdown)
    if args.action == 'minimize':
        controller.minimize()
        return
    hwnd = controller.show()
    if args.action == 'demo':
        print(f'Keeping Loggy3 visible for {args.hold:g} seconds...', flush=True)
        controller.pause(args.hold)
        controller.minimize(hwnd)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\nStopped.', file=sys.stderr)
        sys.exit(130)
    except (OSError, RuntimeError, ValueError) as error:
        print(f'Error: {error}', file=sys.stderr)
        sys.exit(1)
