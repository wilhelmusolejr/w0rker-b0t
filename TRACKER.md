# Build tracker

Small, shippable tasks broken out from [task.md](task.md). Each one should fit in
one sitting and end with something you can run, test, or dry-run.

The [root README](README.md) describes the implemented first version of the
ordered JSON flow. This tracker still contains the earlier profile-driven
backlog; its old scheduler tasks are historical and have not been recast as
flow-runner tasks.

Active scope: one interactive Windows session for personal learning in
accessibility automation and an AI helper. The current player follows configured
actions; live desktop behavior and broader AI-assisted decisions remain separate
validation and research tasks.

**How to use:** change a task's status in place, add a short note when you finish
or get stuck, and link the commit. Phases run roughly in order; later phases plug
into the C pieces built in Phase 1.

Status: `[ ]` to do · `[~]` doing · `[!]` blocked · `[x]` done
Size: **S** ≈ under an hour or two · **M** ≈ half a day to a day · **L** ≈ several days
`needs Dn` = waiting on an open decision at the bottom of this file.
Sources (O1–O3, N1–N3) refer to the photos listed in task.md.

## Current JSON flow

`tutorial.exe --validate-flow FILE`, `--dry-run-flow FILE`, and `--flow FILE`
validate, preview, and run an ordered step list. The [10-minute browsing
example](TutorialAutomation/scripts/browse-10m-flow.json) is a short input-free
validation target; the [workday example](TutorialAutomation/scripts/workday-flow.json)
uses step checkpoints. Coding, browsing, Notepad, Calculator, app-window actions,
and explicit Loggy3 actions are supported. Loggy3 is never started implicitly by
the flow. See the [flow guide](TutorialAutomation/README.md#json-work-flows).

The JSON parser and dry runs are tested. Short browsing-flow and full workday
execution on a live desktop remain acceptance work. AI generation of JSON, Excel
as a flow action, and the older backlog's broader scheduling ideas are not yet
implemented in this runner.

## Earlier profile-driven backlog

The sequence below is the original plan for the profile and combined-session
modes. The current JSON flow runner already wraps some of these activities;
unchecked items here do not describe missing flow steps.

1. **T1** profile config schema → **T2** dry-run prints it
2. **T3** activity interface, wrapping the existing coding and browser visit code
3. **T4** scheduler dry-run timeline

These were the original foundation tasks and remain for historical tracking.

---

## Phase 1: Foundations (C player)

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [x] | T1 | Profile database `TutorialAutomation/profiles/profiles.json` (name, gender, local ID, occupation, interests, hobbies, main/side tasks, randomness score, typo %, computer name). Parse and validate only; no behavior yet. | S | N1, N3 | `src/profile.c`, `tests/profile_tests.c`. Profiles 1 (Wilhelmus) and 2 (Williana). |
| [ ] | T2 | Make `--dry-run-config` print the loaded profile. Add a test in `reference_config_tests.c`. | S | | |
| [ ] | T3 | Define an activity interface in C (start / step / yield / resume / stop) and wrap the existing reference typing and browser visit as two activities. No behavior change. | M | | |
| [ ] | T4 | Session scheduler: main, side, and idle budgets with a seed. Dry-run prints the planned timeline. `needs D5, D6` for the default percentages; build with placeholder values. | M | N1, N3 | |
| [ ] | T56 | Make the profile the entry point (`tutorial.exe --profile 1`) and retire `job.json`: its typing speed moves into the profile, its source/destination and browser mix become programming-task settings. | M | | After T3/T4, so nothing breaks before the replacement works. |
| [ ] | T5 | Move shared pacing (pause ranges, mouse-move time, cursor paths) into one `pace` section used by every activity. | S | O2 | |
| [ ] | T6 | Focus guard: check the foreground window before each input batch. On mismatch, Alt+Tab back, verify, then resume or stop. | M | O2, N2 | |
| [ ] | T7 | Interruptibility audit: confirm F8/F9/F12 work during typing, idle waits, app switches, and recovery waits. Fix any gaps. | S | N3 | |
| [ ] | T8 | Write a local session status file (current activity, state, last error) every few seconds for troubleshooting and the optional monitor. | S | O1 | |

## Phase 2: Recording lifecycle (L3)

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [~] | T9 | Port L3 show / maximize / minimize / close from the Python prototype to C. | M | O1, N1 | `src/loggy3.c`, `start-loggy3.cmd` show, minimize, close, demo. Taskbar icon, then title-bar buttons by accessible name, with the profile's mouse hand. Close refuses while recording. Not live-tested. |
| [x] | T10 | Port recording-state inspection (accessibility) to C, with a status-only CLI flag. | M | N1 | `tutorial --loggy3 status`: no input, works while minimized. Read Start and `--:--:--` live on 2026-10-28. |
| [~] | T11 | Add Start/Stop recording actions in C, with dry-run and hover-only modes. | M | N1, N3 | `--loggy3 record` and `stop` use a checked button click once. Both real Start and Stop transitions were confirmed in 2026-10-28/29; the combined session is not live-tested. |
| [x] | T12 | Live test one real Start → Stop transition and record the result here. | S | | 2026-10-28/29: the script clicked Stop (confirmed within 1 s) and Start (confirmed within 3 s, timer running). |
| [~] | T13 | Session start sequence: prepare a chosen app, attempt recording, then begin tasks. | M | N1, N3 | Implemented through T62; the session now continues when L3 cannot start. Live acceptance remains. |
| [~] | T14 | Session end sequence: save outputs, close selected apps, and stop a confirmed recording. | M | N1, N3 | Implemented through T63; recorder state may require manual checking if unavailable. Live acceptance remains. |

### Combined accessibility session (implemented, live acceptance pending)

`start-session.cmd 1 --duration-minutes 20` selects a 20-minute local session
budget from launch. L3 recording is attempted, but its total cannot stop
this run. Preparation, work, planned idles, app breaks, and ending share the
budget; explicit pauses are excluded. A safe save or slow UI step can finish
slightly after the deadline.

Without a duration override, `profiles/session.json` selects a daily L3
target between 10h15m and 10h45m. Time already recorded in its UTC day counts.
If L3 cannot be opened, read, or started, the session keeps working. It
switches once to a local countdown for the remaining amount from the last
confirmed total. If no total was readable, it uses the full selected target
and reports that assumption. Later recorder recovery does not move the deadline.
`--target-minutes N` remains a daily-total override and is mutually exclusive
with `--duration-minutes N`.

| Phase | Current behavior |
|---|---|
| Prepare | Read L3 when available; keep the desktop guards and choose one timing mode. |
| Work | Alternate coding and browsing; side-app breaks and session idles count toward the selected limit. |
| Recorder failure | Alert, switch the default clock to local fallback, and retry recording at most twice at safe points. Work continues. |
| End | Finish a safe coding line, run the selected end look, wait to a local deadline if ending finishes early, and stop a confirmed recording. |
| Controls | Telegram `/stop` winds down; F9/F12 stop immediately; F8 and remote pauses suspend local active time. |

The older combined session is scripted and profile driven. Windows accessibility
provides control names, states, and rectangles; foreground and desktop checks
guard each input. Logs and saved-output verification show what actually ran.
See [the session guide](TutorialAutomation/README.md#combined-work-session).
The combined session, especially L3 failure and recovery, has not yet been
validated in a live desktop run.

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [x] | T57 | Read today's recorded total from L3's Sessions page (UTC day row) plus the running timer. Read-only; check live whether the day row already includes the running session. | S | | `loggy_today` / `loggy_today_seconds` in `src/loggy3.c`; `--loggy3 status` prints it. Checked live 2026-10-29: the day row counts finished sessions only (stayed 48s while recording 1m18s), so today = row + running timer. Row only shown on the Sessions page, current period; totals are per L3 account (all computers). |
| [x] | T58 | Session settings and input-free dry-run timeline. | M | | `profiles/session.json` sets the daily target and slot lengths. `tutorial --dry-run-session ID --duration-minutes 20` prints a duration plan without reading L3 or requiring downloaded projects; the default preview reads today's total when available. |
| [~] | T59 | Time-limited slots: coding and browsing take a budget and stop at a safe point (line finished, file saved). | M | | Coding stops at a safe point (finishes the line, saves, `ended_early`) and reuses an open VS Code window; browsing and side-app breaks end on `playback_yield`. Not live-tested. |
| [~] | T60 | Combined session loop and two timing modes. | M | | `src/work_session.c` alternates coding and browsing. `--duration-minutes N` uses a local clock; the default uses the daily L3 target and switches to a local remaining-time countdown after recorder failure. Session idles use active local time. Input-free clock tests pass; live acceptance remains. |
| [~] | T61 | Watcher and recorder-failure recovery. | M | | Every 15 s the watcher checks the selected deadline and L3 read-only. Missing, stopped, or unknown recording triggers a local fallback and at most two spaced retries. Work continues; Telegram `/stop` winds down. Live acceptance remains. |
| [~] | T62 | Start looks (empty desktop, VS Code, browser, random app) and the L3 Start sequence. | M | | Replaces T13. Start looks and L3 Start (opens the Sessions page if needed). Not live-tested. |
| [~] | T63 | End looks (direct, close all, close 1–2), app closing by X / Alt+F4 / taskbar menu, L3 after-stop. | M | | Replaces T14. End looks; apps closed by X / Alt+F4 / taskbar menu "Close window"; save prompts answered (VS Code: Save). Not live-tested. |
| [~] | T64 | Session summary, alerts, and short live acceptance run. | M | | `SESSION SUMMARY`, `SESSION ALERT`, and `SESSION CLOCK` lines feed the optional monitor. A 20-minute `--duration-minutes 20` run is the intended short acceptance check; live validation is pending. |
| [x] | T65 | Telegram `/screenshot` and `/status` include recorder state, selected session clock mode, and current task. | S | | The monitor reads L3 through accessibility and follows `SESSION TARGET`, `SESSION CLOCK`, and `SESSION TASK` log lines. `monitor --status` is input-free. |

## Phase 3: Browser

Run with `start-browsing.cmd [PROFILE_ID] [MINUTES]`; preview with `tutorial.exe --dry-run-browse 1 --seed 7`. See TutorialAutomation/README.md, "Profile browsing session".

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [~] | T15 | Browser window open / close / minimize as separate actions, distinct from tab actions. | S | O1, N3 | The JSON flow supports `app.ensure_open` and `app.close` for browser windows; a separate browser-minimize flow action is still missing. |
| [x] | T16 | Site catalog file with categories (programming/docs, GitHub, lifestyle, social). Start with about 30 sites. `needs D12` for "dribble" | S | O1 | `profiles/browsing.json`: Superseded by T17. Reddit removed on request, so no social media. "dribble" left out until D12. |
| [x] | T17 | Grow the site catalog to 100 sites. | S | N3 | 100 sites: 50 programming (31 docs, 6 GitHub, 13 blogs) and 50 others (food, home/plants, beauty/yoga, photography, travel, books, movies/K-dramas, games, tech/general news, science). All load without a login wall or automated-access challenge, checked 2026-10-28. |
| [x] | T18 | Search topics driven by profile interests (questions, quotes, songs, apps, L3). | S | O3, N1 | Question, phrase, song lyrics, apps, and L3 templates filled from profile interests. |
| [x] | T19 | Per visit, choose direct URL entry or Google search then open a result (extend the existing research visit). | S | N1 | `direct_site_percent` (50%) per tab. |
| [~] | T20 | Multi-tab visit: random 1–5 tabs with a search in each. Define return and close rules. `needs D7, D8` | M | N1 | 1-5 tabs filled and revisited. Closing rule is provisional until D8. Not live-tested. |
| [~] | T21 | Optional new-tab search during another visit, then return to the original tab. `needs D7` | S | N1 | 50% per tab visit (my reading of D7). Not live-tested. |
| [~] | T22 | Bookmark creation (Ctrl+D) and a check that it persisted. | S | O3 | Ctrl+D then Enter. Persistence check not done. Not live-tested. |
| [~] | T23 | Port the browser-explorer link discovery to C so it picks random eligible links from live accessibility data. | L | O3 | C port in `src/web_page.c` + `src/web_policy.c`; links only (no expandable buttons yet). Not live-tested. |
| [~] | T24 | Before choosing the next action, handle navigation, same-page changes, expanded sections, dialogs, and new tabs. | M | | Dialogs dismissed with Escape, new tabs and content changes detected. Expandable sections not handled. |
| [ ] | T25 | Visual review of scroll speed, reading pauses, and moving away from clicked controls. Tune the defaults. | S | O2 | |

## Phase 4: Programming

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [x] | T26 | Define 2–3 reference project structures (folders plus files) as config. | S | N2 | `programming_files/kruzpayr` is the first reference project; add more as folders there. |
| [~] | T27 | Prepare a new destination folder, or copy a selected folder structure, with clear source and output paths. | M | N2 | `--code`: creates the project folder (desktop or Explorer) and subfolders with mkdir in cmd. Not live-tested. |
| [~] | T28 | Create a new demo file in the chosen project and type code into it (reuse the reference typing). | S | O1 | Types every text file through VS Code with checkpoints, resume, and per-profile project history; copies images/lock files. Not live-tested. |
| [ ] | T29 | Delete a generated demo file as its own configured action. | S | N2 | |
| [ ] | T30 | Open or adjust editor settings as a scenario action. List the settings used. | S | N2 | |
| [ ] | T31 | Generalize the split-window layout: coding on either side, focus verified when switching between main and side tasks. | M | N3 | |

## Phase 5: Excel and other apps

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [ ] | T32 | Excel activity: open a selected workbook and close it, wired into the scheduler. | S | O1 | |
| [ ] | T33 | Decide what happens between Excel open and close, and reuse the Excel demo for it. `needs D13` | M | O1 | |
| [~] | T34 | Notepad activity that types configured example notes. | S | N2 | Side-app break: Win+R notepad, types profile notes, saves to Documents\Notes. Not live-tested. |
| [~] | T35 | Image-viewing activity using example files and a chosen viewer. | S | N3 | Side-app break: opens pictures from File Explorer, arrow keys through them. Not live-tested. |
| [ ] | T36 | Choose the CAD app and write down a concrete action sequence (design only). | S | N3 | |
| [ ] | T37 | Implement the CAD activity from T36. | M | N3 | |
| [~] | T38 | Configured app list and random selection from the taskbar, with return behavior. | M | O1, N2 | `profiles/apps.json`: 20% chance every 8-15 main-task minutes, 10-20 minute break; Notepad, File Explorer, Calculator, images. Opened via Win+R / Win+E rather than taskbar icons. Not live-tested. |

## Phase 6: Desktop and personalization

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [ ] | T39 | Config for coding and Excel desktop folder groups (names and contents). `needs D2` | S | O2, N2 | |
| [ ] | T40 | Pick a coding folder from its group and use it as the reference-typing destination. | S | O2 | |
| [~] | T41 | Side activity: open a configured file or folder (separate from the coding destination). | S | N2 | File Explorer break navigates real folders (double-click, back, scroll). Opens no files. Not live-tested. |
| [ ] | T42 | Pinning activity: eligible items, target location, cleanup. `needs D11` | S | N2 | |
| [ ] | T43 | Wallpaper selection, plus optional restore at session end. | S | N2 | |

## Phase 7: Profile behavior

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [~] | T44 | Typing errors and corrections at the profile's mistype level, keeping final-content and checkpoint verification. | M | N1 | `mistype_level` 1–10 maps to a 2–20% chance of first typing an eligible letter as a neighbouring key, then Backspace and the right letter (coding and all other typing). Not live-tested. |
| [ ] | T45 | Map the randomness score to concrete playback choices (tab count, dwell time, pauses). `needs D4` | M | O3, N1 | |
| [ ] | T46 | Weekday / weekend schedules (weekday = main task, weekend = main or side). | S | N1 | |
| [ ] | T47 | Choose whether to start with the main or side task, and the initial visible state (desktop, folder, CMD, app). `needs D6` | S | N3 | |
| [ ] | T48 | Apply computer name per the decision (metadata or a real Windows setting). `needs D10` | S | N3 | |

## Phase 8: Perception and recovery experiments

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [ ] | T53 | Local error policy: which errors allow recovery, which stop playback, and what is written to the session log or optional monitor. `needs D3` | M | O2 | |
| [ ] | T54 | Audit accessibility coverage for every action above. List targets it can't find, including when windows open small or moved. | M | O2 | |
| [ ] | T55 | "Python eye" experiment: try visual targeting only for the gaps from T54, then decide whether a model is needed and where it runs. | L | O2 | |

---

## Open decisions

Answer these in place. When one is settled, change `[ ]` to `[x]` and remove the
matching `needs` note from its tasks.

| Status | ID | Question | Source | Blocks |
|---|---|---|---|---|
| [ ] | D2 | Does the desktop grid mean icon layout, activity groups, or both? ("Start 5", "Key points", x-y grid, "random file…") | O2 | T39 |
| [ ] | D3 | What should the cursor do while an error is being handled? | O2 | T53 |
| [ ] | D4 | Randomness score: formula, inputs, and effect on playback. Currently it only raises the mouse hand's overshoot rate and wobble (`motion_hand_for`). | O3, N1 | T45 |
| [x] | D5 | Main 70–80%, side "rest", idle 5%: is idle separate, and how do these total 100%? | N1 | T4 |
|  |  | **Answer (2026-10-29):** idle is separate: about 5% of the day away from the desk, counted in the recording target. Main/alternative split is coding 40-80 min / browsing 10-20 min slots. | | |
| [ ] | D6 | Main-first / side-first 80/20: is that the chance of the first task, not time split? How does it relate to D5? | N3 | T4, T47 |
| [ ] | D7 | Browser "50% randomness": does it mean a new-tab search, an off-topic pick, or something else? | N1 | T20, T21 |
| [ ] | D8 | The wording after the 1–5 tabs note: how are tabs closed? | N1 | T20 |
| [x] | D9 | Recording order: what is visible when recording starts, and is cleanup before or after stop? | N1, N3 | T13, T14 |
|  |  | **Answer (2026-10-28):** both randomized. Start: empty desktop, VS Code already open, browser open, or a random app. End: direct stop, close everything, or close 1–2 apps, then Stop. See "Session plan". | | |
| [ ] | D10 | "Computer name": config metadata or an actual Windows rename? | N3 | T48 |
| [ ] | D11 | "Pin random items": pin where (taskbar, Start, Quick Access) and which item types? | N2 | T42 |
| [ ] | D12 | Does "dribble" mean Dribbble? | O1 | T16 |
| [ ] | D13 | Formula writing is crossed out. Do formulas belong in the integrated Excel activity? | O1 | T33 |
