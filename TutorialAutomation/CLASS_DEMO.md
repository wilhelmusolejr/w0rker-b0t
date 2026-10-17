# Class demo

A short classroom version of the reference job. It types two small teaching programs into VS Code at a readable pace, and in between it visits professors' and educators' GitHub projects and the official Python and MDN JavaScript docs. A full run takes about **6-9 minutes**.

## Run

Double-click `start-class-demo.cmd` in the `worken` folder, or send `/run` from Telegram after pointing `launcher` in `build\notify.json` at `../../start-class-demo.cmd` (see [TELEGRAM.md](TELEGRAM.md)).

Check it without sending input:

```powershell
.\TutorialAutomation\build\tutorial.exe --dry-run-config .\TutorialAutomation\scripts\class-demo.json
```

Output goes to `C:\Users\hinatardp\Desktop\Class Demo` (the folder must exist). Finished files are skipped on the next run; to start over, delete the files in that folder and `logs\class-demo.state`.

## Files

| Path | Purpose |
|---|---|
| `start-class-demo.cmd` | Launcher in `worken` |
| `scripts/class-demo.json` | Config (version 2, same format as `job.json`) |
| `scripts/class-demo/grades.py` | Typed first: dictionaries, functions, `sorted` with a lambda, f-strings |
| `scripts/class-demo/shopping-list.cjs` | Typed second: arrays of objects, `map`/`filter`/`reduce`, template strings |
| `scripts/class-demo/links.txt` | GitHub projects for browser visits |

Both programs run as typed: `python grades.py` and `node shopping-list.cjs`. The JavaScript file uses `.cjs` because the player only types `.py` and `.cjs` files.

## Timing

| Setting | Value |
|---|---|
| Typing | 60-120 ms per character, 0.4-0.9 s per new line |
| Pauses | 2.5-5 s every 300-500 characters |
| Browser visits | after every 90-150 s of coding, 45-75 s each, usually 1-2 per run |
| Visit mix | GitHub list 40%, Python docs 30%, JavaScript docs 30%, no search research |
| Auto-restart | Off: an interruption stops the demo cleanly |

| Step | Approx. time |
|---|---|
| Countdown | 10 s |
| Folder, `cmd`, `code .`, VS Code settings check | 1.5-2 min |
| `grades.py` (998 chars) | ~2 min |
| `shopping-list.cjs` (789 chars) | ~1.5 min |
| File creation and save checks | ~30 s |
| Browser visits | 1-2 min |

F8 or Telegram `/pause` pauses at any time, and paused time is not counted. F9, F12, or `/stop` ends the demo.

## Browsing choices

**GitHub (from `links.txt`).** Edit the file to change them; any `Name: https://...` line works.

- Think Python: Allen Downey (Olin College)
- Python for Everybody: Charles Severance (University of Michigan)
- CS50: David Malan (Harvard)
- xv6 teaching operating system (MIT 6.1810)
- LLMs from Scratch: Sebastian Raschka
- Pytudes: Peter Norvig
- micrograd: Andrej Karpathy
- Python Data Science Handbook: Jake VanderPlas
- Eloquent JavaScript: Marijn Haverbeke
- You Don't Know JS: Kyle Simpson

**Docs (`docs_topics` in `class-demo.json`).** Each one searches Google and then opens the official page. URLs must start with `https://docs.python.org/3/` or `https://developer.mozilla.org/en-US/docs/Web/JavaScript/`.

- Python tutorial: introduction, data structures, functions, classes, errors
- MDN JavaScript guide: functions, indexed collections, loops, promises, classes

## Showing it through RustDesk

- Connect in **view-only** mode. A click from the viewer moves focus away from VS Code and stops playback.
- Turn off **Lock after session end** in RustDesk's security settings and do not use privacy mode.
- Use **Best quality** and 30+ fps, and zoom VS Code (Ctrl+=) so code is readable on a projector.
- Close anything that might show a UAC prompt; the player cannot reach the secure desktop.
