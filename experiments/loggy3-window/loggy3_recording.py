"""Read Loggy3 recording status and perform state-aware Start/Stop UI actions."""

import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

from loggy3_window import LoggyWindow


HERE = Path(__file__).resolve().parent


def recording_state(snapshot):
    """Use the current recording button, never old session-history text."""
    buttons = [b for b in snapshot.get('buttons', [])
               if not b.get('offscreen') and 'btn-record' in b.get('class', '').split()]
    if len(buttons) != 1:
        return {'state': 'unknown', 'reason': f'Expected one visible recording control; found {len(buttons)}.'}
    button = buttons[0]
    if not button.get('enabled'):
        return {'state': 'busy', 'button': button, 'reason': 'The recording control is disabled or transitioning.'}
    label = ' '.join(button.get('name', '').split()).casefold()
    state = {'start': 'stopped', 'start recording': 'stopped',
             'stop': 'recording', 'stop recording': 'recording'}.get(label, 'unknown')
    return {'state': state, 'button': button, 'timers': snapshot.get('timers', []),
            'reason': 'Current recording control: ' + button.get('name', '')}


def action_plan(action, snapshot):
    status = recording_state(snapshot)
    if action == 'status':
        return {**status, 'action': 'none'}
    desired = {'record': 'recording', 'stop': 'stopped'}[action]
    if status['state'] == desired:
        return {**status, 'action': 'none', 'desired': desired, 'reason': f'Already {desired}; no action needed.'}
    if status['state'] not in ('stopped', 'recording'):
        raise RuntimeError(status['reason'] + ' No recording action was sent.')
    if not status['button'].get('invoke_supported'):
        raise RuntimeError('The recording control does not expose Invoke; no action was sent.')
    return {**status, 'action': 'invoke', 'desired': desired,
            'expected_name': status['button']['name']}


class RecordingUI:
    def __init__(self, window, hwnd, read_only=False):
        self.window = window
        self.hwnd = hwnd
        self.read_only = read_only

    def request(self, action='snapshot', expected_name='', target_id='', x=None, y=None):
        if self.read_only and action not in ('snapshot', 'probe'):
            raise RuntimeError('Hover preview cannot invoke recording controls.')
        self.window.check()
        if not self.window.api.IsWindow(self.hwnd):
            raise RuntimeError('The Loggy3 main window closed.')
        if action == 'invoke' and self.window.api.GetForegroundWindow() != self.hwnd:
            raise RuntimeError('Loggy3 lost focus; no recording action was sent.')
        powershell = Path(os.environ.get('SystemRoot', r'C:\Windows')) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        command = [str(powershell), '-NoLogo', '-NoProfile', '-NonInteractive', '-MTA',
                   '-File', str(HERE / 'recording_ui.ps1'), '-WindowHandle', str(self.hwnd), '-Action', action]
        if expected_name:
            command += ['-ExpectedName', expected_name]
        if target_id:
            command += ['-TargetId', target_id]
        if x is not None and y is not None:
            command += ['-X', str(x), '-Y', str(y)]
        result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8-sig',
                                errors='replace', timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            raise RuntimeError('Accessibility helper failed: ' + result.stderr.strip())
        response = json.loads(result.stdout.strip())
        if not response.get('ok'):
            raise RuntimeError(response.get('error', 'Accessibility lookup failed.'))
        return response

    def read(self):
        finish = time.monotonic() + 10
        latest = None
        while time.monotonic() < finish:
            latest = self.request()
            if recording_state(latest)['state'] != 'unknown':
                return latest
            self.window.pause(0.3)
        return latest

    def run(self, action, dry_run=False):
        dry_run = dry_run or self.read_only
        plan = action_plan(action, self.read())
        if action == 'status' or plan['action'] == 'none' or dry_run:
            return {**plan, 'dry_run': dry_run}
        # The helper reads the live control again and checks the expected label
        # immediately before Invoke. It is sent once; a timeout never retries it.
        self.request('invoke', plan['expected_name'])
        finish = time.monotonic() + 20
        while time.monotonic() < finish:
            self.window.pause(0.35)
            status = recording_state(self.request())
            if status['state'] == plan['desired']:
                return {**status, 'action': 'invoked', 'desired': plan['desired'], 'dry_run': False}
        raise RuntimeError('The requested recording state was not confirmed. Check Loggy3 before retrying; no second click was sent.')

    def hover(self, plan, seconds):
        if not self.read_only:
            raise RuntimeError('Hover preview requires read-only mode.')
        if plan['action'] == 'none':
            return {**plan, 'hovered': False}
        current = recording_state(self.read())
        if current['state'] != plan['state'] or current['button']['name'] != plan['expected_name']:
            raise RuntimeError('Recording state changed before the cursor preview.')
        button = current['button']
        x, y = round((button['left'] + button['right']) / 2), round((button['top'] + button['bottom']) / 2)
        self.request('probe', button['name'], button['id'], x, y)
        print(f"Moving to {button['name']!r} at ({x}, {y}). Hover only: no click or Invoke.", flush=True)
        self.window.move(x, y)
        px, py = self.window.pointer.pointer_position(self.window.api)
        self.request('probe', button['name'], button['id'], px, py)
        self.window.pause(seconds)
        after = recording_state(self.read())
        return {**plan, 'action': 'hover', 'hovered': True, 'dry_run': True,
                'state_after_hover': after['state'], 'recording_action_sent': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('status', 'record', 'stop'), default='status', nargs='?')
    parser.add_argument('--dry-run', action='store_true', help='show the planned recording action without invoking it')
    parser.add_argument('--hover-only', action='store_true', help='move to the recording button without clicking or invoking it')
    parser.add_argument('--hover-seconds', type=float, default=5)
    parser.add_argument('--minimize-after', action='store_true')
    parser.add_argument('--countdown', type=float, default=3)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Windows 10/11 is required.')
    if not math.isfinite(args.countdown) or not 0 <= args.countdown <= 60:
        parser.error('countdown must be between 0 and 60 seconds')
    if not math.isfinite(args.hover_seconds) or not 0 <= args.hover_seconds <= 60:
        parser.error('hover-seconds must be between 0 and 60 seconds')
    if args.hover_only and args.action == 'status':
        parser.error('Use record or stop with --hover-only.')
    window = LoggyWindow()
    label = f'{args.action} hover preview' if args.hover_only else f'{args.action} dry run' if args.dry_run else args.action
    print(f'{label} in {args.countdown:g}s. Esc/F9/F12 stop.', flush=True)
    window.pause(args.countdown)
    hwnd = window.show()
    ui = RecordingUI(window, hwnd, read_only=args.hover_only)
    result = ui.run(args.action, args.dry_run)
    if args.hover_only:
        result = ui.hover(result, args.hover_seconds)
    print(json.dumps(result, indent=2), flush=True)
    if args.minimize_after:
        window.minimize(hwnd)
    return 0 if result['state'] in ('recording', 'stopped') else 2


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print('\nStopped.', file=sys.stderr)
        sys.exit(130)
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(f'Error: {error}', file=sys.stderr)
        sys.exit(1)
