# Windows Tutorial Automation

A native C11/Win32 player for prerecorded teaching demonstrations. Scripts drive an active, unlocked Windows desktop using `SendInput` and cursor APIs. All script syntax and external text files are checked before playback. Seeded presentation timing and scrolling make recordings reproducible.

**More docs:** [CLASS_DEMO.md](CLASS_DEMO.md) (short classroom run) · [TELEGRAM.md](TELEGRAM.md) (remote pause/resume/screenshot/start) · [CHANGELOG.md](CHANGELOG.md) (recent changes)

## Job settings: `scripts/job.json`

`start-reference-coding.cmd` now runs [scripts/job.json](scripts/job.json), a short hand-edited file (version 2). Source code is read straight from the folder; nothing is embedded or regenerated. Every setting is checked before any input is sent.

| Section | Setting | Meaning |
|---|---|---|
| `files` | `folder` | Source folder, relative to `job.json` (default `../../References/scripts`). Only `.py`/`.cjs` files are typed. |
| | `include` / `exclude` | Optional filename patterns with `*` and `?`, e.g. `"exclude": ["hidemium.cjs", "catalog-*"]`. |
| | `list` | Optional exact filenames; with `"order": "listed"` they are typed in that order. |
| | `order` | `alphabetical`, `listed`, or `shuffle`. A partly typed file is always finished first. |
| | `shuffle_seed` | `0` derives a fixed order from the destination, so resumes keep the same order. Change it to reshuffle. |
| `browser` | `launch` | `taskbar` (mouse to the pinned Chrome icon) or `run`. |
| | `coding_interval_seconds`, `visit_seconds` | Coding time between visits and visit length, each a `[min, max]` random range. |
| | `weights` | Relative chance of `research`, `url_list`, `python_docs`, `js_docs`; `0` turns one off. |
| | `url_list_file` | Text file of links; one is picked at random, never the same twice in a row. |
| | `research.queries_file` | One Google search per line. |
| | `research.templates` | `patterns` such as `"{brand} campervan hire {city}"`, expanded over every combination in `values`; `{brand2}` uses a different brand than `{brand}`. Up to 400 generated searches. |
| | `research.results_to_open` | `[1, 2]` reads one or two organic results per visit (max 3). |
| | `docs_topics` | Python/MDN documentation searches and pages. |
| `auto_restart` | `enabled`, `max_restarts`, `wait_seconds` | After an unexpected stop, waits, re-checks saved files, and resumes. Never restarts after F9, F12, or Ctrl+C. Waits while the desktop is locked. |

Typing and action timing use the same `typing` and `actions` sections described below. Check a job without input: `tutorial.exe --dry-run-config scripts\job.json` prints the file order, visit mix, and number of research searches. The older `reference-coding.json` (version 1) still works with `--config`.

## All reference Python and JavaScript files

From `C:\Users\hinatardp\Desktop\worken`, run:

```powershell
.\start-reference-coding.cmd
```

The launcher reads [scripts/reference-coding.json](scripts/reference-coding.json). It contains **45 files: 4 `.py` and 41 `.cjs`**, with their original filenames, exact source snapshots, and SHA-256 checksums. The destination is `C:\Users\hinatardp\Desktop\Project for vehicle`. Source code is entered through the VS Code UI; copied programs are not executed.

After a **10-second countdown**, playback shows the desktop, double-clicks the project folder, types `cmd` in Explorer's address bar, and types **`code .`** in Command Prompt. It verifies the project's Plain Text settings for literal entry and configures them through Settings if needed: indentation is `none`, accepting suggestions on Enter is `off`, and automatic closing brackets, closing quotes, and surrounding selections are `never`. It reads the saved settings back before entering source. This prevents VS Code from adding closing characters to an unfinished reference prefix at a save checkpoint. It creates each file with **Ctrl+N → Save As**, typing its full destination path and original filename into the native dialog. This pins the file to the project root even when Explorer has a subfolder selected. All commands, names, paths, source text, mouse movement, and clicks retain the normal slow presentation profile. The file list runs in JSON order; set an entry's `enabled` to `false` to omit it, or reorder entries.

The defaults are 140–300 ms per character, 900–2,200 ms per newline, 6–14 second pauses after 160–320 characters, 1.4–3 second action pauses, and 1–1.8 second mouse movement. With roughly 415,000 source characters, this is a long run that can span multiple days. There is no faster test profile in the launcher.

Before entering editor setup commands, playback waits for the expected VS Code command field, its exact current value, and keyboard focus within the same command widget. It also checks the language and line-ending pickers, then confirms the Plain Text and LF status buttons. An unavailable or unexpected command field stops setup before further text is sent.

After **300–600 seconds of coding**, playback saves and verifies a checkpoint and chooses randomly between **Python documentation, JavaScript documentation, and a page from the URL list**. Each available category has an equal chance; it then chooses a page within that category, avoiding an immediate repeat when alternatives exist. Documentation visits search Google, scroll the results briefly, and open the official page. URL-list visits open the selected link directly. Both scroll with reading pauses and return to the same editor after the **120–180 second visit budget**. Opening the first browser window adds startup time; slow navigation can extend a visit. Later visits reuse that window and use Alt+Tab with a checked fallback to the exact window. The timer counts file creation, source entry, and coding checkpoint pauses; initial project/workspace setup, browser visits, and F8 pauses are excluded. Visits happen at completed-character checkpoints, so an input delay or short save operation can extend the interval slightly.

Documentation topics cover [Python data structures](https://docs.python.org/3/tutorial/datastructures.html), functions, pathlib, and JSON, plus [MDN JavaScript functions](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Guide/Functions), promises, objects, and arrays. Search queries and official URLs are editable in the JSON. `browser.url_list_file` points to `../../References/Apollo Vehicle URLs AU and NZ.txt`, which currently adds **19 Apollo links**, including the AU/NZ fleet pages. The player reads that text file on each launch, recognizes inline links such as `Fleet: https://...`, and removes duplicate URLs. Edit the text file to change those choices for the next run; no code-snapshot regeneration is needed. Missing files, lists without HTTP/HTTPS links, malformed links, or links longer than 219 characters stop validation before input. Removing the optional `url_list_file` property keeps documentation-only browsing compatible with earlier configs.

**Vehicle business research.** `browser.research_file` points to `../../References/Vehicle Research Queries.txt`: one Google search per line (`#` comments allowed), reloaded on each launch. Research is a fourth equal-chance category. A research visit searches Google, scrolls the results for up to 9 seconds, then moves the mouse to one of the first visible organic results and clicks it, and reads that page for the rest of the visit. Results are found through accessibility; Google's own pages, YouTube, and anything under a "Sponsored"/"Ads" label are skipped (best effort). If no suitable result is visible, the visit keeps reading the results page.

**Opening the browser from the taskbar.** With `browser.launch` set to `"taskbar"`, the first visit presses Win+T, locates the pinned **Google Chrome** taskbar button through accessibility (no stored pixel positions), moves the mouse to it, and clicks. If Chrome already has windows open, it middle-clicks to get a new window. It then types the address with Ctrl+L. Pin Chrome to the taskbar. If the button can't be found, playback falls back to Windows Run. Use `"run"` or remove the key for the old behavior. Later visits still reuse the same window with Alt+Tab.

If the foreground window is briefly empty (for example during a focus hand-off or an RDP repaint), playback now waits up to 5 seconds for focus to return to the target before stopping.

During documentation visits, playback locates and clicks Google's Search field through accessibility, types the query at the normal pace, then opens the official page by its configured URL. Browser setup must already be complete. Playback does not verify rendered web content or resolve network/consent challenges; an unavailable Search field stops a documentation visit safely.

Check the job without generating input:

```powershell
.\TutorialAutomation\build\tutorial.exe --validate-config .\TutorialAutomation\scripts\reference-coding.json
.\TutorialAutomation\build\tutorial.exe --dry-run-config .\TutorialAutomation\scripts\reference-coding.json
```

Files are saved and checked about every 45 seconds of source typing and before browser visits. After Ctrl+S, verification waits up to 30 active seconds for VS Code to finish saving and for the saved text to reach the expected checkpoint. An older, shorter matching prefix is retried; different text still stops playback. Pause and stop controls remain available while waiting. Each checkpoint also logs the coding time remaining until the next browser visit; each visit logs its selected category and URL.

Completed matching files are skipped. `logs/reference-coding.state` records which partial files this job created, keyed by destination, original filename, and source checksum. Run the same launcher again to resume: only a recorded file whose saved content is still an exact prefix of its reference is eligible. Existing different files or unrecorded partial files stop preflight before any desktop input. A dirty editor buffer from an interrupted run is saved and checked again before continuing. Close an unfinished Save As dialog before restarting. Keep the progress file for resuming, and resolve a conflicting output manually rather than overwriting it. Use a single editor group for playback.

To deliberately restart a conflicting file, close its VS Code tab and remove that output file from the destination folder. The next run recreates missing outputs; other completed files are still skipped. Startup failures are recorded in the playback log even before the countdown, and the launcher keeps its console open when startup checks fail so the error remains visible.

The original snapshots retain all line-ending details. As requested, editor output uses normalized **LF** line endings; all other text and whitespace must match. This matters for `wordpress-save.cjs` and `analyze-motorhomehire-profile.cjs`, whose references contain mixed line endings, and `motorhomehire-profile-schema.cjs`, whose reference uses CRLF. The snapshot checksum validates original bytes, while saved-text verification permits only line-ending normalization.

Edit the timing ranges under `typing`, `actions`, and `browser` to change the presentation. `seed: 0` chooses and logs a fresh seed; a positive seed repeats its random sequence. Changing `content` also requires its matching SHA-256. To intentionally refresh all snapshots from the references:

```powershell
.\TutorialAutomation\scripts\New-ReferenceCodingConfig.ps1
```

Regeneration replaces the config, including custom selections/timing. It does not change references, output files, or progress. The generator scans `.py` and `.cjs` files directly in `References/scripts`; it does not execute them. No additional dependencies are needed beyond the installed native build tools, VS Code, and Chrome/Edge. Keep the RDP desktop unlocked, visible, and free of manual input while playing. **F8 pauses/resumes; F9 or F12 stops.**

## Build

Install [CMake](https://cmake.org/download/) 3.20+ and either [w64devkit](https://github.com/skeeto/w64devkit/releases) / MinGW-w64 or Visual Studio Build Tools with the **Desktop development with C++** workload and Windows SDK. Use Windows 10 version 1703 or newer, or Windows 11.

From this directory, with the chosen tools on `PATH`:

This workspace also includes verified portable GCC 15.2.0, Make, and CMake 3.31.6 under the parent `.tools/` directory. The parent `setup-tools.ps1` registers these and the existing VS Code installation in the current user's PATH. Restart terminals and VS Code after setup; keep `.tools/` in its current location. Rerunning setup is safe.

```powershell
# MinGW-w64 (gcc.exe and mingw32-make.exe must be on PATH)
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

# Alternatively, from a Visual Studio developer console; use a separate build folder
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

MSVC puts executables in `build-msvc/Release/`. MinGW puts them directly in `build/`. Windows SDK libraries `user32`, `shell32`, `wtsapi32`, `ole32`, `oleaut32`, and `oleacc` supply the runtime APIs. No additional automation package is required.

## Run

Run these commands from this directory:

```powershell
build/tutorial.exe --validate scripts/excel_tutorial.txt
build/tutorial.exe --dry-run scripts/excel_tutorial.txt
build/tutorial.exe --script scripts/excel_tutorial.txt --countdown 5
build/tutorial.exe --script scripts/coding_tutorial.txt --log logs/coding.log
```

Validation and dry-run never generate desktop input or activate applications. Both load `TYPEFILE` content too; dry-run logs each command, numeric arguments, key counts, and text lengths without executing pauses or delays. Playback starts after a three-second countdown by default. Low-level scripts require an already-open target; the coding and browser actions open their own windows. A title fragment must match exactly one visible window, case-insensitively; zero or multiple matches fail. Windows foreground activation restrictions are respected. Focus the intended application yourself and retry if activation is refused.

F8 toggles pause/resume, F9 stops, and F12 is the emergency stop. F8/F9 are registered global hotkeys. Windows reserves F12 for debuggers, so a temporary low-level keyboard hook handles and consumes F12 during playback; it records no keyboard input and passes other keys through. Failure to register F8/F9 or install the F12 hook prevents playback. The hook is removed on exit. Ctrl+C also requests a stop. Held synthetic keys and buttons are released on exit or interruption. Pausing while a key, shortcut, click, or drag is held stops playback and releases that input. Pausing between double-click halves also stops safely. Unicode character batches remain atomic. Paced key presses and shortcuts have brief holds; releases complete even when the press changes the foreground application.

The console displays the current source line. Playback and dry-run create a timestamped execution log under `logs/` relative to the current working directory; `--log` selects a different writable file. Exit codes: `0` success, `1` interrupted/failed playback, `2` CLI/script/log error. Release physical modifier keys and mouse buttons before starting, and avoid concurrent manual input.

## Script language

Save scripts and text assets as UTF-8, with or without a BOM. Commands and key names use uppercase. Blank lines and `#` comments are ignored; `#` inside quotes is text. Strings must be quoted. Supported escapes: `\n`, `\r`, `\t`, `\"`, `\\`. Backslashes in quoted Windows paths therefore need doubling; forward slashes also work.

| Command | Arguments and behavior |
|---|---|
| `ACTIVATE_WINDOW "title fragment"` | Activate exactly one visible matching window and set the target. Required before keyboard/mouse actions. |
| `COORDS CLIENT` / `COORDS SCREEN` | Physical pixels relative to the target client area (default) or virtual screen. Screen coordinates may be negative on secondary monitors. |
| `MOVE x y duration_ms [STRAIGHT\|CURVE]` | Deterministic smoothstep easing; default straight path. |
| `CLICK LEFT\|RIGHT\|MIDDLE` | Press and release a mouse button. |
| `DOUBLE_CLICK` / `RIGHT_CLICK` | Left double-click or right single-click. |
| `DRAG x1 y1 x2 y2 duration_ms [STRAIGHT\|CURVE]` | Left-button drag. Endpoints are checked before pressing. |
| `SCROLL notches` | Signed wheel notches; positive scrolls upward. |
| `TYPE "text"` | Unicode entry, with CR/LF mapped to Enter and tabs to Tab. |
| `TYPEFILE "relative/path.txt"` | Type a UTF-8 file; relative to the **script directory**. Content is preloaded before input begins. |
| `KEY ENTER` | Single named key press/release. |
| `SHORTCUT CTRL SHIFT S` | One or more modifiers, followed by a key; releases in reverse order. |
| `TIMING character_ms line_ms` | Explicit fixed delay after each Unicode character and newline. Disables variable typing pacing. |
| `TYPING_PACE char_min char_max line_min line_max seed` | Reproducible variation in character and line delays, in milliseconds. `TYPING_PACE OFF` restores fixed timing. |
| `TYPING_BREAKS chars_min chars_max pause_min pause_max` | Schedule extra pauses at the next word/line boundary after a variable character interval. `TYPING_BREAKS OFF` disables them. |
| `ACTION_PACE pause_min pause_max move_min move_max seed` | Vary pauses, cursor duration, gentle cursor arcs, click positions, and click timing for the desktop workflow. Milliseconds; seed is numeric or `AUTO`. `ACTION_PACE OFF` disables variation. |
| `CODE_REFERENCE "absolute folder" "source.py"` | Open Explorer and VS Code, create a fresh Python file, type the preloaded reference, save, and compare the saved text. |
| `DESKTOP_CODE_REFERENCE "absolute desktop folder" "source.py"` | Show the desktop, double-click the folder, open Command Prompt from Explorer, create an empty file, open VS Code, click its tab, and type the reference. |
| `BROWSER_SCROLL "https://example.com/page" duration_ms seed` | Open a separate Chrome/Edge window, scroll with reading pauses, and return to the previous application. |
| `BROWSER_BREAK lines "https://example.com/page" duration_ms seed` | Add a browser visit after each specified number of saved lines in either coding action. `BROWSER_BREAK OFF` disables visits. |
| `WAIT milliseconds` | Interruptible, pausable delay. |
| `PAUSE` | Wait for F8. |
| `END` | Required final command; trailing comments/blanks allowed. |

Playback defaults to 140-300 ms per character, 900-2,200 ms per newline, 6-14 second pauses after 160-320 characters at a boundary, 1.4-3 second action pauses, and 1-1.8 second cursor travel. This applies to commands, paths, filenames, source text, and browser setup. Enter, other individual keys, shortcuts, mouse clicks, and wheel notches also receive brief paced holds or intervals. Movement/drag durations are at least the action profile's sampled duration. Explicit `TIMING`, `TYPING_PACE`, and `ACTION_PACE` commands can override these defaults. Bundled examples use the normal slower typing profile.

Keys: A–Z, 0–9, CTRL, ALT, SHIFT, WIN, ENTER, TAB, ESC/ESCAPE, BACKSPACE, DELETE, arrows, HOME, END, PAGEUP, PAGEDOWN, SPACE, INSERT, and F1–F24 except reserved F8/F9/F12. Unknown or duplicate keys and malformed shortcuts are rejected. Durations are 0–3,600,000 ms, coordinates −100,000–100,000, scroll notches −10,000–10,000, character delay 0–10,000 ms and line delay 0–60,000 ms. A file may contain up to 16 MiB; scripts are limited to 100,000 commands.

Client coordinates are resolved against the target's current client rectangle on every animation sample. Destinations must fall on an actual monitor; holes in multi-monitor layouts are rejected. DPI awareness uses per-monitor v2 with a fallback. `WAIT` and cursor movement are presentation timing, not real-time scheduling; system load, input processing, and RDP latency can stretch them.

Validation checks syntax, arguments, UTF-8, and external file readability. Live window existence, focus, display bounds, application readiness, and formula results require playback/manual verification. Validation does not launch applications or inspect their content.

## Excel demo

Open **a new, blank, disposable Excel workbook**, close other Excel windows, and validate/dry-run first. The demo uses Ctrl+G cell references instead of fixed coordinates. It writes 10, 20, 30 into A2:A4; displays SUM = 60, AVERAGE = 20, and the first value's share as 17% (rounded display); then bolds the first row and navigates the results. It does not save the workbook. Existing content in A1:C4 will be replaced.

The supplied formulas, dialog navigation, and percentage shortcut assume English Excel and its usual Windows shortcuts. Localized formula names/separators may require editing the script. Turn off cell edit mode and dismiss dialogs before starting. Delays may need extending on a slow RDP connection.

## Browser scrolling

From the parent `worken` directory, run:

```powershell
.\start-browser-scrolling.cmd
```

During the **10-second countdown**, focus the application you want to return to and close any Windows Run dialog. Scripts containing `BROWSER_SCROLL` also default to this countdown when run directly without `--countdown`. The sample opens a separate Chrome window (or Edge if Chrome is unavailable), visits the Python data structures tutorial, and scrolls for **20 seconds**. Cursor movement and mouse-wheel events go through the same guarded input controller as the other actions. Scroll bursts move down 2-4 notches, with occasional 1-2 notch upward movements, separated by 1.6-3.8 second reading pauses. A seed selects a repeatable sequence. The visit ends by switching back to the previous application. The browser window remains open.

Edit `scripts/browser_scrolling.txt` to change the URL, duration, or seed:

```text
BROWSER_SCROLL "https://docs.python.org/3/tutorial/datastructures.html" 20000 42
END
```

Durations are 1,000-300,000 ms and seeds are 1-2,147,483,647. URLs must use HTTP or HTTPS, with no spaces, quotes, or backslashes; encode spaces as `%20`. The fixed browser launch must fit the Windows Run dialog's text limit, so keep URLs short. Opening the browser and its initial 3.5-second loading allowance are additional to the scrolling duration. Slow pages may need a retry; playback does not inspect page content or confirm network success. Chrome/Edge must already have completed first-run setup. F8 pauses/resumes and F9/F12 stop throughout the visit.

To include reading visits while code is being entered, place this before either coding action:

```text
BROWSER_BREAK 40 "https://docs.python.org/3/tutorial/datastructures.html" 20000 42
```

The vehicle sample already includes this setting. It verifies the saved code at each visit, switches to the browser, scrolls, and returns to the same editor to continue typing. Later visits reuse the same browser window and its current scroll position, with successive seeds. The interval must be a multiple of 20, from 20 to 100,000 lines, matching the coding action's save checkpoints. There is no visit after the final chunk. Use `BROWSER_BREAK OFF` to disable these visits. The standalone browser action opens a new window each time it appears in a script.

## VS Code demo

### Complete vehicle coding action

From the parent `worken` directory, run:

```powershell
.\start-vehicle-coding.cmd
```

After a visible **10-second countdown**, the sample performs this sequence:

1. Show the desktop with Win+D, unless the desktop is already visible.
2. Locate and double-click the **Project for vehicle** folder icon.
3. Click Explorer's address bar and type `cmd` to open Command Prompt in the folder.
4. Type exactly `code .` in Command Prompt to open the folder in VS Code. Hover over the Explorer folder header, click its **New File...** button, and type a fresh `vehicle_lookup_<process>_<tick>.py` name.
5. Save and verify the empty file, select Plain Text, configure Plain Text workspace settings through VS Code Settings, return to the new file tab, and type the 219-line `References/scripts/vehicle-lookup.py` reference.

It saves every 20 lines, takes a 20-second browser visit every 40 saved lines, and verifies the final saved text against the reference, allowing CRLF/LF differences. The slower presentation can take tens of minutes for this long file. Setup commands are also typed at the configured speed. F8 pauses/resumes; F9/F12 stop.

The sample's presentation pacing is configured in `scripts/vehicle_coding.txt`:

```text
ACTION_PACE 1400 3000 1000 1800 AUTO
TYPING_PACE 140 300 900 2200 AUTO
TYPING_BREAKS 160 320 6000 14000
BROWSER_BREAK 40 "https://docs.python.org/3/tutorial/datastructures.html" 20000 42
```

The action profile adds 1.4-3 second pauses between steps and makes cursor travel take 1-1.8 seconds. Clicks vary slightly around the control's center, remaining inside its bounds; cursor arcs and button timing also vary. `AUTO` chooses and logs a fresh seed for each run. Replace it with a logged numeric seed to reuse that random sequence. Action pause ranges are 0-60,000 ms, cursor durations 100-10,000 ms, and numeric seeds 1-2,147,483,647.

The typing profile chooses 140-300 ms after each character and 900-2,200 ms after each newline. After 160-320 characters it waits for a word/line boundary or comma/semicolon, then adds a 6-14 second pause. Pauses are logged and remain interruptible with F8/F12. Typing seeds also support `AUTO` or a number. Configuration supports character delays of 1-10,000 ms, line delays of 0-60,000 ms, pause intervals of 1-100,000 characters, and pause durations of 1-60,000 ms. Maximum values must be at least their corresponding minimums.

Typing pacing changes delays without inserting mistakes or modifying source text. Its state continues across setup and the 20-line save checkpoints. Use `TIMING 25 160` in place of the typing commands for fixed typing speed. `ACTION_PACE OFF` disables the extra action variation. Browser visits use the action profile for their cursor travel when enabled.

The destination must be an existing folder directly on the current user's desktop, with exactly one matching icon. Icon positions are read through Windows accessibility; Explorer's address bar and VS Code's file tab are located through UI Automation. These APIs only read names and bounds. Mouse clicks and keyboard entry perform the actions, using current screen coordinates instead of a stored pixel map. Keep desktop icons visible. An unavailable or ambiguous target stops playback.

The VS Code CLI `code` must be on the PATH inherited by Command Prompt; the supplied tool setup registers it. VS Code creates the output file through its Explorer UI. The action sets **Workspace → Plain Text** `editor.autoIndent` to `none`, `editor.acceptSuggestionOnEnter` to `off`, and `editor.autoClosingBrackets`, `editor.autoClosingQuotes`, and `editor.autoSurround` to `never` using the Settings UI. VS Code saves these language-specific project settings in `.vscode/settings.json`; user settings are not changed. Language-specific values take precedence over general editor values, as described in [VS Code settings](https://code.visualstudio.com/docs/configure/settings). Existing extensions remain enabled, so extensions that rewrite Plain Text can still affect output and cause verification to fail. The copied Python source is not executed. Output uses a fresh filename and a collision check. Avoid manual input while playback is active.

To inspect the script without desktop interaction:

```powershell
.\TutorialAutomation\build\tutorial.exe --validate .\TutorialAutomation\scripts\vehicle_coding.txt
.\TutorialAutomation\build\tutorial.exe --dry-run .\TutorialAutomation\scripts\vehicle_coding.txt
```

The sample uses `DESKTOP_CODE_REFERENCE "absolute desktop folder" "UTF-8 source file"`. It types only `cmd` and `code .` at the two launch steps; no file-creation command is entered into CMD. The original `CODE_REFERENCE` remains available for other folders and uses Explorer plus Windows Run with a generated demo workspace. Both preload the source relative to the tutorial script, retain typing pacing throughout setup, and default to a 10-second countdown. UI labels and shortcuts assume English Windows/VS Code. An unexpected foreground window or dialog causes playback to stop or time out.

### Original short C demo

Use the supplied **designated demo workspace**:

1. Open `scripts/demo/TutorialAutomation-Demo.code-workspace` in a separate VS Code window. Review the supplied settings and `.vscode/tasks.json`; trust this workspace when appropriate. Close other VS Code windows.
2. Ensure `gcc.exe` is on VS Code's inherited `PATH`; restart VS Code after changing it. Disable extensions that automatically insert, format, or complete text for this demo.
3. Open `scripts/demo/hello.c`, ensure it is **empty and saved**, and make it the focused editable tab. Its title must include `hello.c - TutorialAutomation-Demo`. Reset only this designated demo file before repeating playback. The player does not clear or select an entire file.
4. Validate and dry-run `scripts/coding_tutorial.txt`, then play it. Do not switch editors during playback.

The script types `hello.txt` with spaces and line breaks, edits the first comment using Home/Shift+End, saves, and invokes Ctrl+Shift+B. The supplied default task compiles only `hello.c` into `tutorial-demo.exe`, then runs that executable; VS Code's terminal displays `Hello, tutorial!`. These are explicit **process** tasks with fixed arguments, with no shell interpolation. The engine itself has no build/run/shell command. Remove the final shortcut and wait from the script if build/run is unwanted. The engine cannot verify compilation or output; check the visible terminal for success. Change the final wait if the compiler takes longer than four seconds.

The title and workspace preparation constrain the demo to the designated file, but this player is not an editor content inspector. It cannot prove which file VS Code is editing or enforce application permissions. Use only the supplied disposable workspace; do not substitute an unrelated editor file.

## RDP and input limitations

Playback requires the current process session to report `WTSActive` and the input desktop to be accessible and named `Default`. Lock, disconnect, secure-desktop prompts, inaccessible windows, and invalid coordinates stop playback. It never attempts to unlock, reconnect, or bypass desktop restrictions. A target that closes, minimizes, or loses foreground is rejected before further input. Keyboard interaction permits modal dialogs owned by the target in the same process, such as Excel's Go To dialog. Other foreground windows stop input. Mouse buttons and scrolling require the cursor to be over the actual target window, protecting against inactive-window scrolling and overlays; client coordinates refer to the original target, not a modal dialog.

Minimizing an RDP client may suppress rendering/input without an immediately detectable session-state change. Keep the client visible and the desktop unlocked throughout recording. RDP interruption detection is best effort; input release may fail after Windows withdraws access to the desktop. Restart from a known application state after reconnecting.

`SendInput` cannot drive elevated applications from a lower integrity process; match normal integrity levels and avoid elevated targets. Protected fields and applications that reject Unicode input are unsupported. Text entry preserves supplied spaces and newlines, but editor indentation/completion settings can still alter the result. Windows focus changes can race with an individual input event; avoid manual interference. Coordinate validity alone does not establish that the intended control occupies a pixel. The desktop coding action additionally discovers the named folder, address bar, and file tab through accessibility. Other low-level commands still rely on configured coordinates and delays; no visual page-content recognition is implemented. Microsoft documents [accessibility screen locations](https://learn.microsoft.com/en-us/windows/win32/winauto/navigation-through-hit-testing-and-screen-location) and [UI Automation element discovery](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-obtainingelements).

## Testing and extension

See [tests/TESTING.md](tests/TESTING.md) for automated coverage, manual desktop checks, and recorded results. `input_probe.exe` is an opt-in Win32 receiver for checking real input in a disposable window; ordinary CTest runs do not generate input.

To add a command: add a `CommandKind` value and its name in `script_engine.h` / `script_engine.c`, validate and populate it in `script_parse`, implement its runtime branch in `playback.c`, and add parser tests plus a manual integration case. Add new enum entries at the end and update parser/name bounds as needed. Keep all input behind desktop/focus guards, interrupt long operations through `playback_poll` / `playback_wait`, track held input for cleanup, and preload/validate resources before playback. No command should execute arbitrary shell text.
