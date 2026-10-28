"""One minute of coding, then one minute of browsing in equal window halves."""

import argparse
import ast
import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import random
import subprocess
import sys
import time


HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'browser-explorer'))
from browser_explorer import BudgetEnded, Explorer, START_URL, StopRequested, allowed_url


SAMPLE = '''# Split-window coding sample.
from statistics import mean

daily_totals = [12, 18, 9, 21, 15]

def summarize(values):
    return {
        "count": len(values),
        "total": sum(values),
        "average": round(mean(values), 2),
    }

summary = summarize(daily_totals)
for label, value in summary.items():
    print(f"{label}: {value}")
'''


class MONITORINFO(ctypes.Structure):
    _fields_ = [('cbSize', wintypes.DWORD), ('monitor', wintypes.RECT),
                ('work', wintypes.RECT), ('flags', wintypes.DWORD)]


def halves(work, coding_side):
    left, top, right, bottom = work
    middle = left + (right - left) // 2
    first, second = (left, top, middle, bottom), (middle, top, right, bottom)
    return (first, second) if coding_side == 'left' else (second, first)


def typing_schedule(text, duration, rng):
    weights = [rng.uniform(0.65, 1.45) + (rng.uniform(4, 10) if char == '\n' else 0)
               for char in text]
    total = sum(weights)
    elapsed = 0
    result = []
    for char, weight in zip(text, weights):
        elapsed += weight
        result.append((duration * elapsed / total, char))
    return result


class SplitDemo(Explorer):
    def __init__(self, args):
        self.active_target = None
        self.layout_ready = False
        self.next_layout_check = 0
        self.code_hwnd = None
        super().__init__(args)
        extra = {
            'GetWindowTextW': ([wintypes.HWND, wintypes.LPWSTR, ctypes.c_int], ctypes.c_int),
            'IsWindow': ([wintypes.HWND], wintypes.BOOL),
            'IsIconic': ([wintypes.HWND], wintypes.BOOL),
            'IsZoomed': ([wintypes.HWND], wintypes.BOOL),
            'GetClientRect': ([wintypes.HWND, ctypes.POINTER(wintypes.RECT)], wintypes.BOOL),
            'ClientToScreen': ([wintypes.HWND, ctypes.POINTER(wintypes.POINT)], wintypes.BOOL),
            'GetMonitorInfoW': ([wintypes.HANDLE, ctypes.POINTER(MONITORINFO)], wintypes.BOOL),
            'SetWindowPos': ([wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                              ctypes.c_int, ctypes.c_int, wintypes.UINT], wintypes.BOOL),
        }
        for name, (arguments, result) in extra.items():
            function = getattr(self.api, name)
            function.argtypes, function.restype = arguments, result
        self.dwm = ctypes.WinDLL('dwmapi')
        self.dwm.DwmGetWindowAttribute.argtypes = [wintypes.HWND, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD]
        self.dwm.DwmGetWindowAttribute.restype = ctypes.c_long
        self.code_process = None
        self.project_folder = args.folder
        self.scratch = self.run_dir / 'split_sample.py'
        self.window_tag = 'Split sample ' + self.run_dir.name
        self.phase = 'setup'
        self.phase_results = {}
        point = self.pointer.pointer_position(self.api)
        monitor = self.api.MonitorFromPoint(wintypes.POINT(*point), 2)
        info = MONITORINFO(cbSize=ctypes.sizeof(MONITORINFO))
        if not self.api.GetMonitorInfoW(monitor, ctypes.byref(info)):
            raise ctypes.WinError(ctypes.get_last_error())
        work = info.work
        self.work = (work.left, work.top, work.right, work.bottom)
        if work.right - work.left < 1040 or work.bottom - work.top < 550:
            raise RuntimeError('This monitor is too small for two useful app halves (need at least 1040x550 work area).')
        self.coding_side = self.rng.choice(('left', 'right')) if args.coding_side == 'random' else args.coding_side
        self.code_rect, self.browser_rect = halves(self.work, self.coding_side)

    def log(self, event, **data):
        super().log(event, phase=getattr(self, 'phase', 'setup'), **data)

    def frame(self, hwnd):
        rect = wintypes.RECT()
        if self.dwm.DwmGetWindowAttribute(hwnd, 9, ctypes.byref(rect), ctypes.sizeof(rect)) != 0:
            if not self.api.GetWindowRect(hwnd, ctypes.byref(rect)):
                raise ctypes.WinError(ctypes.get_last_error())
        return rect.left, rect.top, rect.right, rect.bottom

    def check(self, focus=True):
        super().check(focus=False)
        if focus and self.active_target and self.api.GetForegroundWindow() != self.active_target:
            raise StopRequested(f'The {self.phase} target lost focus. No further input will be sent.')
        if self.layout_ready and time.monotonic() >= self.next_layout_check:
            for hwnd, expected in ((self.code_hwnd, self.code_rect), (self.hwnd, self.browser_rect)):
                if (not self.api.IsWindow(hwnd) or not self.api.IsWindowVisible(hwnd)
                        or self.api.IsIconic(hwnd) or self.api.IsZoomed(hwnd)
                        or max(abs(a - b) for a, b in zip(self.frame(hwnd), expected)) > 5):
                    raise StopRequested('The split layout changed. Keep both demo windows visible and in place.')
            self.next_layout_check = time.monotonic() + 0.75

    def place(self, hwnd, desired):
        self.api.ShowWindow(hwnd, 9)
        self.pause(0.2, focus=False)
        outer = wintypes.RECT()
        if not self.api.GetWindowRect(hwnd, ctypes.byref(outer)):
            raise ctypes.WinError(ctypes.get_last_error())
        visible = self.frame(hwnd)
        borders = (visible[0] - outer.left, visible[1] - outer.top,
                   outer.right - visible[2], outer.bottom - visible[3])
        left, top, right, bottom = desired
        if not self.api.SetWindowPos(hwnd, None, left - borders[0], top - borders[1],
                                    right - left + borders[0] + borders[2],
                                    bottom - top + borders[1] + borders[3], 0x0014):
            raise ctypes.WinError(ctypes.get_last_error())
        self.pause(0.3, focus=False)

    def activate(self, hwnd):
        self.active_target = None
        self.check(focus=False)
        self.api.SetForegroundWindow(hwnd)
        self.pause(0.25, focus=False)
        if self.api.GetForegroundWindow() != hwnd:
            left, top, right, _ = self.frame(hwnd)
            x, y = (left + right) // 2, top + 14
            hit = self.api.WindowFromPoint(wintypes.POINT(x, y))
            result = ctypes.c_size_t()
            coordinate = ((y & 0xffff) << 16) | (x & 0xffff)
            self.api.SendMessageTimeoutW(hwnd, 0x0084, 0, coordinate, 2, 500, ctypes.byref(result))
            if not hit or self.api.GetAncestor(hit, 2) != hwnd or result.value != 2:
                raise StopRequested('Windows declined activation and no safe title-bar point was available.')
            self.move(x, y)
            self.pointer.left_click(self.api, self.rng)
            self.pause(0.25, focus=False)
        if self.api.GetForegroundWindow() != hwnd:
            raise StopRequested('The intended demo window did not become active.')
        self.active_target = hwnd

    def find_code_window(self):
        found = []
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

        @callback_type
        def visit(hwnd, _):
            title = ctypes.create_unicode_buffer(1024)
            self.api.GetWindowTextW(hwnd, title, len(title))
            if self.api.IsWindowVisible(hwnd) and self.window_tag in title.value:
                found.append(hwnd)
            return True
        self.api.EnumWindows(visit, 0)
        if len(found) > 1:
            raise RuntimeError('More than one scratch editor window matched.')
        return found[0] if found else None

    def launch_code(self):
        candidates = [Path(os.environ.get(root, '')) / suffix for root, suffix in (
            ('LOCALAPPDATA', 'Programs/Microsoft VS Code/Code.exe'),
            ('ProgramFiles', 'Microsoft VS Code/Code.exe'),
            ('ProgramFiles(x86)', 'Microsoft VS Code/Code.exe'),
        )]
        executable = next((p for p in candidates if p.is_file()), None)
        if executable is None:
            raise RuntimeError('Visual Studio Code was not found.')
        profile = self.run_dir / 'vscode-profile'
        settings_dir = profile / 'User'
        settings_dir.mkdir(parents=True)
        settings = {
            'window.title': self.window_tag + ' - ${activeEditorShort}',
            'window.commandCenter': False, 'window.restoreWindows': 'none',
            'security.workspace.trust.startupPrompt': 'never',
            'workbench.startupEditor': 'none', 'update.showReleaseNotes': False,
            'editor.accessibilitySupport': 'on', 'editor.autoIndent': 'none',
            'editor.autoClosingBrackets': 'never', 'editor.autoClosingQuotes': 'never',
            'editor.autoSurround': 'never', 'editor.formatOnType': False, 'editor.formatOnSave': False,
            'editor.quickSuggestions': False, 'editor.suggestOnTriggerCharacters': False,
            'editor.acceptSuggestionOnCommitCharacter': False, 'editor.wordBasedSuggestions': 'off',
            'editor.minimap.enabled': False, 'editor.fontSize': 15, 'files.eol': '\n',
            'files.autoSave': 'off', 'files.insertFinalNewline': False,
            '[python]': {'editor.autoIndent': 'none', 'editor.autoClosingBrackets': 'never',
                         'editor.autoClosingQuotes': 'never', 'editor.formatOnType': False},
        }
        (settings_dir / 'settings.json').write_text(json.dumps(settings, indent=2), encoding='utf-8')
        with self.scratch.open('x', encoding='utf-8'):
            pass
        self.code_process = subprocess.Popen([
            str(executable), '--new-window', '--disable-extensions',
            '--user-data-dir=' + str(profile), '--extensions-dir=' + str(self.run_dir / 'vscode-extensions'),
            '--force-renderer-accessibility=complete', str(self.project_folder), str(self.scratch),
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        finish = time.monotonic() + 35
        while time.monotonic() < finish:
            self.code_hwnd = self.find_code_window()
            if self.code_hwnd:
                self.log('project-folder-opened', folder=str(self.project_folder))
                return
            self.pause(0.2, focus=False)
        raise RuntimeError('The isolated scratch editor did not appear.')

    def editor_ready(self):
        powershell = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        result = subprocess.run([str(powershell), '-NoLogo', '-NoProfile', '-NonInteractive', '-MTA',
                                 '-File', str(HERE / 'editor_status.ps1'), '-WindowHandle', str(self.code_hwnd),
                                 '-FileName', self.scratch.name, '-FocusEditor'],
                                capture_output=True, text=True, encoding='utf-8-sig', errors='replace',
                                timeout=12, creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            raise RuntimeError('Editor accessibility check failed: ' + result.stderr.strip())
        status = json.loads(result.stdout.strip())
        self.log('editor-focus', **status)
        return status.get('ok') and status.get('editable') and self.scratch.name in status.get('title', '')

    def setup(self):
        self.launch()
        self.active_target = None
        self.launch_code()
        self.place(self.hwnd, self.browser_rect)
        self.place(self.code_hwnd, self.code_rect)
        self.layout_ready = True
        self.check(focus=False)
        self.log('split-layout', coding_side=self.coding_side, coding=self.code_rect, browser=self.browser_rect)
        self.activate(self.hwnd)
        self.wait_initial()
        self.activate(self.code_hwnd)
        # Reveal the project's Explorer tree before focusing the external sample.
        self.hotkey(0x11, 0x10, ord('E'))
        ready = False
        finish = time.monotonic() + 20
        while time.monotonic() < finish:
            self.hotkey(0x1B)
            self.hotkey(0x11, ord('1'))
            self.pause(0.4)
            if self.editor_ready():
                ready = True
                break
            self.pause(0.6)
        if not ready:
            raise RuntimeError('The scratch file editor is not the focused editable control. No sample text was sent.')
        if self.scratch.read_text(encoding='utf-8'):
            raise RuntimeError('The new scratch file is not empty.')

    def type_character(self, char):
        self.check()
        if char == '\n':
            self.pointer.send_key(self.api, 0x0D)
            try:
                self.pause(0.025)
            finally:
                self.pointer.send_key(self.api, 0x0D, up=True)
            return
        events = (self.pointer.INPUT * 2)()
        for index, flag in enumerate((0x0004, 0x0006)):
            events[index].type = 1
            events[index].data.ki = self.pointer.KEYBDINPUT(wScan=ord(char), dwFlags=flag)
        if self.api.SendInput(2, events, ctypes.sizeof(self.pointer.INPUT)) != 2:
            raise RuntimeError('Windows rejected sample typing.')

    def move_to_editor(self):
        client, origin = wintypes.RECT(), wintypes.POINT()
        if not self.api.GetClientRect(self.code_hwnd, ctypes.byref(client)) or not self.api.ClientToScreen(self.code_hwnd, ctypes.byref(origin)):
            raise ctypes.WinError(ctypes.get_last_error())
        x = origin.x + round(client.right * self.rng.uniform(0.55, 0.72))
        y = origin.y + round(client.bottom * self.rng.uniform(0.35, 0.65))
        hit = self.api.WindowFromPoint(wintypes.POINT(x, y))
        if not hit or self.api.GetAncestor(hit, 2) != self.code_hwnd:
            raise StopRequested('Another window covers the scratch editor.')
        self.move(x, y, purpose='rest')

    def coding_phase(self):
        self.phase = 'coding'
        self.activate(self.code_hwnd)
        self.move_to_editor()
        self.started = time.monotonic()
        self.deadline = self.started + self.args.coding_seconds
        saved = False
        self.log('phase-start', seconds=self.args.coding_seconds, file=str(self.scratch))
        try:
            for offset, char in typing_schedule(SAMPLE, self.args.coding_seconds * 0.80, self.rng):
                self.pause(max(0, self.started + offset - time.monotonic()))
                self.type_character(char)
            self.hotkey(0x11, ord('S'))
            finish = min(self.deadline - 0.5, time.monotonic() + 6)
            while time.monotonic() < finish:
                if self.scratch.read_text(encoding='utf-8').replace('\r\n', '\n') == SAMPLE:
                    saved = True
                    ast.parse(SAMPLE)
                    self.log('sample-saved', file=str(self.scratch), characters=len(SAMPLE))
                    break
                self.pause(0.1)
            if not saved:
                raise RuntimeError('The saved scratch file did not match the sample; browsing will not start.')
            self.move_to_editor()
            self.pause(max(0, self.deadline - time.monotonic()) + 0.05)
        except BudgetEnded:
            if not saved:
                raise RuntimeError('The coding minute expired before the sample was saved.')
        finally:
            self.phase_results['coding_seconds'] = round(time.monotonic() - self.started, 2)
            self.deadline = None
        self.log('phase-end', saved=saved)

    def browsing_phase(self):
        self.phase = 'browsing'
        self.activate(self.hwnd)
        snapshot = self.snapshot()
        self.relax_pointer(snapshot, reason='phase-switch', chance=1)
        self.log('phase-start', seconds=self.args.seconds)
        try:
            self.explore(snapshot)
        except BudgetEnded:
            pass
        finally:
            self.phase_results['browsing_seconds'] = round(time.monotonic() - self.started, 2)
            self.deadline = None
        self.log('phase-end', pages=len(self.visits))

    def run_demo(self):
        self.setup()
        self.coding_phase()
        self.browsing_phase()
        self.phase = 'return-to-coding'
        self.activate(self.code_hwnd)
        self.hotkey(0x11, ord('1'))
        self.move_to_editor()
        self.check()
        self.log('returned-to-coding', file=str(self.scratch))

    def finish(self, status, message=''):
        report = {'status': status, 'message': message, 'coding_side': self.coding_side,
                  'project_folder': str(self.project_folder),
                  'phase_times': self.phase_results, 'scratch_file': str(self.scratch),
                  'coding_rect': self.code_rect, 'browser_rect': self.browser_rect,
                  'returned_to_coding': bool(self.code_hwnd and self.api.GetForegroundWindow() == self.code_hwnd)}
        (self.run_dir / 'split-summary.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        self.deadline = None
        super().finish(status, message)
        print('Split demo report: ' + str(self.run_dir / 'split-summary.json'), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--coding-side', choices=('left', 'right', 'random'), default='left')
    parser.add_argument('--folder', type=Path, default=Path.home() / 'Desktop' / 'Project for vehicle',
                        help='Existing project folder to open in VS Code (default: Desktop/Project for vehicle).')
    parser.add_argument('--coding-seconds', type=float, default=60)
    parser.add_argument('--browsing-seconds', type=float, default=60)
    parser.add_argument('--url', default=START_URL)
    parser.add_argument('--countdown', type=float, default=5)
    parser.add_argument('--seed', type=int, default=17)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Windows 10/11 is required.')
    if not allowed_url(args.url):
        parser.error('Supply a valid HTTP(S) browsing URL.')
    args.folder = args.folder.expanduser().resolve()
    if not args.folder.is_dir():
        parser.error('The project folder does not exist: ' + str(args.folder))
    if (not all(math.isfinite(x) for x in (args.coding_seconds, args.browsing_seconds, args.countdown))
            or not 10 <= args.coding_seconds <= 300 or not 5 <= args.browsing_seconds <= 600
            or not 0 <= args.countdown <= 60):
        parser.error('coding: 10-300s; browsing: 5-600s; countdown: 0-60s.')
    args.seconds, args.page_min, args.page_max, args.inspect = args.browsing_seconds, 12, 24, False
    demo = SplitDemo(args)
    status, message, result = 'error', '', 1
    try:
        demo.run_demo()
        status, message, result = 'completed', 'Both phases completed; focus returned to the scratch editor.', 0
    except (StopRequested, KeyboardInterrupt) as error:
        status, message, result = 'stopped', str(error) or 'Stopped with Ctrl+C.', 130
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        message = str(error)
    finally:
        demo.finish(status, message)
    return result


if __name__ == '__main__':
    raise SystemExit(main())
