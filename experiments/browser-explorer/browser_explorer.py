"""Visible Windows accessibility browsing experiment; no browser drivers or AI."""

import argparse
from collections import Counter
import ctypes
from ctypes import wintypes
import hashlib
import importlib.util
import ipaddress
import json
import math
import os
from pathlib import Path
import queue
import random
import re
import subprocess
import sys
import threading
import time
import uuid
from urllib.parse import parse_qsl, unquote, urljoin, urlsplit, urlunsplit

from motion import ScrollRhythm, click_point, pointer_path, rest_point


HERE = Path(__file__).resolve().parent
START_URL = 'https://docs.python.org/3/tutorial/'
BLOCKED_SEGMENTS = set('login logout signin signout signup sign-in sign-out log-in log-out register '
                       'checkout purchase subscribe unsubscribe delete remove edit upload create settings '
                       'preferences follow unfollow star unstar new'.split())
DOWNLOAD_SUFFIXES = ('.zip', '.exe', '.msi', '.dmg', '.pkg', '.deb', '.rpm', '.tar', '.gz',
                     '.bz2', '.xz', '.7z', '.rar', '.iso', '.pdf', '.doc', '.docx', '.xls',
                     '.xlsx', '.ppt', '.pptx')
UNSAFE_NAMES = re.compile(r'\b(follow|unfollow|star|unstar|watch|unwatch|subscribe|unsubscribe|sponsor|'
                          r'delete|remove|report|block|submit|save|create|upload|download|sign in|'
                          r'sign up|sign out|log out|purchase|buy|merge|revert|fork|checkout|print)\b', re.I)
LOCAL_BUTTONS = re.compile(r'^(close|dismiss|cancel|more|show|hide|load more|view|read|learn|browse|'
                           r'explore|next|previous|back|preview|details|menu|expand|collapse|toggle|open)', re.I)


class BudgetEnded(Exception):
    pass


class StopRequested(Exception):
    pass


def canonical(url, fragment=False):
    parts = urlsplit(url)
    return urlunsplit((parts.scheme.lower(), parts.netloc.lower(), parts.path.rstrip('/') or '/',
                       parts.query, parts.fragment if fragment else ''))


def web_address(url):
    """Parse a real web address without silently accepting whitespace/credentials."""
    try:
        if not isinstance(url, str) or '\\' in url or any(ch.isspace() or ord(ch) < 32 or ord(ch) == 127 for ch in url):
            return None
        parts = urlsplit(url)
        if parts.scheme.lower() not in ('http', 'https') or not parts.hostname or parts.username is not None:
            return None
        if parts.port is not None and not 1 <= parts.port <= 65535:
            return None
        host_key(parts.hostname)
        return parts
    except ValueError:
        return None


def host_key(host):
    host = host.rstrip('.').lower()
    try:
        return ipaddress.ip_address(host).compressed
    except ValueError:
        return host.encode('idna').decode('ascii')


class SiteScope:
    """Derive the website boundary from the supplied URL, not a website list."""
    def __init__(self, url):
        parts = web_address(url)
        if parts is None:
            raise ValueError('A valid HTTP(S) starting URL is required.')
        host = host_key(parts.hostname)
        self.hosts = {host}
        try:
            ipaddress.ip_address(host)
        except ValueError:
            if '.' in host:
                self.hosts.add(host[4:] if host.startswith('www.') else 'www.' + host)
        port = parts.port or (443 if parts.scheme == 'https' else 80)
        self.ports = {80, 443} if port in (80, 443) else {port}

    def contains(self, url):
        parts = web_address(url)
        if parts is None:
            return False
        port = parts.port or (443 if parts.scheme == 'https' else 80)
        return host_key(parts.hostname) in self.hosts and port in self.ports


def allowed_url(url, scope=None):
    parts = web_address(url)
    if parts is None or (scope is not None and not scope.contains(url)):
        return False
    path = parts.path
    for _ in range(3):
        decoded = unquote(path)
        if decoded == path:
            break
        path = decoded
    segments = [x.casefold() for x in path.split('/') if x]
    if '..' in segments or any(segment in BLOCKED_SEGMENTS for segment in segments):
        return False
    if path.casefold().endswith(DOWNLOAD_SUFFIXES):
        return False
    for key, value in parse_qsl(parts.query):
        key, value = key.casefold(), value.casefold()
        if key in {'delete', 'download', 'quick_pull', 'filename'}:
            return False
        if key in {'action', 'do', 'operation', 'task', 'cmd'} and re.match(
                r'^(delete|remove|edit|upload|create|purchase|checkout|subscribe|unsubscribe|logout)(?:$|[_-])', value):
            return False
    return True


def signature(snapshot):
    # Ignore coordinates and runtime identities: scrolling/re-rendering can change
    # these without replacing the page's meaningful content.
    controls = [(x['type'], x['name'], x.get('value', '')) for x in snapshot.get('controls', [])]
    data = [snapshot.get('title'), snapshot.get('headings', []), snapshot.get('visible_text', ''), controls]
    return hashlib.sha256(json.dumps(data, ensure_ascii=True).encode()).hexdigest()[:16]


def classify(before, after, clicked=None):
    old_tabs = {t['id'] for t in before.get('tabs', [])}
    new_tabs = {t['id'] for t in after.get('tabs', [])}
    if new_tabs - old_tabs:
        return 'new-tab'
    old_overlays = {o['id'] for o in before.get('overlays', [])}
    new_overlays = {o['id'] for o in after.get('overlays', [])}
    if new_overlays - old_overlays:
        return 'overlay-opened'
    if old_overlays - new_overlays:
        return 'overlay-closed'
    if canonical(before['url']) != canonical(after['url']):
        return 'navigation'
    if urlsplit(before['url']).fragment != urlsplit(after['url']).fragment:
        return 'page-anchor'
    if clicked:
        current = next((x for x in after.get('controls', []) if x['id'] == clicked['id']), None)
        if current and clicked.get('expanded') != current.get('expanded'):
            return 'section-state-changed'
        if current and clicked.get('selected') != current.get('selected'):
            return 'selection-changed'
    if signature(before) != signature(after):
        return 'content-changed'
    return 'no-observable-change'


def eligible(control, current_url, overlays=(), scope=None):
    name = control.get('name', '')
    if not control.get('enabled') or control.get('offscreen') or UNSAFE_NAMES.search(name):
        return False
    if overlays and control.get('scope') not in {o['id'] for o in overlays}:
        return False
    if control['type'] == 'Hyperlink':
        value = control.get('value', '')
        target = urljoin(current_url, value) if value else ''
        return allowed_url(target, scope or SiteScope(current_url)) and target != current_url
    if control.get('in_form') and not re.match(r'^(close|dismiss|cancel)\b', name, re.I):
        return False
    if control['type'] in ('Button', 'TabItem', 'MenuItem', 'TreeItem'):
        return bool(control['type'] in ('TabItem', 'MenuItem', 'TreeItem')
                    or control.get('navigation') or LOCAL_BUTTONS.search(name)
                    or control.get('expanded') in ('Collapsed', 'Expanded'))
    return False


def load_pointer():
    path = HERE.parent / 'taskbar-pointer-demo' / 'taskbar_demo.py'
    spec = importlib.util.spec_from_file_location('pointer_demo', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def process_integrity(pid):
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    security = ctypes.WinDLL('advapi32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    security.OpenProcessToken.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE)]
    security.GetTokenInformation.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                            wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
    security.GetSidSubAuthorityCount.argtypes = [ctypes.c_void_p]
    security.GetSidSubAuthorityCount.restype = ctypes.POINTER(ctypes.c_ubyte)
    security.GetSidSubAuthority.argtypes = [ctypes.c_void_p, wintypes.DWORD]
    security.GetSidSubAuthority.restype = ctypes.POINTER(wintypes.DWORD)
    process = kernel.OpenProcess(0x1000, False, pid)
    token = wintypes.HANDLE()
    try:
        if not process or not security.OpenProcessToken(process, 8, ctypes.byref(token)):
            return 'unavailable'
        needed = wintypes.DWORD()
        security.GetTokenInformation(token, 25, None, 0, ctypes.byref(needed))
        data = ctypes.create_string_buffer(needed.value)
        if not security.GetTokenInformation(token, 25, data, len(data), ctypes.byref(needed)):
            return 'unavailable'
        sid = ctypes.c_void_p.from_buffer(data).value
        count = security.GetSidSubAuthorityCount(sid).contents.value
        level = security.GetSidSubAuthority(sid, count - 1).contents.value
        return {0x1000: 'low', 0x2000: 'medium', 0x3000: 'high', 0x4000: 'system'}.get(level, hex(level))
    finally:
        if token:
            kernel.CloseHandle(token)
        if process:
            kernel.CloseHandle(process)


class Explorer:
    def __init__(self, args):
        self.args = args
        self.scope = SiteScope(args.url)
        self.last_url = args.url
        self.rng = random.Random(args.seed)
        self.scroll_rhythm = ScrollRhythm(self.rng)
        self.last_pointer_motion = 0
        self.pointer = load_pointer()
        self.api = self.pointer.windows_api()
        self.ole32 = ctypes.OleDLL('ole32')
        self.ole32.CoInitializeEx(None, 0)
        self.oleacc = ctypes.OleDLL('oleacc')
        self.oleacc.AccessibleObjectFromWindow.argtypes = [wintypes.HWND, wintypes.DWORD,
                                                        ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
        self.oleacc.AccessibleObjectFromWindow.restype = ctypes.c_long
        self.accessible_objects = {}
        extra = {
            'EnumWindows': ([ctypes.c_void_p, wintypes.LPARAM], wintypes.BOOL),
            'EnumChildWindows': ([wintypes.HWND, ctypes.c_void_p, wintypes.LPARAM], wintypes.BOOL),
            'SendMessageTimeoutW': ([wintypes.HWND, wintypes.UINT, wintypes.WPARAM,
                                    wintypes.LPARAM, wintypes.UINT, wintypes.UINT,
                                    ctypes.POINTER(ctypes.c_size_t)], wintypes.LPARAM),
            'GetWindowThreadProcessId': ([wintypes.HWND, ctypes.POINTER(wintypes.DWORD)], wintypes.DWORD),
            'IsWindowVisible': ([wintypes.HWND], wintypes.BOOL),
            'GetWindowRect': ([wintypes.HWND, ctypes.POINTER(wintypes.RECT)], wintypes.BOOL),
            'SetForegroundWindow': ([wintypes.HWND], wintypes.BOOL),
            'ShowWindow': ([wintypes.HWND, ctypes.c_int], wintypes.BOOL),
        }
        for name, (params, result) in extra.items():
            function = getattr(self.api, name)
            function.argtypes, function.restype = params, result
        stamp = time.strftime('%Y%m%d-%H%M%S') + f'-{os.getpid()}'
        self.run_dir = HERE.parents[1] / '.tmp' / 'browser-explorer' / stamp
        self.run_dir.mkdir(parents=True)
        self.log_file = (self.run_dir / 'events.jsonl').open('w', encoding='utf-8')
        self.started = None
        self.deadline = None
        self.hwnd = None
        self.browser = None
        self.bridge = None
        self.bridge_errors = None
        self.responses = queue.Queue()
        self.visits = Counter()
        self.outcomes = Counter()
        self.interactions = Counter()
        self.page_since = 0
        self.page_dwell = 0
        self.navigation_depth = 0
        self.scroll_stalls = 0
        self.next_desktop_check = 0

    def assert_interactive(self):
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        wts = ctypes.WinDLL('wtsapi32', use_last_error=True)
        session = wintypes.DWORD()
        kernel.ProcessIdToSessionId.argtypes = [wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
        if not kernel.ProcessIdToSessionId(os.getpid(), ctypes.byref(session)):
            raise StopRequested('The Windows session could not be determined.')
        wts.WTSQuerySessionInformationW.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.c_int,
                                                   ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.DWORD)]
        wts.WTSQuerySessionInformationW.restype = wintypes.BOOL
        wts.WTSFreeMemory.argtypes = [ctypes.c_void_p]
        data = ctypes.c_void_p()
        length = wintypes.DWORD()
        if not wts.WTSQuerySessionInformationW(None, session.value, 8, ctypes.byref(data), ctypes.byref(length)):
            raise StopRequested('The Windows session state could not be read.')
        try:
            state = ctypes.cast(data, ctypes.POINTER(ctypes.c_int)).contents.value
        finally:
            wts.WTSFreeMemory(data)
        if state != 0:
            label = {1: 'Connected', 4: 'Disconnected', 5: 'Idle'}.get(state, str(state))
            raise StopRequested(f'An active interactive Windows session is required; session {session.value} is {label}.')
        self.api.OpenInputDesktop.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.api.OpenInputDesktop.restype = wintypes.HANDLE
        self.api.GetUserObjectInformationW.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p,
                                                       wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
        self.api.CloseDesktop.argtypes = [wintypes.HANDLE]
        desktop = self.api.OpenInputDesktop(0, False, 1)
        if not desktop:
            raise StopRequested('The input desktop is unavailable. Unlock Windows and keep RDP connected.')
        try:
            name = ctypes.create_unicode_buffer(128)
            needed = wintypes.DWORD()
            if not self.api.GetUserObjectInformationW(desktop, 2, name, ctypes.sizeof(name), ctypes.byref(needed)) or name.value != 'Default':
                raise StopRequested('The unlocked Default input desktop is required.')
        finally:
            self.api.CloseDesktop(desktop)

    def log(self, event, **data):
        elapsed = round(time.monotonic() - self.started, 2) if self.started else 0
        record = {'elapsed': elapsed, 'event': event, **data}
        self.log_file.write(json.dumps(record, ensure_ascii=True) + '\n')
        self.log_file.flush()
        detail = (data.get('outcome') or data.get('url') or data.get('message') or data.get('name')
                  or data.get('reason') or (f'{data["seconds"]:.1f}s' if 'seconds' in data else ''))
        print(f'[{elapsed:6.1f}s] {event}: {detail}', flush=True)

    def check(self, focus=True):
        if time.monotonic() >= self.next_desktop_check:
            self.assert_interactive()
            self.next_desktop_check = time.monotonic() + 1
        if self.api.GetAsyncKeyState(0x78) & 0x8000 or self.api.GetAsyncKeyState(0x7B) & 0x8000:
            raise StopRequested('Stopped with F9/F12.')
        if self.deadline is not None and time.monotonic() >= self.deadline:
            raise BudgetEnded
        if focus and self.hwnd and self.api.GetForegroundWindow() != self.hwnd:
            raise StopRequested('The test browser lost focus. No more input will be sent.')

    def pause(self, seconds, focus=True):
        finish = time.monotonic() + seconds
        while time.monotonic() < finish:
            self.check(focus)
            time.sleep(min(0.04, max(0, finish - time.monotonic())))

    def hotkey(self, *keys, focus=True):
        held = []
        try:
            for key in keys:
                self.check(focus)
                self.pointer.send_key(self.api, key)
                held.append(key)
                self.pause(self.rng.uniform(0.04, 0.08), focus=focus)
        finally:
            for key in reversed(held):
                self.pointer.send_key(self.api, key, up=True)

    def move(self, x, y, purpose='aim', target_width=40):
        self.check()
        start_x, start_y = self.pointer.pointer_position(self.api)
        if math.hypot(x - start_x, y - start_y) < 2:
            return
        path = pointer_path((start_x, start_y), (x, y), self.rng, purpose, target_width)
        started = time.monotonic()
        while True:
            self.check()
            t = min(1, (time.monotonic() - started) / path.duration)
            px, py = path.at(t)
            point = wintypes.POINT(round(px), round(py))
            if not self.api.MonitorFromPoint(point, 0):
                raise StopRequested('The pointer path leaves connected monitors.')
            if not self.api.SetCursorPos(point.x, point.y):
                raise ctypes.WinError(ctypes.get_last_error())
            if t == 1:
                self.last_pointer_motion = time.monotonic()
                return
            self.pause(self.rng.uniform(0.008, 0.014))

    def relax_pointer(self, snapshot, reason, chance=0.75):
        if self.rng.random() >= chance:
            return False
        if reason != 'after-click' and time.monotonic() - self.last_pointer_motion < 1.5:
            return False
        region = snapshot['overlays'][-1] if snapshot.get('overlays') else snapshot['document']
        if min(region['right'] - region['left'], region['bottom'] - region['top']) < 20:
            return False
        x, y = rest_point(region, self.rng)
        current_x, current_y = self.pointer.pointer_position(self.api)
        if math.hypot(x - current_x, y - current_y) < 25:
            return False
        hit = self.api.WindowFromPoint(wintypes.POINT(x, y))
        if not hit or self.api.GetAncestor(hit, 2) != self.hwnd:
            return False
        self.move(x, y, purpose='rest', target_width=160)
        self.log('pointer-rest', reason=reason, x=x, y=y)
        return True

    def request(self, command, **data):
        self.check()
        request = {'command': command, **data}
        self.bridge.stdin.write(json.dumps(request) + '\n')
        self.bridge.stdin.flush()
        timeout = time.monotonic() + 18
        while True:
            self.check()
            try:
                line = self.responses.get(timeout=0.08)
                if line is None:
                    raise RuntimeError('The accessibility helper exited; see helper-stderr.log.')
                result = json.loads(line)
                if not result.get('ok'):
                    raise RuntimeError(result.get('error', 'Accessibility lookup failed.'))
                return result['data']
            except queue.Empty:
                if time.monotonic() >= timeout:
                    raise StopRequested('Accessibility lookup timed out; stopping instead of clicking blindly.')

    def find_window(self):
        matches = []
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

        @callback_type
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            self.api.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(128)
            if pid.value == self.browser.pid and self.api.IsWindowVisible(hwnd):
                self.api.GetClassNameW(hwnd, name, len(name))
                if name.value == 'Chrome_WidgetWin_1':
                    rect = wintypes.RECT()
                    self.api.GetWindowRect(hwnd, ctypes.byref(rect))
                    matches.append(((rect.right - rect.left) * (rect.bottom - rect.top), hwnd))
            return True
        self.api.EnumWindows(visit, 0)
        return max(matches)[1] if matches else None

    def enable_accessibility(self):
        # Chromium's documented assistive-technology handshake. Some builds need
        # this renderer request even when accessibility was enabled at launch.
        callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        renderers = []

        @callback_type
        def visit(hwnd, _):
            name = ctypes.create_unicode_buffer(128)
            self.api.GetClassNameW(hwnd, name, len(name))
            if name.value == 'Chrome_RenderWidgetHostHWND':
                renderers.append(hwnd)
                result = ctypes.c_size_t()
                self.api.SendMessageTimeoutW(hwnd, 0x003D, 0, 1, 0x0002, 750, ctypes.byref(result))
                if hwnd not in self.accessible_objects:
                    iid = (ctypes.c_ubyte * 16).from_buffer_copy(
                        uuid.UUID('618736e0-3c3d-11cf-810c-00aa00389b71').bytes_le)
                    accessible = ctypes.c_void_p()
                    try:
                        self.oleacc.AccessibleObjectFromWindow(hwnd, 0xFFFFFFFC, iid, ctypes.byref(accessible))
                        if accessible.value:
                            self.accessible_objects[hwnd] = accessible
                    except OSError:
                        pass
            return True
        self.api.EnumChildWindows(self.hwnd, visit, 0)
        return renderers

    def launch(self):
        self.check(focus=False)
        try:
            self.pointer.pointer_position(self.api)
        except OSError as error:
            raise StopRequested('Windows denied cursor access. Run the launcher inside the connected, unlocked desktop you intend to control.') from error
        candidates = [Path(os.environ.get(root, '')) / suffix for root, suffix in (
            ('ProgramFiles', 'Google/Chrome/Application/chrome.exe'),
            ('ProgramFiles(x86)', 'Google/Chrome/Application/chrome.exe'),
            ('LOCALAPPDATA', 'Google/Chrome/Application/chrome.exe'),
        )]
        chrome = next((p for p in candidates if p.is_file()), None)
        if chrome is None:
            raise RuntimeError('Google Chrome was not found.')
        print(f'Logs: {self.run_dir}', flush=True)
        print(f'Opening a separate Chrome test window in {self.args.countdown:g}s. F9/F12 stop.', flush=True)
        self.pause(self.args.countdown, focus=False)
        foreground_pid = wintypes.DWORD()
        self.api.GetWindowThreadProcessId(self.api.GetForegroundWindow(), ctypes.byref(foreground_pid))
        self.log('desktop-context', current_integrity=process_integrity(os.getpid()),
                 foreground_integrity=process_integrity(foreground_pid.value))
        # Give the new browser an unobstructed desktop, matching the standalone
        # Show Desktop experiment. Chromium may hide an occluded renderer's tree.
        if self.api.GetForegroundWindow() and not self.pointer.desktop_visible(self.api):
            self.pointer.press_win_d(self.api, self.rng)
            self.pause(0.8, focus=False)
        self.browser = subprocess.Popen([
            str(chrome), f'--user-data-dir={self.run_dir / "chrome-profile"}',
            '--no-first-run', '--no-default-browser-check', '--disable-sync',
            '--force-renderer-accessibility=complete', '--enable-features=UiaProvider',
            '--new-window', self.args.url,
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        finish = time.monotonic() + 25
        while not self.hwnd and time.monotonic() < finish:
            self.pause(0.1, focus=False)
            self.hwnd = self.find_window()
        if not self.hwnd:
            raise RuntimeError('The dedicated Chrome window did not appear within 25 seconds.')
        self.api.ShowWindow(self.hwnd, 3)
        self.api.SetForegroundWindow(self.hwnd)
        # Chrome may take foreground asynchronously while creating its first tab.
        self.pause(1, focus=False)
        focus_deadline = time.monotonic() + 2
        while self.api.GetForegroundWindow() != self.hwnd and time.monotonic() < focus_deadline:
            self.api.SetForegroundWindow(self.hwnd)
            self.pause(0.25, focus=False)
        if self.api.GetForegroundWindow() != self.hwnd:
            # A click on the empty central title-bar area activates this fresh,
            # single-tab window. First confirm the point belongs to this window.
            rect = wintypes.RECT()
            self.api.GetWindowRect(self.hwnd, ctypes.byref(rect))
            x, y = round((rect.left + rect.right) / 2), rect.top + 16
            hit = self.api.WindowFromPoint(wintypes.POINT(x, y))
            if hit and self.api.GetAncestor(hit, 2) == self.hwnd:
                self.log('activate-browser', message='Clicking the visible test window title bar.')
                self.pointer.move_pointer(self.api, (x, y), 0.8, self.rng)
                self.pointer.left_click(self.api, self.rng)
                self.pause(0.5, focus=False)
        self.log('browser-window', hwnd=self.hwnd, process=self.browser.pid,
                 latest_window=self.find_window(), foreground=self.api.GetForegroundWindow())
        self.check()
        self.log('accessibility-start', renderer_windows=self.enable_accessibility())
        powershell = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        self.bridge_errors = (self.run_dir / 'helper-stderr.log').open('w', encoding='utf-8')
        self.bridge = subprocess.Popen([
            str(powershell), '-NoLogo', '-NoProfile', '-NonInteractive', '-MTA',
            '-File', str(HERE / 'accessibility.ps1'), '-BrowserWindow', str(self.hwnd),
        ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.bridge_errors,
            text=True, encoding='utf-8', errors='replace', bufsize=1,
            creationflags=subprocess.CREATE_NO_WINDOW)

        def read_responses():
            for line in self.bridge.stdout:
                self.responses.put(line)
            self.responses.put(None)
        threading.Thread(target=read_responses, daemon=True).start()

    def snapshot(self):
        finish = time.monotonic() + 12
        last_error = None
        while time.monotonic() < finish:
            try:
                snapshot = self.request('snapshot', renderer_windows=self.enable_accessibility(),
                                        expected_scheme=urlsplit(self.last_url).scheme)
                if not snapshot.get('url'):
                    self.pause(0.35)
                    continue
                if not allowed_url(snapshot['url'], self.scope):
                    raise StopRequested(f'The browser reached an excluded URL: {snapshot["url"]!r}')
                for control in snapshot.get('controls', []):
                    if control['type'] == 'Hyperlink' and control.get('value'):
                        control['value'] = urljoin(snapshot['url'], control['value'])
                self.last_url = snapshot['url']
                (self.run_dir / 'last-snapshot.json').write_text(json.dumps(snapshot, indent=2), encoding='utf-8')
                return snapshot
            except RuntimeError as error:
                last_error = error
                self.enable_accessibility()
                self.pause(0.35)
        raise RuntimeError(f'The page did not expose a stable accessible document: {last_error}')

    def wait_initial(self):
        finish = time.monotonic() + 25
        last_error = None
        while time.monotonic() < finish:
            try:
                result = self.snapshot()
                if result.get('controls'):
                    return result
            except RuntimeError as error:
                last_error = error
            self.pause(0.5)
        raise RuntimeError(f'The initial page did not expose controls: {last_error}')

    def click(self, snapshot, control):
        self.check()
        fresh = self.request('probe', frame=snapshot['frame'], id=control['id'], name=control['name'])
        self.verify_click_policy(snapshot, control, fresh)
        doc = snapshot['document']
        # Browser tabs live above the web document; all other targets stay inside it.
        region = fresh if control in snapshot.get('tabs', []) else {
            'left': max(fresh['left'], doc['left']), 'top': max(fresh['top'], doc['top']),
            'right': min(fresh['right'], doc['right']), 'bottom': min(fresh['bottom'], doc['bottom']),
        }
        if region['right'] - region['left'] < 4 or region['bottom'] - region['top'] < 4:
            raise RuntimeError('The target is outside the visible document.')
        x, y = click_point(region, self.rng)
        self.request('probe', frame=snapshot['frame'], id=control['id'], name=control['name'], x=x, y=y)
        self.move(x, y, target_width=min(region['right'] - region['left'], region['bottom'] - region['top']))
        self.pause(self.rng.uniform(0.07, 0.28) if self.rng.random() < 0.88 else self.rng.uniform(0.4, 0.9))
        fresh = self.request('probe', frame=snapshot['frame'], id=control['id'], name=control['name'], x=x, y=y)
        self.verify_click_policy(snapshot, control, fresh)
        actual_x, actual_y = self.pointer.pointer_position(self.api)
        if max(abs(actual_x - x), abs(actual_y - y)) > 2:
            raise StopRequested(f'The pointer moved before the click: expected ({x}, {y}), found ({actual_x}, {actual_y}).')
        if (actual_x, actual_y) != (x, y):
            # RDP scaling can round a cursor position by a pixel or two. Verify
            # the actual position still hits this exact element before clicking.
            fresh = self.request('probe', frame=snapshot['frame'], id=control['id'], name=control['name'],
                                 x=actual_x, y=actual_y)
            self.verify_click_policy(snapshot, control, fresh)
            x, y = actual_x, actual_y
        hit = self.api.WindowFromPoint(wintypes.POINT(x, y))
        if not hit or self.api.GetAncestor(hit, 2) != self.hwnd:
            raise RuntimeError('Another window covers the click target.')
        self.check()
        self.pointer.send_button(self.api, 0x0002)
        try:
            self.pause(self.rng.uniform(0.06, 0.11))
        finally:
            self.pointer.send_button(self.api, 0x0004)
        self.pause(self.rng.uniform(0.06, 0.25))
        if not control.get('expanded') and control['type'] != 'MenuItem':
            self.relax_pointer(snapshot, reason='after-click', chance=0.78)

    def verify_click_policy(self, snapshot, selected, fresh):
        if fresh['type'] != selected['type']:
            raise RuntimeError('The target control type changed before clicking.')
        if selected['type'] == 'Hyperlink':
            value = fresh.get('value', '')
            target = urljoin(snapshot['url'], value) if value else ''
            expected = urljoin(snapshot['url'], selected.get('value', ''))
            if not allowed_url(target, self.scope) or target != expected:
                raise RuntimeError('The link destination changed or left the browsing boundary before clicking.')

    def observe(self, before, clicked=None):
        self.pause(self.rng.uniform(0.25, 0.65))
        last = None
        stable = 0
        latest = before
        for _ in range(7):
            latest = self.snapshot()
            if last is None and clicked is None:
                self.relax_pointer(latest, reason='view-change', chance=0.55)
            elif last is None and clicked and (clicked.get('expanded') or clicked['type'] == 'MenuItem'):
                self.relax_pointer(latest, reason='expanded-content', chance=0.65)
            key = (latest['url'], signature(latest), tuple(o['id'] for o in latest['overlays']))
            stable = stable + 1 if key == last else 0
            outcome = classify(before, latest, clicked)
            if stable >= 1 and outcome != 'no-observable-change':
                return latest, outcome
            last = key
            self.pause(0.45)
        return latest, classify(before, latest, clicked)

    def new_page(self, snapshot):
        self.page_since = time.monotonic()
        self.page_dwell = self.rng.uniform(self.args.page_min, self.args.page_max)
        self.scroll_stalls = 0
        self.visits[canonical(snapshot['url'])] += 1
        self.log('page', url=snapshot['url'], planned_seconds=round(self.page_dwell, 1), title=snapshot['title'])

    def scroll(self, snapshot):
        doc = snapshot['document']
        overlays = snapshot.get('overlays', [])
        region = overlays[-1] if overlays else doc
        x, y = self.pointer.pointer_position(self.api)
        width, height = region['right'] - region['left'], region['bottom'] - region['top']
        in_content = (region['left'] + width * 0.20 < x < region['left'] + width * 0.86
                      and region['top'] + height * 0.17 < y < region['top'] + height * 0.85)
        reposition = not in_content or self.scroll_stalls >= 2 or self.rng.random() < 0.18
        if reposition:
            x, y = rest_point(region, self.rng)
        hit = self.api.WindowFromPoint(wintypes.POINT(x, y))
        if not hit or self.api.GetAncestor(hit, 2) != self.hwnd:
            raise RuntimeError('The scroll area is covered by another window.')
        if reposition:
            self.move(x, y, purpose='rest', target_width=160)
        plan = self.scroll_rhythm.plan(snapshot.get('scroll', -1), self.scroll_stalls)
        for burst in plan.bursts:
            self.log('scroll-burst', style=burst.style, direction='up' if burst.direction == 1 else 'down',
                     notches=burst.notches, message=f'{burst.style}: {burst.notches} notches')
            for step in burst.steps:
                self.check()
                px, py = self.pointer.pointer_position(self.api)
                hit = self.api.WindowFromPoint(wintypes.POINT(px, py))
                if not hit or self.api.GetAncestor(hit, 2) != self.hwnd:
                    raise RuntimeError('The pointer left the test browser before scrolling.')
                event = self.pointer.INPUT(type=0, data=self.pointer.INPUTUNION(
                    mi=self.pointer.MOUSEINPUT(mouseData=step.delta & 0xffffffff, dwFlags=0x0800)))
                if self.api.SendInput(1, ctypes.byref(event), ctypes.sizeof(event)) != 1:
                    raise RuntimeError('Windows rejected scrolling.')
                self.pause(step.delay)
            self.pause(burst.pause_after)
        if self.rng.random() < 0.25:
            self.relax_pointer(snapshot, reason='after-scroll', chance=1)
        self.pause(plan.reading_pause)
        after = self.snapshot()
        current = signature(after)
        old_scroll, new_scroll = snapshot.get('scroll', -1), after.get('scroll', -1)
        stalled = abs(old_scroll - new_scroll) < 0.01 if min(old_scroll, new_scroll) >= 0 else current == signature(snapshot)
        self.scroll_stalls = self.scroll_stalls + 1 if stalled else 0
        return after

    def pick(self, candidates):
        weights = []
        for item in candidates:
            target = item.get('value', '')
            key = target or item['name']
            weight = 1 / (1 + self.interactions[key]) ** 2
            if target.startswith(self.args.url):
                weight *= 2
            if target:
                weight /= 1 + self.visits[canonical(target)]
            weights.append(weight)
        return self.rng.choices(candidates, weights=weights, k=1)[0]

    def explore(self, snapshot):
        self.started = time.monotonic()
        self.deadline = self.started + self.args.seconds
        self.new_page(snapshot)
        self.relax_pointer(snapshot, reason='page-arrival', chance=0.85)
        while True:
            self.check()
            overlays = snapshot.get('overlays', [])
            candidates = [c for c in snapshot['controls'] if eligible(c, snapshot['url'], overlays, self.scope)]
            due = time.monotonic() - self.page_since >= self.page_dwell
            links = [c for c in candidates if c['type'] == 'Hyperlink']
            navigations = [c for c in links if canonical(c['value']) != canonical(snapshot['url'])] or links
            local = [c for c in candidates if c['type'] != 'Hyperlink'
                     or canonical(c['value']) == canonical(snapshot['url'])]
            # Links become the main choice once the sampled reading time expires.
            weights = (60, 15, 25) if self.scroll_rhythm.last_style == 'fast' else (32, 42, 26)
            action = self.rng.choices(['scroll', 'read', 'click'], weights)[0]
            if overlays:
                action = 'click' if candidates else 'dismiss'
            elif due:
                action = 'click' if navigations else 'back' if self.navigation_depth else 'scroll'
                if self.navigation_depth and self.rng.random() < 0.23:
                    action = 'back'
                if len(snapshot.get('tabs', [])) > 1 and self.rng.random() < 0.35:
                    action = 'close-tab'
            if action == 'read':
                seconds = (self.rng.triangular(0.8, 4.0, 1.4) if self.scroll_rhythm.last_style == 'fast'
                           else self.rng.triangular(2.0, 11.0, 4.0))
                self.relax_pointer(snapshot, reason='reading', chance=0.32)
                self.log('read', seconds=round(seconds, 2))
                self.pause(seconds)
                snapshot = self.snapshot()
                continue
            if action == 'scroll':
                try:
                    snapshot = self.scroll(snapshot)
                except RuntimeError as error:
                    self.log('target-skipped', message=str(error))
                    snapshot = self.snapshot()
                continue
            before = snapshot
            clicked = None
            if action == 'click':
                pool = candidates if overlays else navigations if due and navigations else local
                if not pool:
                    snapshot = self.scroll(snapshot)
                    continue
                clicked = self.pick(pool)
                try:
                    self.click(snapshot, clicked)
                except RuntimeError as error:
                    self.interactions[clicked.get('value') or clicked['name']] += 1
                    self.log('target-skipped', name=clicked['name'], message=str(error))
                    snapshot = self.snapshot()
                    continue
                self.interactions[clicked.get('value') or clicked['name']] += 1
                self.log('click', name=clicked['name'], target=clicked.get('value', ''))
            elif action == 'back':
                self.hotkey(0x12, 0x25)
                self.log('back')
            elif action == 'close-tab':
                self.hotkey(0x11, ord('W'))
                self.log('close-test-tab')
            else:
                self.hotkey(0x1B)
                self.log('dismiss-overlay')
            snapshot, outcome = self.observe(before, clicked)
            self.outcomes[outcome] += 1
            self.log('result', outcome=outcome, url=snapshot['url'])
            if outcome == 'new-tab':
                old_ids = {tab['id'] for tab in before['tabs']}
                added = [tab for tab in snapshot['tabs'] if tab['id'] not in old_ids]
                if added and not added[-1].get('selected'):
                    self.click(snapshot, added[-1])
                    snapshot, _ = self.observe(snapshot)
            if canonical(before['url']) != canonical(snapshot['url']):
                self.navigation_depth = max(0, self.navigation_depth - 1) if action == 'back' else self.navigation_depth + 1
                self.new_page(snapshot)
            elif outcome in ('content-changed', 'selection-changed', 'new-tab'):
                self.new_page(snapshot)
            self.pause(self.rng.uniform(1.0, 3.5) if outcome == 'no-observable-change' else self.rng.uniform(0.25, 1.6))
            snapshot = self.snapshot()

    def finish(self, status, message=''):
        elapsed = round(time.monotonic() - self.started, 2) if self.started else 0
        self.log('finished', status=status, message=message)
        summary = {'status': status, 'message': message, 'active_seconds': elapsed,
                   'requested_seconds': self.args.seconds, 'seed': self.args.seed,
                   'unique_pages': len(self.visits), 'page_visits': dict(self.visits),
                   'outcomes': dict(self.outcomes), 'events': str(self.run_dir / 'events.jsonl')}
        (self.run_dir / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
        print(f'Summary: {self.run_dir / "summary.json"}', flush=True)
        if self.bridge:
            self.bridge.terminate()
            try:
                self.bridge.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.bridge.kill()
        if self.bridge_errors:
            self.bridge_errors.close()
        if self.browser and self.started is None and self.browser.poll() is None:
            # Failed startup/inspection windows belong only to this fresh profile.
            self.browser.terminate()
            try:
                self.browser.wait(timeout=3)
            except subprocess.TimeoutExpired:
                pass
        self.log_file.close()
        for accessible in self.accessible_objects.values():
            table = ctypes.cast(accessible, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents
            ctypes.WINFUNCTYPE(wintypes.ULONG, ctypes.c_void_p)(table[2])(accessible)
        self.ole32.CoUninitialize()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default=START_URL)
    parser.add_argument('--prompt-url', action='store_true', help='ask for the starting website; Enter keeps the default')
    parser.add_argument('--seconds', type=float, default=600, help='active browsing duration (default: 600)')
    parser.add_argument('--page-min', type=float, default=18)
    parser.add_argument('--page-max', type=float, default=55)
    parser.add_argument('--countdown', type=float, default=8)
    parser.add_argument('--seed', type=int, default=None)
    parser.add_argument('--inspect', action='store_true', help='open the test window and save its accessible controls; no exploration')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Windows 10/11 is required.')
    if args.prompt_url:
        args.url = input(f'Website URL [{args.url}]: ').strip() or args.url
    if not allowed_url(args.url):
        parser.error('Use a valid HTTP(S) webpage URL. Authentication, mutation, and download targets are excluded.')
    if not all(math.isfinite(n) for n in (args.seconds, args.page_min, args.page_max, args.countdown)):
        parser.error('Time values must be finite.')
    if not 5 <= args.seconds <= 3600 or not 3 <= args.page_min <= args.page_max <= 180 or not 0 <= args.countdown <= 60:
        parser.error('seconds: 5-3600; page times: 3-180, min <= max; countdown: 0-60.')
    if args.seed is None:
        args.seed = random.SystemRandom().randint(1, 2 ** 31 - 1)
    explorer = Explorer(args)
    status, message, result = 'error', '', 1
    try:
        explorer.launch()
        snapshot = explorer.wait_initial()
        if args.inspect:
            (explorer.run_dir / 'inspection.json').write_text(json.dumps(snapshot, indent=2), encoding='utf-8')
            explorer.log('inspection', url=snapshot['url'], controls=len(snapshot['controls']), title=snapshot['title'])
            status, result = 'inspected', 0
        else:
            explorer.explore(snapshot)
    except BudgetEnded:
        status, result = 'completed', 0
        message = 'The active browsing time limit was reached.'
    except (StopRequested, KeyboardInterrupt) as error:
        status, message, result = 'stopped', str(error) or 'Stopped with Ctrl+C.', 130
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        message = str(error)
    finally:
        explorer.finish(status, message)
    return result


if __name__ == '__main__':
    sys.exit(main())
