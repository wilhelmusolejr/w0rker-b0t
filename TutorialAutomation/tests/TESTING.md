# Verification and desktop acceptance tests

## JSON flow examples - 2026-11-01

The native player rebuilt successfully. Five focused CTest checks passed:
`flow_config_tests`, `validate_workday_flow`, `dry_run_workday_flow`,
`validate_browse_10m_flow`, and `dry_run_browse_10m_flow`. The short flow's dry
run printed one `browser.browse` step with 10 work minutes, zero planned idle,
zero other-app minutes, and no Loggy3 action. Validation and dry run generated
no desktop input and wrote no progress file.

The short browsing flow and the 10h30m workday flow have not been run live from
their JSON configurations. A live check needs an unlocked interactive Windows
desktop; browser launch, page load, and safe-point behavior are not established
by these input-free tests.

## Manual input configuration and pacing - 2026-10-31

The native player and regression targets rebuilt successfully. All five selected
CTest checks passed: `action_pace_tests`, `playback_controls_tests`, `session_tests`,
`dry_run_session`, and `dry_run_session_duration`. They cover the optional
`pause_on_user_input` boolean, its backward-compatible default, rejection of
non-boolean values, and both automatic-pause modes without desktop input.

A live profile 1 session with `--duration-minutes 20 --coding-minutes 6
--browsing-minutes 3 --seed 7` and `pause_on_user_input: false` completed at
04:09:58 local time. The session reported exactly 20 minutes elapsed, coding and
browsing slots, one other-app break, one completed file, and a saved partial file
for resume. Slot times include breaks and idles. The selected direct-stop ending
left application windows open. L3 was unavailable and its final recording
state remained unconfirmed; failure recovery during confirmed recording was not
validated by this run.

## Session clock change — 2026-10-31

The native build completed with no compiler warnings. Input-free `session_tests`,
`dry_run_session`, and `dry_run_session_duration` passed. The new clock checks cover
a 20-minute duration independent of a larger L3 total, explicit pauses,
the default daily-total target, fallback from a last confirmed total, and a full
default fallback when no total was readable. The duration dry run printed an
end-to-end plan without sending desktop input.

The full CTest run passed 29 of 35 checks. Six unrelated checks require local
fixtures that are absent or differ in this checkout: `project_tests` expects the
`kruzpayr` reference folder; four reference-job checks expect an existing
destination directory; `excel_simulation_files` found rebuilt workbook hashes
different from its recorded manifest. The combined session and L3 failure
path have **not** been run live on a desktop yet.

## Recorded results — 2026-10-15

Environment: Windows x64, GCC 15.2.0 (portable w64devkit 2.5.0), CMake 3.31.6, MinGW Makefiles, Debug configuration.

| Check | Result |
|---|---|
| CMake configure and full native build | PASS: player, input probe, and nine test executables |
| Additional GCC build with `-Wall -Wextra -Wpedantic -Werror` | PASS: engine and CLI, no warnings |
| Parser / UTF-8 / easing / saved-text assertions | PASS: 290 checks, zero failures; includes rejection of the old Plain Text profile that left pair completion enabled |
| Variable typing pacing | PASS: 2,178 checks for bounds, repeatable seeds, pause boundaries, disabled setup, and state continuity |
| Browser scroll planning / URLs / checkpoint scheduling | PASS: 4,020 checks for repeatable seeds, scroll directions/ranges, pauses, URL validation, and visit boundaries |
| Action presentation pacing and bounded click points | PASS: 6,005 checks for pause/movement ranges, curve limits, repeatable/different seeds, negative screen coordinates, and click bounds |
| Paced key presses, shortcuts, and text | PASS: 38 fake-receiver checks for hold/settle bounds, release after focus changes, held-state tracking on interruption/failure, newline timing, and resumable Unicode/CRLF slices; no input generated |
| JSON reference config, progress ownership, and mixed browsing schedule | PASS: 74,113 checks for JSON syntax/escapes, SHA-256 validation, preserved snapshots, line-ending normalization, conflict refusal, safe prefix resume, destination identity, interval/duration bounds, reproducible seeds, timer reset/overflow, text-file URL import/deduplication, category balance, and repeat avoidance |
| Asynchronous reference checkpoint saves | PASS: delayed atomic file replacement, stale-prefix timeout, busy-editor resume, pending reads, incorrect-text refusal, unexpected longer prefixes, and cancellation; no desktop input |
| Guarded VS Code command setup | PASS: 41 fake-UI checks for unavailable palettes, wrong command-field values, missing pickers, and unconfirmed language/line-ending status; no desktop input |
| All-reference manifest audit | PASS: 45 original filenames and embedded payloads match original bytes and SHA-256 checksums; 4 Python and 41 CommonJS files |
| Invalid JSON job configurations | PASS: CLI rejects filename traversal, reserved devices, duplicate filenames, changed payload/checksum, reversed browser ranges, untrusted documentation hosts, and unknown keys without input |
| Batch file creation through native Save As | PASS: two disposable `.py`/`.cjs` files created in the exact project root at normal pacing; independent UTF-8 byte comparison confirmed Python CRLF→LF normalization, accented text, and a supplementary Unicode character |
| Recorded-prefix resume with Google documentation visit | PASS: reopened the exact path, resumed from 19/27 UTF-16 units, saved a prefix, searched Google through its Search field, opened Python pathlib documentation, scrolled for a 120-second visit, restored the same VS Code window, and verified all 27 units |
| Full first reference with the production browser interval | PASS: all 2,572 characters of `analyze-campervanfinder.cjs`, a Google search after 306.3 coding seconds, a 179-second documentation visit, editor return, continued typing, and independent UTF-8 byte comparison; normal input pacing throughout |
| Excel example validation | PASS: 50 commands |
| Coding example validation, including relative TYPEFILE | PASS: 16 commands |
| Excel and coding dry-run | PASS: no input generated |
| CTest suite | PASS: 19/19 tests, including current production-config validation/dry-run, guarded editor setup, delayed saves, and mixed browsing choices |
| F12 control startup, callback handling, cleanup, restart, and end-only live playback | PASS: no input injected |
| Vehicle lookup reference Python syntax | PASS: parsed with Python AST; 219 lines |
| Compile supplied C text asset and run resulting console program | PASS: `Hello, tutorial!` |
| MSVC compilation | Not run; CMake configuration and source support supplied |
| Original CODE_REFERENCE action in VS Code | PASS: Explorer opened folder; VS Code created file, typed 219 lines, saved, and verified all 10,643 characters |
| Live coding with browser visits | PASS: 42-line source prefix, two 4-second browser visits at lines 20/40, browser reuse, editor return, and exact saved-text comparison |
| Standalone browser launcher | PASS: 10-second countdown, 20-second downward/upward scrolling with reading pauses, previous application restored, exit code 0 |
| Live desktop / folder / CMD / new-file / code workflow | PASS: mouse clicks discovered at runtime; 42-line reference typed and verified; two browser visits restored the editor |
| Physical pause / emergency hotkeys during application input | Not manually exercised; control installation/handler tests pass |
| Excel results and VS Code build task execution | Not run in applications |
| RDP interruption, resolution changes, and mixed DPI | Not run; requires controlled desktop session |

CTest's tests operate without injecting input. The playback control regression temporarily installs F8/F9 registrations and the F12 keyboard hook, calls the hook handler with test event structures, verifies release/reinstallation, and executes an END-only script. It skips when no interactive desktop is available. This catches the original error of attempting to register debugger-reserved F12 with RegisterHotKey. Physical F12 delivery still requires the manual acceptance check below. Parser assertions cover valid commands, escaped/empty strings, comments, all malformed command groups, reserved hotkeys, duplicate shortcut keys, numeric overflow/ranges, missing END, commands after END, line-numbered errors, strict UTF-8 with BOM and supplementary characters, malformed UTF-8, embedded NUL, empty files, and smoothstep endpoints/monotonicity. Example validation exercises TYPEFILE resolution relative to the script directory. These tests do not establish real application behavior.

## JSON reference batch acceptance

The full 45-file job was **prepared and validated, not started**, as requested. The production config remains at 300–600 coding seconds and 120–180 browsing seconds, with a 10-second countdown. Every original payload and filename was independently audited against `References/scripts`; total source size is 414,855 bytes / 414,843 UTF-16 units, or 414,707 units after line-ending normalization.

A normal-speed two-file UI fixture completed at 07:27:57 local time on 2026-10-15. `reference-smoke-python-v2.py` and `reference-smoke-javascript-v2.cjs` were created through Ctrl+N and the owned native Save As dialog. Independent byte comparison confirmed their expected UTF-8/LF contents, including `café` and a car emoji. Setup, paths, filenames, source, and mouse actions all used the production pacing ranges.

A second fixture used the existing GUI-created JavaScript file as a recorded 19-unit prefix of a 27-unit payload. Only the fixture's progress metadata was prepared directly; code output was extended through VS Code. Its coding interval was shortened to 5 seconds to reach a visit without waiting 5–10 minutes; typing/action timing stayed normal and the browser budget stayed at 120 seconds. At 07:31:13 it began the visit, clicked Google's Search field, typed the configured Python pathlib query, scrolled the results, and opened the official page. Read-only accessibility inspection confirmed the foreground document title was `pathlib — Object-oriented filesystem paths — Python 3.15.0 documentation`. It returned at 07:33:16, finished the exact remaining text, and exited successfully at 07:33:25. Independent saved-text comparison passed afterward.

These checks exposed modal Settings overlays and Explorer's tendency to create new files in the selected subfolder. The batch now dismisses Settings overlays, waits for accessible controls, verifies existing Plain Text settings, saves new files to explicit absolute paths through the native dialog, and reopens partial files by absolute path. The all-file, multi-day job and repeated Google visits across a full run have not been replayed; repeated browser reuse was exercised by the earlier browser integration described below. Test files and logs are disposable and use distinct names from all 45 references.

### Checkpoint bracket-completion regression

The user's 07:50 run stopped at 07:54:38 while typing `analyze-campervanfinder.cjs`. Independent comparison found 467 correct source characters followed by two extra closing characters, `)}`. The saved workspace profile only disabled indentation and accepting suggestions on Enter. A checkpoint inside an unfinished bracketed expression exposed automatic pair completion, which the earlier balanced, short examples had missed. Resume correctly refused the resulting non-prefix file.

The workspace profile now also requires `editor.autoClosingBrackets`, `editor.autoClosingQuotes`, and `editor.autoSurround` to be `never` under `[plaintext]`. The UI configures all five required values and reads the saved JSON back before source entry. Regression assertions reject the old two-value profile and enabled pair-completion values. Saved-content errors now report the exact differing UTF-16 position and character codes.

At 17:49:54 local time on 2026-10-15, a live regression completed the first 550 characters of the failing reference at the production typing/action speeds. It verified checkpoints at 145, 313, 440, and 550 characters, including unfinished brackets, then independently matched the output to the original 550-character text. Read-only accessibility inspection confirmed the editor used Plain Text, LF, and UTF-8. The fixture disabled browser visits; the production config's 300–600 / 120–180 second ranges were unchanged.

The damaged file and progress record were backed up under `logs/reference-repair-20261015-173607/`. After opening and saving the actual partial file in VS Code, its bytes were rechecked against the known 467-character prefix plus `)}`. Two Backspace presses at the end removed only those characters through the editor. Independent read-back verified the 467-character prefix, and the production 45-file config passed validation with resume available. The rebuilt executable passed all 17 CTests and the strict GCC build. The full multi-day job remains stopped.

### Delayed-save regression

The user's next run stopped at 18:00:02 after Ctrl+S: verification read the preceding 776-character checkpoint while 923 characters had been typed. A subsequent independent read found all 923 characters saved correctly. The first browser visit was scheduled after 591 coding seconds; the run stopped before reaching that deadline.

Reference checkpoints now wait up to 30 active seconds for the saved text to reach the exact expected length, retrying an older matching prefix or a temporarily unavailable read. An unknown resume offset also waits while VS Code's title reports unsaved changes. Different text and unexpected longer prefixes still fail immediately, and pause/stop checks remain active. Each checkpoint logs the coding time remaining until the next documentation visit.

The new saved-text waiting executable passes 28 checks without desktop input. It reproduces a real delayed atomic save after 650 ms, then covers a stale-prefix timeout, pending editor resume, pending reads, mismatched text, a longer-than-expected prefix, cancellation, and invalid arguments. All 18 CTests and the strict GCC build pass.

A live first-file run reached Google after 306.4 coding seconds and loaded the official Python pathlib page. At 18:19:04 the cursor moved outside Chrome, correctly triggering the wheel-input guard; the user confirmed moving the mouse. The saved 742-character prefix remained correct. That interrupted log is preserved as `logs/reference-save-wait-browser-interrupted-20261015.log`.

The resumed single-file run used the original complete source, the production 300–600 second coding interval and 120–180 second visit ranges, and normal typing, action, and mouse timing. After 306.3 coding seconds it saved 1,535 characters, opened Google, entered its query, scrolled the results, and opened Python pathlib documentation for a 179-second visit. At 18:30:33 it returned to the same VS Code window; read-only accessibility inspection confirmed that window, and the next saved checkpoint reached 1,663 characters. The run completed at 18:36:20 with exit code 0. Independent text and UTF-8 byte comparison verified all 2,572 characters; the saved SHA-256 is `9f21df8a5d13ea66018259767c3e9c628dcde49925b2187f66f9350b5bf76f4b`. The log is `logs/reference-coding-save-fix-20261015.log`.

The full 45-file config was then validated without input: one file is complete and 412,135 normalized UTF-16 units remain across the other 44 files. The full job was not started. Its seed remains automatic (`0`), and its original browser ranges remain unchanged.

### Command palette setup conflict

The user's later run completed `analyze-campervanfinder-nz.cjs`, including a Google/MDN browser visit and editor return, then stopped at 18:58:06 while preparing the third file, `analyze-editor-snapshots.py`. That file contained only `\r\nChange End of Line Sequence\r\n` (31 bytes, 29 characters after normalization). The setup command had reached the editor instead of a command palette. No reference code had been typed into that file. Both completed `.cjs` outputs independently matched their original snapshots. Restart preflight correctly refused the third file before any desktop input.

The damaged output, progress state, and failing log were backed up under `logs/reference-start-repair-20261015-192220/`. The user chose to reset the output manually, so the repair did not erase it or start another reference playback. At the last check it was still present and conflicting; removing that output after closing its editor tab allows it to be recreated while the two completed files are skipped.

Editor setup now verifies the command field's accessible name and exact value, plus focus within its `quick-input-widget`, before typing commands and before confirming them. Live read-only inspection established that VS Code can expose active-descendant focus on a selected result row while its Edit control receives typing. The guard accepts that shared-widget relationship and requires the expected language/line-ending picker and final Plain Text/LF status. Missing fields or unexpected values stop setup. Startup failures now reach the execution log before preflight returns, and the launcher holds startup-error output open.

All 41 command setup regression checks and all 17 selected CTests passed; the two production reference checks were excluded because the output conflict intentionally remains. The strict GCC build passed with warnings treated as errors. A malformed-config startup and an invalid-argument launcher check confirmed error logging, visible startup failure output, exit code 2, and no desktop input. A complete live playback of the updated command guards was not run after the user chose the manual reset.

### Mixed documentation and Apollo browsing

The production config now references `References/Apollo Vehicle URLs AU and NZ.txt` through the optional relative `browser.url_list_file` field. Loading produces 27 choices: four Python documentation topics, four JavaScript documentation topics, and 19 unique Apollo links, including inline fleet links. The list is read again on the next launch. The 45 source entries and their checksums, seed, typing/action profiles, 300–600 coding-second intervals, and 120–180 second visit budgets were preserved.

Selection first chooses an available category with equal probability, then chooses a page within that category while excluding the previous page when alternatives exist. Python/JavaScript choices retain their Google search step; URL-list pages open directly. Both use the same guarded browser window switching, scrolling, reading pauses, and return path.

The config tests import a relative text-file path containing spaces, verify inline links and duplicate removal, reject missing/empty/malformed/overlong lists, and exercise 30,000 seeded selections for reproducibility, category balance, reachability of every choice, and repeat avoidance. All 19 CTests and the strict GCC build pass. The current production dry-run recognizes four completed references and reports the 4/4/19 browser choice counts.

A browser-only live check completed at 23:15:47 local time on 2026-10-15 with exit code 0. It retained the 10-second countdown and normal typing/mouse/action pacing, opened the listed Apollo Australia Hitop URL directly, scrolled down and occasionally up with reading pauses for 120 seconds, and restored the previous application. Read-only window inspection and a screenshot confirmed `Apollo Hitop` and visible page content during scrolling. The log is `logs/browser-apollo-live-20261015.log`. The full coding job was not started; legacy line-based browser visits retain their current scroll position when reusing a window, while mixed-topic visits navigate to the newly selected page.

The config generator was also exercised into a disposable JSON file. Its relative URL-list path resolved correctly, and the generated 45-file config passed native validation against an empty disposable destination without input.

## Disposable input receiver

1. From the project directory, run `build/input_probe.exe`. It opens a plain Win32 window and displays received mouse, shortcut, and text events. It never injects input itself.
2. In another console, run `build/tutorial.exe --validate scripts/input_probe.txt`, then `build/tutorial.exe --script scripts/input_probe.txt --countdown 5`.
3. Observe the straight and curved motions. Expect **3 left presses** (single, first double-click press, drag), **1 double-click**, **1 right press**, **1 wheel message**, and **1 Ctrl+K shortcut**. Windows may deliver a physical wheel message differently through an RDP client; inspect the visible result.
4. Confirm the text includes `café Ω 😀`, an indented line, and `=SUM(10,20,30)`. A control character from Ctrl+K can also appear in the text buffer. Supplementary characters count as two UTF-16 units; font glyph availability varies.
5. At the scripted PAUSE, confirm the cursor remains still. F8 resumes the final movement and delay. Repeat and press F8 during the initial movement and text entry; confirm input stops while paused and resumes at the same operation.
6. Repeat and press F12 during movement, typing, a wait, and the pause. Playback should promptly exit with code 1 and log a stop. Repeat with F9 and Ctrl+C. Confirm no simulated key or mouse button stays held.
7. Press F8 during the drag: it must release the button and **stop**, rather than resume an incomplete drag. Press F8 between double-click halves: playback must stop safely.

The receiver captures mouse messages but does not act as a text editor: drag selection and indentation are checked from its event counters/text display. Screen-record a playback for judging motion quality.

## Focus, bounds, and desktop interruption

Use the probe, not a document containing valuable work, for these checks:

| Scenario | Expected behavior |
|---|---|
| No matching window / two matching windows | Activation fails without tutorial input |
| Owned modal dialog, such as Excel Go To | Keyboard entry continues in the target-owned dialog |
| F8 or F9 already registered by another application | Playback refuses to start |
| F12 reserved by Windows for debugging | Startup succeeds using the temporary keyboard hook |
| Switch foreground window or minimize/close the probe during a wait | Further mouse/keyboard input is rejected |
| `MOVE -1 -1 500` in CLIENT mode | Coordinate error |
| SCREEN destination beyond all monitors | Coordinate error |
| Curved path through a gap between monitors | Stops at first invalid animation sample |
| Move/resize the target | Client coordinates use the current client rectangle |
| Run at 100%, 150%, 200% scaling; move between monitors | Cursor destinations remain client-relative physical pixels |
| Change resolution during movement | Continue only while coordinates and target remain valid |
| Lock or disconnect RDP during WAIT or a drag | Stop when desktop/session unavailability is observed; release input best effort |
| Reconnect after interruption | No automatic continuation; restore target state before restarting |
| Minimize RDP client | Unsupported reliability; document observed rendering/input behavior, keep visible for recordings |
| Elevated probe / lower-integrity player | Input failure/UIPI limitation reported; do not bypass it |

Release held physical keys before testing. If a desktop transition prevents release events, confirm key/button state after reconnecting. The application cannot guarantee Windows will accept cleanup events on an inaccessible desktop.

## Tutorial acceptance

Follow the README preparation exactly. In a new blank English Excel workbook, verify A2:A4 are 10/20/30, C1 is 60, C2 is 20, C3 displays 17%, and the first row is bold. Confirm displayed formulas reference A2:A4 and check pauses let viewers read the results.

In the supplied VS Code demo workspace with an empty saved hello.c, verify exact whitespace, Unicode-independent C punctuation, the edited first comment, the saved source, successful compilation by the fixed process task, and terminal output `Hello, tutorial!`. Confirm unrelated files remain untouched. Run twice only after resetting the designated demo file. Check slow sessions with longer waits.

Record application versions, keyboard language, monitor geometry/scaling, RDP client state, logs, and pass/fail observations when completing these manual checks.

## Complete vehicle coding action

Run `start-vehicle-coding.cmd` from the workspace root. The current sample uses the desktop workflow described below: confirm the 10-second countdown, desktop view, folder double-click, Explorer address-bar click, Command Prompt launch, new file creation, VS Code tab click, source entry, browser visits, and final text verification. Check F8/F12 during playback and inspect partial output after a stop. The older Run-based `CODE_REFERENCE` action remains supported; its earlier full-length live result is recorded next.

Live run completed on 2026-10-15 at 04:38 local time with exit code 0. Output:

```text
C:\Users\hinatardp\Desktop\Project for vehicle\vehicle_lookup_25664_1217645828.py
```

The engine entered code through the editor and saved every 20 lines. Independent verification confirmed all 10,643 characters match `References/scripts/vehicle-lookup.py`, and Python's AST parser accepted the saved file. The copied program was not executed. The folder already existed, so the new-folder creation branch remains a manual test.

Live testing identified asynchronous editor startup/save behavior and missing keyboard scan codes. The implementation now supplies scan codes, sends complete shortcut press/release batches, checks the foreground editor identity, and waits for saved content to reach the expected state rather than assuming a fixed save delay. Named files are opened as part of the fixed VS Code launch and created by Ctrl+S, avoiding an unreliable native Save As handoff.

## Slower presentation pacing update

The first pacing update used `TYPING_PACE 80 200 600 1800 42` and `TYPING_BREAKS 160 320 4000 10000`. The current vehicle sample uses the same ranges with `AUTO` for a fresh typing seed. Automated tests verify delay limits, identical sequences for matching seeds, variation for different seeds, deferral of longer pauses until a boundary, disabling extra pauses, preservation of state across typing chunks, and suspension during the original setup action. Parser checks reject inverted ranges, missing arguments, invalid seeds, zero intervals, and excessive pause durations. The strict GCC build also passes with warnings treated as errors.

The complete 219-line playback at this slower speed has not been repeated. The earlier live run establishes the full source workflow; the pacing tests establish the delay schedule. A later 42-line live browser integration run exercises editor setup, source entry, saves, application switching, and final text verification. Pauses use the existing interruptible playback wait, and they are logged as presentation pauses.

## Browser scrolling update

`start-browser-scrolling.cmd` provides a 10-second countdown followed by a 20-second browser reading visit. `scripts/vehicle_coding.txt` now enables a 20-second visit after every 40 saved lines. The player opens a separate Chrome window through Windows Run, using Edge if Chrome is unavailable, and sends actual cursor movement and mouse-wheel input. Scroll bursts and reading pauses use a seeded, repeatable schedule. Each visit returns to the previous application. Visits during one coding action reuse the browser window and its scroll position.

The standalone launcher completed at 05:40 local time with exit code 0. Chrome displayed the Python data structures tutorial, scrolled down in several bursts and up once, and restored the previous application after the configured 20-second visit. The full 10-second countdown was observed. Edge fallback has not been exercised on the desktop.

The focused integration test used the first 42 complete lines of `References/scripts/vehicle-lookup.py`, fixed zero typing delays, and `BROWSER_BREAK 20 "https://docs.python.org/3/tutorial/datastructures.html" 4000 42`. It completed at 05:37 local time with exit code 0. The first visit opened the browser; the second reused it with seed 43. Both restored the editor, typing continued, and independent comparison confirmed all 1,044 characters match the reference prefix. Output:

```text
C:\Users\hinatardp\Desktop\Project for vehicle\vehicle_lookup_16364_1221665234.py
```

Live checks caught two setup problems: Run needed a stable foreground check before entering its command, and the editor could retain a blank line after the language picker closed. Run now has a short readiness check, and the fresh designated editor is focused, cleared, saved, and verified before source entry. A transient desktop focus loss also stopped a trial without further typing. These checks do not establish reliable input in minimized/disconnected RDP sessions.

To repeat desktop acceptance:

1. Keep RDP visible, close Windows Run, and focus the application to return to during the standalone countdown.
2. Run `start-browser-scrolling.cmd`. Confirm a separate Chrome/Edge window, the configured page, downward and occasional upward scrolling, reading pauses, and return to the same application. The browser remains open.
3. Run `start-vehicle-coding.cmd`, or use a shorter source and a 20-line interval for a quick check. Confirm the saved prefix before each visit, reuse of the browser, the original editor caret on return, and the final exact-content verification. No visit should follow the final chunk.
4. Exercise F8/F9/F12 during browser startup, reading pauses, and scrolling. Switch to an unrelated window or close the browser during a visit; further input must stop. These physical-interruption cases remain manual acceptance checks.

Browser startup assumes Chrome/Edge first-run setup is complete and allows 3.5 seconds for initial loading. No page-content or network-success check is implemented. Slow pages, browser prompts, and browser scroll behavior need visual inspection.

## Desktop coding workflow update

The section below records the earlier console-based file creation revision. The current launcher types `cmd` and `code .`, then creates the file through VS Code; see the final section for the current workflow and pacing.

The vehicle launcher now selects `DESKTOP_CODE_REFERENCE`, with `ACTION_PACE 900 2200 650 1400 AUTO`. It shows the desktop, locates the named folder icon through Windows accessibility, moves to a bounded point inside the icon, and double-clicks it. Explorer's English **Address Bar** and VS Code's new-file tab are located through read-only UI Automation. Actual actions use mouse/button and keyboard input. Click points, cursor duration/arcs, button timing, and pauses vary according to the action seed. `AUTO` seeds are selected and logged at playback time; numeric seeds remain available.

Command Prompt is launched from Explorer with AutoRun and delayed expansion disabled. A fixed `if not exist ... type nul > ...` operation creates only the fresh output file, followed by the fixed VS Code launch. Source code is entered through the editor, rather than written into the output file by the engine. Shell expansion characters are rejected in launch paths. The destination must already exist directly on the current user's desktop, with a unique visible icon; general destinations can still use the original action.

A live test completed at 06:06 local time with exit code 0. It used 300-600 ms action pauses, 650-1,100 ms cursor travel, fixed 4/50 ms typing delays for a shorter acceptance run, a 42-line reference prefix, and two 3-second browser visits at lines 20 and 40. Observed clicks were the folder icon at 116,545; address bar at 748,163; and VS Code file tab at 501,51. These coordinates came from current control bounds and are not stored in the production script. The test created:

```text
C:\Users\hinatardp\Desktop\Project for vehicle\vehicle_lookup_23584_1223423281.py
```

Independent comparison confirmed all 1,044 source characters match. Both browser visits restored the editor and source entry continued. Live testing caught Windows Terminal briefly returning focus to Explorer after startup; the implementation now allows the host to settle and rechecks its uniquely titled foreground window before entering file commands. The complete 219-line workflow with the slower production settings has not been replayed. Physical pause/stop delivery, moved desktop icons, mixed DPI, and other console-host configurations remain manual acceptance checks.

Two END-only startup runs also confirmed that `AUTO` selected different logged action and typing seeds on each run. These checks generated no desktop input. The final native build, strict GCC build with warnings treated as errors, and all 13 CTest cases passed.

## code . and consistent slow input

The current desktop action types exactly `cmd` in Explorer's address bar and `code .` in Command Prompt. It locates the Explorer folder header, hovers to expose the toolbar, clicks the exact **New File...** button, and types the fresh filename inside VS Code. This disambiguates the folder toolbar from the separate Welcome-page New File button. Console commands no longer create the output file or contain long VS Code paths.

The normal defaults and bundled samples use 140-300 ms character delays, 900-2,200 ms line delays, 6-14 second pauses at boundaries after 160-320 characters, 1.4-3 second action pauses, and 1-1.8 second mouse travel. Browser launch and the original coding setup no longer suspend typing pacing. Individual keys and shortcuts include bounded holds/settles; releases finish after a focus-changing press. Mouse clicks use the same paced controller, and multi-notch scrolls are delivered in spaced steps. A fake receiver verifies the keyboard sequencing and interruption contracts without injecting input.

Live checks of the exact `code .` workflow use these production defaults with a short reference function, rather than the earlier accelerated screen-test profile. The script omits timing overrides. The editor's Settings UI applies workspace settings specifically to Plain Text: auto-indent **none**, Enter suggestion acceptance **off**. This avoids language-specific defaults overriding a general editor setting and accepting a completion instead of a newline. The editor is maximized through Alt+Space, X; Win+Up could open Snap Assist when the existing window was already snapped.

The full current workflow completed at 06:47 local time with exit code 0, using the normal defaults throughout command entry, the filename, Settings, and source entry. The reference was the seven-line `unique_object` function from `References/scripts/vehicle-lookup.py`. Independent comparison confirmed all 203 characters, including exact indentation and newlines after CRLF normalization. Output:

```text
C:\Users\hinatardp\Desktop\Project for vehicle\vehicle_lookup_22168_1225754359.py
```

The live run included an 11.3-second presentation pause during source entry, and a screenshot confirmed the entered indentation. No accelerated timing profile was used for this live run. The full 219-line source at these settings, and browser startup with the newly retained typing delays, have not been replayed in full. Browser scrolling/window return passed earlier live checks; the launch path now uses the same tested typing controller without timing overrides.
