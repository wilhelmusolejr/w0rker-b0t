# Ten-minute browser exploration experiment

This UI navigation experiment discovers controls through Windows accessibility,
selects browsing actions, and logs the resulting page or control changes. Varied
cursor paths, scroll amounts, and pauses provide configurable playback scenarios.

Double-click **start-browser-test.cmd** and enter the website URL. Press Enter
to use **https://docs.python.org/3/tutorial/**. The script opens that website in
a fresh Chrome profile, then explores for **600 seconds**. It accepts HTTP and
HTTPS webpage addresses; there is no Python/GitHub website list. No packages need
installing: it uses Python 3, Windows APIs,
and Windows' built-in PowerShell/.NET UI Automation client. The experiment imports
the input definitions from the neighboring `taskbar-pointer-demo` folder.
The existing C player is not modified.

Keep the Windows/RDP desktop connected and unlocked. After starting, leave the
test Chrome window in front and avoid moving the mouse or changing focus.
**F9**, **F12**, or **Ctrl+C** stop the run. The script stops if its test window
loses focus, the session disconnects/locks, or a target cannot be verified.

From the workspace root:

```powershell
python experiments/browser-explorer/browser_explorer.py
```

Useful variations:

```powershell
# Supply any website without editing the script; replace this example URL.
python experiments/browser-explorer/browser_explorer.py --url "https://example.com/"

# Ask for the website at startup.
python experiments/browser-explorer/browser_explorer.py --prompt-url

# Same random choices on a repeat run (page layout/network timing can still differ).
python experiments/browser-explorer/browser_explorer.py --seed 17

# Faster smoke test.
python experiments/browser-explorer/browser_explorer.py --seconds 45 --page-min 5 --page-max 9

# Override the randomly sampled time per page.
python experiments/browser-explorer/browser_explorer.py --page-min 20 --page-max 65

# Open the test page, save its accessibility inspection, and exit without exploring.
python experiments/browser-explorer/browser_explorer.py --inspect
```

## Visible behavior

After an eight-second countdown, the script shows the desktop with Win+D if
needed, launches a separate Chrome profile, and maximizes its window. If Windows
declines foreground activation, it can click the unobstructed central title-bar
area after checking that the point belongs to the new single-tab test window.
The title-bar focus step uses that window's current bounds; webpage targets use
accessibility element names, states, and rectangles.

The ten-minute timer begins once the initial page exposes its controls. Startup
and the countdown are additional. During browsing:

- Each page/view gets a new target reading time, sampled from **18–55 seconds**.
  These are approximate targets, not exact departure times: waiting for changes,
  reading, and finishing an interaction can extend a visit.
- It mixes reading pauses, wheel scrolls, local controls, links, and occasional
  Back actions. Short cursor moves are quick; longer moves have varied paths and
  asymmetric acceleration/deceleration. Click locations vary within the control.
  It favors less-used links to broaden coverage.
- After ordinary clicks it often moves away into the content, using a different
  resting point each time. Sometimes it stays put. It also occasionally adjusts
  the pointer while reading, and usually keeps an already suitable position for
  scrolling. Expandable controls wait for fresh menu/section information before
  any move away, so a newly opened menu is not immediately abandoned.
- Scroll passes contain 2-4 bursts, mixing **fast scans of 9-20 wheel notches**
  with **slow bursts of 1-3 notches**. Fast bursts build and reduce speed, while
  slow bursts and reading pauses interrupt the scanning. The order, amount,
  timing, and occasional short look back vary. Page ends reverse the direction.
  These are adjustable visual approximations, not a validated human-motion model.
- The supplied URL defines the website boundary. It accepts relative links,
  extensionless paths, query-based pages, section anchors, HTTP-to-HTTPS upgrades,
  and the host's normal `www` counterpart. Other subdomains and outside websites
  are excluded. A custom port stays restricted to that port. A server redirect
  beyond the boundary stops the run when the new address is observed.
- It discovers navigation links and controls from the live accessibility tree.
  Tabs, menus, expandable sections, and buttons exposed inside navigation regions
  do not need website-specific labels. It also recognizes common browsing button
  labels. Exposed form controls, downloads, authentication, and recognizable
  account-changing actions are excluded. These generic filters can skip some
  legitimate content; they are not a guarantee of understanding every control.
- Before clicking, it refreshes the element's rectangle and link destination, checks accessibility
  hit-testing at the chosen point, moves there, and checks again. Coordinates
  are read at runtime; no screenshot processing, OCR, or AI is used.
- Before/after snapshots distinguish navigation, anchors, new tabs, open/closed
  overlays, expanded/collapsed controls, selection changes, same-URL content
  changes, and no observable change. The result is logged as an observation,
  not as a guarantee that the website intended that action.
- Open overlays restrict candidate clicks to their descendants. Escape can
  dismiss an overlay with no eligible controls. It never clicks the obscured
  background page while treating an overlay as active.
- At the time limit it stops issuing browsing input and writes a summary. A
  brief final cleanup may finish afterward. The test browser stays open.

## Logs and limits

Every run prints its directory under `.tmp/browser-explorer/<timestamp>-<pid>/`:

- `events.jsonl`: action times, targets, observed outcomes, and page residence targets.
- `summary.json`: status, actual active time, seed, visited pages, and outcome counts.
- `last-snapshot.json`: the most recent accessible page/control state.
- `helper-stderr.log`: accessibility helper diagnostics.
- `inspection.json`: saved only for `--inspect`.
- `chrome-profile/`: the separate test browser profile.

`pointer-rest` events identify movement away from a target. `scroll-burst` events
record fast/slow style, direction, and amount, so the new pacing can be reviewed.
All moves and wheel events still obey the desktop, foreground, and time guards.

Chrome is launched with full renderer accessibility and native UI Automation
enabled. A Windows accessibility handshake and direct renderer-fragment lookup
handle builds that do not attach the webpage tree beneath the toolbar tree.
The helper runs persistently, so a new PowerShell process is not created for
every action.

The page must expose usable accessibility information. Custom controls can be
unlabeled, overlays can be unmarked, and dynamic content can be ambiguous. An
unrecognized change is recorded as such. A new browser window or unrelated
foreground dialog stops the test; this prototype handles tabs inside its own
window but does not take over other windows. Old element identities are not
assumed to survive navigation. It does not bypass login or verification screens.

Run the meaningful policy/outcome checks from the workspace root:

```powershell
python -m unittest discover -s experiments/browser-explorer -p test_browser_explorer.py
```

## Validation

On 2026-10-24, the full run completed **600.03 active seconds**, visiting **7
distinct pages** and recording **9 page navigations** and **12 section jumps**.
It also scrolled, paused, and used Back. A final 35-second smoke test verified
the updated text/heading inspection and scroll tracking, navigated successfully,
and stopped at its limit. All **8 policy and outcome tests** passed.

The generalized version passed **16 policy and outcome checks** and a live local
fixture covering real section expansion, modal opening/closing, relative links,
JavaScript navigation buttons, and exclusion of exposed form actions/outside links.
Run that deterministic Windows integration check with:

```powershell
python experiments/browser-explorer/verify_accessibility.py
```

The local fixture starts a temporary loopback web server and its own Chrome
window, then closes both. New-tab classification has automated test coverage but
has not been exercised live here.

The revised movement/scrolling planner additionally has automated coverage for
distance-dependent timing, exact cursor destinations, varied interior resting
positions, bounded click variation, mixed scroll speeds, and page-end recovery.
The current suite passes **23 checks**. The live fixture also passed with the
new movement. A **90.07-second** browsing run completed across **3 pages**, logging
fast/slow wheel bursts, three moves away after clicks, and a reading-position
adjustment. The normal launcher still defaults to ten minutes.
The latest full-run report is
`.tmp/browser-explorer/20261024-053611-13624/summary.json` from the workspace root.
