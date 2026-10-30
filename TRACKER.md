# Build tracker

Small, shippable tasks broken out from [task.md](task.md). Each one should fit in
one sitting and end with something you can run, test, or dry-run.

**How to use:** change a task's status in place, add a short note when you finish
or get stuck, and link the commit. Phases run roughly in order; later phases plug
into the C pieces built in Phase 1.

Status: `[ ]` to do · `[~]` doing · `[!]` blocked · `[x]` done
Size: **S** ≈ under an hour or two · **M** ≈ half a day to a day · **L** ≈ several days
`needs Dn` = waiting on an open decision at the bottom of this file.
Sources (O1–O3, N1–N3) refer to the photos listed in task.md.

## Start here

1. **T1** profile config schema → **T2** dry-run prints it
2. **T3** activity interface, wrapping the existing coding and browser visit code
3. **T4** scheduler dry-run timeline

These three give every later task somewhere to plug in, and none of them sends
desktop input, so you can test them entirely with `--dry-run-config`.

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
| [ ] | T8 | Write a bot status file (current activity, state, last error) every few seconds. This is groundwork for the manager. | S | O1 | |

## Phase 2: Recording lifecycle (Loggy3)

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [~] | T9 | Port Loggy3 show / maximize / minimize / close from the Python prototype to C. | M | O1, N1 | `src/loggy3.c`, `start-loggy3.cmd` show, minimize, close, demo. Taskbar icon, then title-bar buttons by accessible name, with the profile's mouse hand. Close refuses while recording. Not live-tested. |
| [x] | T10 | Port recording-state inspection (accessibility) to C, with a status-only CLI flag. | M | N1 | `tutorial --loggy3 status`: no input, works while minimized. Read Start and `--:--:--` live on 2026-10-28. |
| [~] | T11 | Add Start/Stop recording actions in C, with dry-run and hover-only modes. | M | N1, N3 | `--loggy3 record` and `stop` with `--dry-run` and `--hover-only`. A real mouse click on the button (checked under the pointer first), sent once, never retried. Dry runs and hover-only checked live on 2026-10-28 (rested on Start 8 s, still stopped). Real Stop click checked live: stopped a 42 s recording, confirmed within 1 s. A real Start click not yet. |
| [x] | T12 | Live test one real Start → Stop transition and record the result here. | S | | 2026-10-28/29: the script clicked Stop (confirmed within 1 s) and Start (confirmed within 3 s, timer running). |
| [ ] | T13 | Session start sequence: optionally open a chosen app, start recording, optionally close that app, then begin tasks. | M | N1, N3 | Planned: see "Session plan", step 1. Built as T62. |
| [ ] | T14 | Session end sequence: save outputs, close all / one / no apps, stop recording, in the configured order. | M | N1, N3 | Planned: see "Session plan", step 4. Built as T63. |

### Session plan (agreed 2026-10-28, not built yet)

One launcher runs a whole recorded day: `start-session.cmd [PROFILE]`, then `switch-to-console.cmd` as now.

**Decisions**
- Main task: coding. Alternative task: browsing (for now).
- Time comes from Loggy3: today's total from its Sessions page (UTC day) plus the running timer.
- Daily target: random 10h15–10h45.
- Telegram stop: finish the current line, save, then end.
- Loggy3 stops by itself: auto-recover (click Start again, alert on Telegram).
- Launched when today is already close to the target: record only what remains.
- Crossing Loggy3's day boundary (00:00 UTC, 8 AM for the owner): keep going until the target. The session counts from its launch snapshot; Loggy3 splits the hours between its two days (confirmed 2026-10-29).
- Telegram summary at the end: yes.
- Start and end looks are randomized so days don't look alike. All percentages below are placeholders for the config discussion (D4).

**0. Prepare**
- Read Loggy3: open, recording, today's total (T0). Not open: open it from the Start menu.
- Roll today's target. Remaining = target − T0.
- Already at the target: press nothing, send Telegram "already done today", exit.
- Already recording (for example after a crash restart): don't click Start, carry on.
- State unknown or busy for more than 10 s: don't start, alert.
- The count is a snapshot at launch, so Loggy3's midnight-UTC rollover (5 PM Pacific) doesn't reset the session.

**1. Start look** (rolled each day)

| Look | Odds | On screen before Start |
|---|---|---|
| Empty desktop | 25% | Win+D, or minimize what's open. |
| VS Code already open | 35% | Open or focus VS Code with the project. |
| Browser open | 20% | Chrome on a random catalog site. |
| A random app | 20% | Explorer, Notepad, or Calculator. Afterwards closed (50%) or left open (50%). |

Then:
1. Click Loggy3's taskbar icon.
2. Go to Start: straight to the button, or look over the window first (50/50).
3. Click once and confirm the button reads Stop with the timer running.
4. Put Loggy3 away: minimize (60%) or just switch to the work (40%).

First task: coding (80%) or browsing (20%).

**2. Work loop**

| Part | Range | Notes |
|---|---|---|
| Coding slot | 40–80 min | Breaks count inside the slot. Ends by finishing the line and saving. Resumes there next slot. Next project when one is done. |
| Browsing slot | 10–20 min | Afterwards Chrome is minimized (50%), closed (20%), or left behind VS Code (30%). |
| Other-app break | 5–20 min | Checked every 8–15 min, 20% chance. Explorer, Calculator, images, or Notepad, by weight. Closed (80%) or minimized (20%). |
| Slot error | — | Up to 5 recoveries, then that slot ends early and the loop continues. |

Rough split: coding 75–80%, browsing 10–15%, other apps the rest.

**3. Watchers** (every few seconds, in every step)

| Event | Reaction |
|---|---|
| Telegram stop | Finish the line, save, go to step 4. |
| Telegram pause / resume | The automation freezes; Loggy3 keeps recording. |
| Target − estimated ending time reached | Go to step 4, so Stop lands near the target. |
| Loggy3 stopped by itself | Save, alert, click Start once, confirm. If it doesn't take within 20 s, try once more 2 min later. If that also fails, end the day with an alert. Re-read T0 after the restart. |
| Desktop locked or RDP disconnected | Wait as now. Loggy3 time still counts. |

**4. End look** (weights vary a little each day)

| Ending | About | Steps |
|---|---|---|
| Direct stop | 35% | Save, then Stop. Everything else stays as it is. |
| Close everything | 30% | Save, close each open app in random order, then Stop. |
| Close 1–2 apps | 35% | Save, close 1–2 random apps, then Stop. |

- How each app is closed: X button 40%, Alt+F4 35%, taskbar right-click "Close window" 25%.
- VS Code: always saved first; an "unsaved" prompt is answered Save.
- Approach to Stop: straight or look over the window first (50/50).
- Afterwards: Loggy3 is left in front (40%), minimized (40%), or its window closed (20%).
- Confirm the button reads Start, then log and send the summary.

**Example timeline** (one possible day)

```
07:00:10      —     prepare: T0 0h00m, target 10h31m
07:00:11      —     start look: VS Code already open
07:00:21      —     Loggy3 taskbar → straight to Start → click
07:00:25  0:00:01   confirmed recording → minimize Loggy3
07:00:30  0:00:05   CODING 63 min (07:24 Calculator break 7 min)
08:03:34  1:03:09   BROWSING 14 min
08:17:36  1:17:11   CODING 71 min ... alternating ...
11:47:12  4:46:47   Loggy3 stopped by itself → save, alert, Start, confirmed
17:00:00  9:59:35   Loggy3 day rolls over (ignored: launch snapshot)
17:27:50 10:27:25   target near → ending: close 2 apps (Chrome via taskbar, Calculator via X)
17:28:28            Stop → confirmed 10:31:04 → Telegram summary → exit
```

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [x] | T57 | Read today's recorded total from Loggy3's Sessions page (UTC day row) plus the running timer. Read-only; check live whether the day row already includes the running session. | S | | `loggy_today` / `loggy_today_seconds` in `src/loggy3.c`; `--loggy3 status` prints it. Checked live 2026-10-29: the day row counts finished sessions only (stayed 48s while recording 1m18s), so today = row + running timer. Row only shown on the Sessions page, current period; totals are per Loggy3 account (all computers). |
| [x] | T58 | `profiles/session.json` (target range, slot lengths, break odds, start/end weights) and `--dry-run-session ID` printing the day's planned timeline. | M | | `profiles/session.json`, `tutorial --dry-run-session ID [--seed N]` prints the day (reads today's total from Loggy3, no input). 16405 plan checks. |
| [~] | T59 | Time-limited slots: coding and browsing take a budget and stop at a safe point (line finished, file saved). | M | | Coding stops at a safe point (finishes the line, saves, `ended_early`) and reuses an open VS Code window; browsing and side-app breaks end on `playback_yield`. Not live-tested. |
| [~] | T60 | Session loop: coding ⇄ browsing with other-app breaks (5–20 min), recorded-time accounting, ending started early. | M | | `src/work_session.c`: slots alternate, shared side-app timer (breaks 5-20 min), recorded time = today's row at launch + banked + Loggy3 timer. Not live-tested. Idles away from the desk: about 5% of the day (scaled 0.8-1.2 by the profile's randomness score), 1-6 min every 20-40 min of work at a planned chance, counted in the target; listed in the dry run. |
| [~] | T61 | Watchers: Telegram stop (finish line, save, end), Loggy3 stopped by itself (auto Start, alert), desktop locked. | M | | Telegram /stop becomes a wind-down in sessions (`soft_stop`); watcher every 15 s; Loggy3 stopped twice in a row → Start again (retry after 2 min), alert. Not live-tested. |
| [~] | T62 | Start looks (empty desktop, VS Code, browser, random app) and the Loggy3 Start sequence. | M | | Replaces T13. Start looks and Loggy3 Start (opens the Sessions page if needed). Not live-tested. |
| [~] | T63 | End looks (direct, close all, close 1–2), app closing by X / Alt+F4 / taskbar menu, Loggy3 after-stop. | M | | Replaces T14. End looks; apps closed by X / Alt+F4 / taskbar menu "Close window"; save prompts answered (VS Code: Save). Not live-tested. |
| [~] | T64 | Session summary in the log and on Telegram. `start-session.cmd`. Short live test (about 15 min target), then a full day. | M | | `SESSION SUMMARY` / `SESSION ALERT` lines, forwarded to Telegram by the monitor. `start-session.cmd`, short test day options. Live test still to do. |
| [x] | T65 | Telegram `/screenshot` and `/status` include Loggy3 (recording or not, timer), today's recorded hours from the app, and the current task (for example "coding kruzpayr, src/App.jsx, 34 of 63 min"). | S | | The monitor reads Loggy3 itself (read-only, accessibility); the session writes its current task to a small state file. Uses T57. Monitor reads Loggy3 itself (state, timer, today's hours) and the session's `SESSION TASK` / current file. `monitor --status` checked locally. Restart the monitor to load it. |

## Phase 3: Browser

Run with `start-browsing.cmd [PROFILE_ID] [MINUTES]`; preview with `tutorial.exe --dry-run-browse 1 --seed 7`. See TutorialAutomation/README.md, "Profile browsing session".

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [~] | T15 | Browser window open / close / minimize as separate actions, distinct from tab actions. | S | O1, N3 | Opening from the taskbar icon works (`browser_open_window`). Close and minimize actions not done yet. |
| [x] | T16 | Site catalog file with categories (programming/docs, GitHub, lifestyle, social). Start with about 30 sites. `needs D12` for "dribble" | S | O1 | `profiles/browsing.json`: Superseded by T17. Reddit removed on request, so no social media. "dribble" left out until D12. |
| [x] | T17 | Grow the site catalog to 100 sites. | S | N3 | 100 sites: 50 programming (31 docs, 6 GitHub, 13 blogs) and 50 others (food, home/plants, beauty/yoga, photography, travel, books, movies/K-dramas, games, tech/general news, science). All load without a login wall or bot challenge, checked 2026-10-28. |
| [x] | T18 | Search topics driven by profile interests (questions, quotes, songs, apps, Loggy3). | S | O3, N1 | Question, phrase, song lyrics, apps, and Loggy3 templates filled from profile interests. |
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

## Phase 8: Manager and bots

Two roles (see task.md, "Roles: manager and bots"):
- **Bot:** a machine or RDP session that runs the automation: `tutorial.exe`, Loggy3, and one profile. There are about 20.
- **Manager:** one separate place that keeps the list of bots. It asks any bot for a screenshot or its status, sends start, pause, resume and stop, and receives the bots' reports and alerts.

Today only the bot side exists: each bot's Telegram monitor reports on its own.

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [ ] | T49 | Short design note: the manager and bot roles, how bots reach the manager, and how they authenticate. `needs D14, D15` | S | O1 | |
| [ ] | T66 | Bot list in the manager: name, machine or RDP session, profile, last seen, online or offline. | S | | |
| [ ] | T67 | Bot agent: the bot side of the link. Reuses the monitor's readers: Loggy3 state and timer, today's hours, current task, screenshot. | M | | Builds on `monitor.exe` (T65). |
| [ ] | T51 | Bots report status, current activity, errors, and alerts to the manager on a schedule and on change. | M | O1 | Builds on T8/T65. |
| [ ] | T68 | The manager asks one bot, or all, for a screenshot and status, and shows them side by side. | M | | |
| [ ] | T50 | The manager starts, stops, pauses, resumes, and interrupts a bot's session (same meanings as Telegram /run, /stop, /pause, /resume). | L | O1, O2 | |
| [ ] | T52 | Assign a profile and activity mix to each bot from the manager. | S | O1 | |
| [ ] | T53 | Error policy: which errors allow recovery, which stop the bot, and what is reported to the manager. `needs D3` | M | O2 | |

## Phase 9: Perception experiments

| Status | ID | Task | Size | Source | Notes |
|---|---|---|---|---|---|
| [ ] | T54 | Audit accessibility coverage for every action above. List targets it can't find, including when windows open small or moved. | M | O2 | |
| [ ] | T55 | "Python eye" experiment: try visual targeting only for the gaps from T54, then decide whether a model is needed and where it runs. | L | O2 | |

---

## Open decisions

Answer these in place. When one is settled, change `[ ]` to `[x]` and remove the
matching `needs` note from its tasks.

| Status | ID | Question | Source | Blocks |
|---|---|---|---|---|
| [~] | D1 | RDP 1 = "record", RDP 2 and 3 = "bot"? Confirm each session's role. | O1 | T49 |
|  |  | **Partly answered (2026-10-29):** the roles are manager and bot. Still to confirm whether "record" in the drawing means the manager. | | |
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
| [ ] | D14 | Where does the manager run: a separate machine, one of the RDP sessions, or a web or Telegram group? | Owner | T49 |
| [ ] | D15 | How do bots and the manager talk: through Telegram (one bot token per bot, or one group), a small server the bots call, or a shared folder? | Owner | T49, T51 |
| [ ] | D13 | Formula writing is crossed out. Do formulas belong in the integrated Excel activity? | O1 | T33 |
