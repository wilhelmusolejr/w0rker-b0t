"""Live, deterministic checks against the local accessible HTML fixtures."""

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import threading

from browser_explorer import Explorer, StopRequested, eligible


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def handle(self):
        try:
            super().handle()
        except (ConnectionResetError, BrokenPipeError):
            # Chrome closes speculative/keep-alive sockets when the fixture exits.
            pass


def named(snapshot, name):
    matches = [item for item in snapshot['controls'] if item['name'] == name]
    if len(matches) != 1:
        raise AssertionError(f'Expected one {name!r} control; got {len(matches)}.')
    return matches[0]


def main():
    handler = partial(QuietHandler, directory=str(Path(__file__).parent / 'fixtures'))
    server = ThreadingHTTPServer(('127.0.0.1', 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = f'http://127.0.0.1:{server.server_port}/index.html'
    args = argparse.Namespace(url=url, seconds=120, seed=7, countdown=0,
                              page_min=5, page_max=9, inspect=False)
    explorer = Explorer(args)
    status, message = 'error', 'Fixture verification did not complete.'
    try:
        explorer.launch()
        snapshot = explorer.wait_initial()
        for name in ('Outside website', 'Delete record', 'Open form view'):
            control = named(snapshot, name)
            assert not eligible(control, snapshot['url'], snapshot['overlays'], explorer.scope), name
        assert named(snapshot, 'Open form view')['in_form'], 'The accessible form context was not detected.'
        assert eligible(named(snapshot, 'Chapters'), snapshot['url'], (), explorer.scope), 'Navigation button was not recognized.'
        control = named(snapshot, 'Show details')
        explorer.click(snapshot, control)
        snapshot, outcome = explorer.observe(snapshot, control)
        assert outcome in ('section-state-changed', 'content-changed'), outcome
        explorer.log('fixture-expansion', outcome=outcome)
        assert named(snapshot, 'Read expanded section')

        control = named(snapshot, 'Open dialog')
        explorer.click(snapshot, control)
        snapshot, outcome = explorer.observe(snapshot, control)
        assert outcome == 'overlay-opened', outcome
        close = named(snapshot, 'Close dialog')
        assert close['scope'], 'The modal close button has no dialog scope.'
        assert eligible(close, snapshot['url'], snapshot['overlays'], explorer.scope)
        explorer.log('fixture-modal-open', outcome=outcome)
        explorer.click(snapshot, close)
        snapshot, outcome = explorer.observe(snapshot, close)
        assert outcome == 'overlay-closed', outcome
        explorer.log('fixture-modal-close', outcome=outcome)

        control = named(snapshot, 'Read next chapter')
        explorer.click(snapshot, control)
        snapshot, outcome = explorer.observe(snapshot, control)
        assert outcome == 'navigation' and snapshot['url'].endswith('/next.html'), (outcome, snapshot['url'])
        explorer.log('fixture-relative-link', outcome=outcome)
        control = named(snapshot, 'Chapters')
        assert eligible(control, snapshot['url'], (), explorer.scope), control
        explorer.click(snapshot, control)
        snapshot, outcome = explorer.observe(snapshot, control)
        assert outcome == 'navigation' and snapshot['url'].endswith('/index.html'), (outcome, snapshot['url'])
        explorer.log('fixture-navigation-button', outcome=outcome)
        status, message = 'verified', 'Live expansion, modal, relative-link, navigation-button, and form/boundary checks passed.'
        return 0
    except (AssertionError, RuntimeError, StopRequested, KeyboardInterrupt) as error:
        message = str(error)
        raise
    finally:
        explorer.finish(status, message)
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)


if __name__ == '__main__':
    raise SystemExit(main())
