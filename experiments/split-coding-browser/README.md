# Split coding / browsing sample

This separate desktop demonstration tests app switching and window placement while
typing a fixed code sample and navigating documentation through accessibility.

Double-click **start-split-demo.cmd** for one cycle:

1. VS Code and Chrome are placed in equal halves of the monitor that contained
   the mouse when the script started. Coding defaults to the left. VS Code opens
   **Desktop/Project for vehicle**, with the project shown in its Explorer sidebar.
2. For **60 seconds**, a small Python example is typed into a new scratch file
   in an isolated VS Code window. The exact saved contents are verified.
3. For **60 seconds**, the mouse moves to Chrome's side and the existing
   accessibility browser explorer scrolls, reads, and follows documentation links.
4. Focus and the mouse return to VS Code. Both windows remain visible.

The countdown, app startup, initial accessibility checks, and phase switching
are additional to the two active minutes. The sample is typed and saved;
it is not executed. Neither app is maximized during the active phases.

```powershell
python experiments/split-coding-browser/split_demo.py
python experiments/split-coding-browser/split_demo.py --coding-side right
python experiments/split-coding-browser/split_demo.py --coding-side random
python experiments/split-coding-browser/split_demo.py --folder "C:\Users\hinatardp\Desktop\Class Demo"
python experiments/split-coding-browser/split_demo.py --url "https://docs.python.org/3/tutorial/"
```

**F9/F12 or Ctrl+C stop the demo.** Keep both windows in place and avoid other
mouse/keyboard use while it runs. Unexpected focus or layout changes stop input.
Keep the Windows/RDP desktop connected and unlocked.

VS Code and Chrome must be installed. Python's standard library and built-in
Windows accessibility APIs are used; no pip packages are needed. The script
imports the sibling `browser-explorer` and `taskbar-pointer-demo` prototypes.
The existing C workflows and standalone browser script are unchanged.

Fresh Chrome/VS Code profiles and `split_sample.py` are created inside the printed
run directory under `.tmp/browser-explorer/`. Existing editor files, settings,
extensions, and browser sessions are not changed. The selected project is opened
for viewing; the typed sample is an external file in the demo's run directory.
The isolated editor suppresses the workspace trust startup prompt and leaves
the project in Restricted Mode. Setup briefly shows the desktop
and opens the new apps before applying the split layout.

`split-summary.json` records each phase's duration, the saved file, side, window
rectangles, and whether focus returned to coding. `events.jsonl` includes phase
labels. `summary.json` is the inherited browser report; use `split-summary.json`
for the combined demonstration.

The layout excludes the taskbar and accounts for invisible window borders. A
work area of at least 1040x550 is required. The keyboard is sent to the verified
scratch editor only; browser actions use the browser's current accessibility
controls. There is one input controller, so coding and browsing never type or
click simultaneously.

Plan checks:

```powershell
python -m unittest discover -s experiments/split-coding-browser -p test_split_demo.py
```

Validated on this desktop with a full 60-second coding phase and 60-second
browsing phase: the saved scratch file matched the sample, Chrome followed
documentation links, the split layout remained intact, and focus returned to
VS Code. The five layout, timing, and sample checks also passed.
