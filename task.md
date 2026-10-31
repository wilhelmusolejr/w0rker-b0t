# Accessibility automation work plan

Updated: 2026-10-31

This backlog combines all six reference photos: three original notebook pages
and three newer planning images. Repeated items are merged by capability.
Unchecked items describe planned implementation, integration, or validation;
they do not imply that the capability is absent from every existing prototype.
Unclear or conflicting details are collected under **Unconfirmed reference notes**.
Source IDs below refer to the linked photos at the end of this file.

The active project is a personal, single-session accessibility automation and AI
helper learning environment: reference typing, application navigation, window
layout, browser exploration, and result verification. The implemented player is
scripted and profile driven in C; Python experiments test targeting concepts.

## Existing starting point

| Component | Current starting point | Remaining distinction |
|---|---|---|
| Native player | C desktop-folder, typing, browser, and saved-file verification workflows. | Integrate the combined scenario configuration and additional activities. |
| Taskbar targeting | [Python prototype](experiments/taskbar-pointer-demo/README.md) locates app buttons and shows the desktop. | Reuse targeting concepts in the integrated workflow. |
| Browser exploration | [Python prototype](experiments/browser-explorer/README.md) discovers accessible controls, observes click outcomes, and varies movement, scrolling, and pauses. | Add the planned tab, bookmark, and scenario-selection behavior. |
| Split-window demo | [Separate sample](experiments/split-coding-browser/README.md) opens the desktop project in VS Code, types for one minute, browses for one minute, and returns to the editor. | Generalize its timing and layout for scheduled main/side tasks. |
| Loggy3 controls | [Prototype](experiments/loggy3-window/README.md) supports window control and state-aware recording commands. Window actions, status, dry-run, and hover-only behavior were tested live. | C integration exists; a real Start/Stop transition was checked on 2026-10-28/29. The combined work session still needs a live acceptance run. |
| Excel playback | [Separate demo](TutorialAutomation/EXCEL_DEMO.md) supports fictional workbooks, formulas, and result checks. | Review what to reuse in the combined workflow; formula integration is unresolved. |

## Current session scope

The active implementation runs `tutorial.exe` in one interactive Windows session.
The optional `monitor.exe` reports status, alerts, and screenshots for that session
and accepts commands from its configured Telegram chat. The combined session uses
an entered `--duration-minutes` as its stop clock; otherwise it follows the
Loggy3 daily target and switches to a local remaining-time countdown if the
recorder fails. Accessibility, foreground guards, and saved-output checks remain
part of the current workflow.

## Scenario profiles and shared playback settings

Sources: O2, O3, N1, N2, N3.

- [ ] Define a configurable scenario profile using the fields below. The newer
  profile list expands the older identity/scoring note; its scoring rules remain
  unresolved rather than being assumed from the field names.

| Proposed field from the notes | Configuration to define |
|---|---|
| Name, gender, local ID | Profile metadata and a stable local identifier. |
| Interests and hobbies | Topic inputs for side activities and browser searches. |
| Main task | The primary configured activity, with available side activities. |
| Randomness score | Its range, meaning, and mapping to playback choices. |
| Incorrect-typing percentage | Error frequency and correction behavior. |
| Computer name | Whether this is session metadata or an actual Windows-name change. |

- [ ] Apply profile settings consistently to activity selection, searches, tab
  counts, page residence times, cursor movement, and pause durations.
- [ ] Configure shared pause/idle intervals and cursor paths across desktop,
  browser, Excel, coding, and other selected applications.
- [ ] Coordinate movement, scrolling, pauses, and app switching through shared
  playback settings, with seeds for repeatable scenarios where supported.
- [ ] Define deliberate typing-error and correction sequences while retaining
  final-content and saved-checkpoint verification for reference playback.

## Scheduling and session lifecycle

Sources: N1, N3.

The implemented sequence attempts optional recording, starts an initial main or
side task, alternates coding and browsing, and ends at the selected time limit.
Remaining proposals below cover broader scheduling and activity choices.

- [ ] Define main-task, side-task, and idle/pause budgets, including how elapsed
  time is counted and how an activity yields or resumes.
- [ ] Support weekday/weekend schedules: the notes associate weekdays with the
  main task and weekends with either main or side tasks.
- [ ] Configure whether a session starts with its main task or a side task.
- [ ] Select the initial visible state: desktop, an open folder, Command Prompt,
  or another configured application.
- [ ] Define the pre-recording sequence: optionally open a selected app, begin
  recording, close that app if configured, then enter the task sequence.
- [ ] Define the end sequence: save outputs, optionally close all selected apps,
  close one, or close none, and stop recording in the configured order.
- [ ] Support stop/start recording across configured session boundaries; verify
  the app's reported state at each transition.
- [ ] Generalize the existing split-window layout for main and side tasks, with
  the coding app on either side and verified focus when switching activities.

## Application actions

### Loggy3 (L3)

Sources: O1, N1, N3. The newer notes explicitly name Loggy3.

- [ ] Integrate opening/showing, maximizing, minimizing, and closing Loggy3.
- [ ] Integrate recording-state inspection and the existing Start/Stop commands
  into the session lifecycle, then validate real recording transitions.

### Browser

Sources: O1, O3, N1, N3.

- [ ] Integrate browser-window opening, closing, and minimizing; keep these
  distinct from tab creation, tab closure, and website navigation.
- [ ] Build a configurable site/topic catalog. The newer whiteboard proposes
  **100 websites**, expanding the earlier notebook target of approximately 30.
- [ ] Include programming/documentation, GitHub, lifestyle, and social-media
  categories. Preserve the older "dribble" entry for clarification.
- [ ] Configure search topics such as questions, phrases/quotes, song-related
  queries, applications, and Loggy3, using the selected profile's interests.
- [ ] Support both direct website entry and Google search followed by a result.
- [ ] Add an optional search in a new tab during another browsing activity,
  then return to the original browsing context.
- [ ] Configure a random tab count in the proposed **1–5** range, with searches
  across those tabs; define tab retention, return, and closure rules.
- [ ] Add configurable bookmark creation, including how saved bookmarks persist.
- [ ] Randomize eligible navigation choices using current accessibility data
  and vary the time spent on each page and tab.
- [ ] Refine the existing fast/slow scrolling, reading pauses, and movement away
  from clicked controls through visual review.
- [ ] Handle navigation, same-page content changes, section expansion, dialogs,
  and new tabs before choosing the next action.

### Programming

Sources: O1, O2, N2, N3.

- [ ] Define reference project structures, including folders and code files, for
  the proposed project-copying demonstration.
- [ ] Support preparing a new destination folder or copying a selected folder
  structure, with clear source and generated-output paths.
- [ ] Create a new demo file and enter code in the selected project folder.
- [ ] Delete a generated demo file as a separate configured action.
- [ ] Open or adjust editor settings; specify the settings used by the scenario.

### Excel

Source: O1; implementation starting point linked above.

- [ ] Open and close a selected workbook/file.
- [ ] Define the Excel activity between opening and closing, and review the
  separate simulation for reuse in the combined workflow.

### Notepad, CAD, and image viewing

Sources: N2, N3.

- [ ] Add a Notepad activity that enters configured example notes.
- [ ] Define the target CAD application and a concrete action sequence; the
  whiteboard names CAD without specifying its operations.
- [ ] Add image viewing using selected example files and a defined viewer.

### Other application selection

Sources: O1, N2, N3.

- [ ] Define the available application list and taskbar-based random selection.
- [ ] Specify actions, completion checks, and return behavior for each additional
  application, reusing the shared targeting and lifecycle controls.

## Desktop folders, files, and personalization

Sources: O2, N2, N3.

- [ ] Define the coding and Excel desktop-folder groups, their contents, and names.
- [ ] Select a folder from the appropriate group and use the selected coding
  folder as the destination for generated reference-typing output.
- [ ] Add configured file/folder opening as a side activity, distinct from
  choosing a coding-output destination.
- [ ] Define which items can be pinned, where they are pinned, and the selection
  and cleanup rules for the proposed pinning activity.
- [ ] Add configurable wallpaper selection and define whether it is restored
  after the demonstration.

## Target discovery and perception experiments

Sources: O2; shared by all application activities.

- [ ] Locate folders and application controls from their current accessibility
  information, including when windows open smaller or in different positions.
- [ ] Evaluate Windows accessibility and keyboard navigation for each action.
- [ ] Define the proposed "Python eye" component and identify concrete gaps where
  accessibility does not expose a usable target.
- [ ] Evaluate object detection or other visual targeting for those gaps; decide
  whether a model is needed and whether it runs locally or in a separate research controller.
  This remains an experiment, not an assumed dependency.

## Error recovery and interruption

Sources: O2, N2, N3.

- [ ] Detect when the intended application is no longer active or available.
- [ ] Support Alt+Tab return to the target app, verifying its foreground state
  before resuming.
- [ ] Define which errors permit recovery and which stop playback; log
  the error and current session state for local review or the optional monitor.
- [ ] Define cursor behavior during recovery; the older note is unclear.
- [ ] Keep activities interruptible, including typing, idle/pause intervals,
  app switching, recording transitions, and recovery waits.

## Unconfirmed reference notes

These source details need clarification before they become fixed implementation
rules. They are collected here rather than repeated as separate feature tasks.

| Source | Open decision |
|---|---|
| O2 | Does the desktop grid specify an icon arrangement, activity groups, or both? The nearby block reads approximately "Start 5," "Key points," grid/x-y notation, and "random file..."; its full meaning is unclear. |
| O2 | What should the cursor do while an error is handled? |
| O3, N1 | The newer profile fields partly clarify "user identity based on scoring," but the score formula, inputs, and effect on randomness remain unspecified. |
| N1 | The process notes show main task **70–80%**, side task "rest %," and idle **5%**, with crossing-out. Confirm whether idle is separate and how allocations total 100%. |
| N3 | The whiteboard marks main-first/side-first **80%/20%**. Confirm that these are initial-selection probabilities rather than time allocations, and how they relate to N1. |
| N1 | What does browser "50% randomness" measure: a new-tab search, an off-topic choice, or another event? |
| N1 | Confirm the wording after the 1–5 tab/search note; it appears to concern how tabs are closed. |
| N1, N3 | Confirm recording startup order, what is visible when recording begins, and whether app cleanup happens before or after recording stops. |
| N3 | "Computer name" could mean configuration metadata or a Windows setting change; choose explicitly. |
| N2 | "Pin random items" does not specify the target location or eligible item types. |
| O1 | Confirm whether "dribble" means Dribbble. |
| O1 | Formula writing is crossed out in the notebook. The separate Excel demo already supports formulas; decide whether they belong in the integrated scenario. |

## Source photos

| ID | Reference |
|---|---|
| O1 | [Original: controller and application actions](References/7f32230f-a202-4762-88ad-d05f95acb39f.jpg) |
| O2 | [Original: pauses, movement, desktop folders, perception, and errors](References/990f1660-bc53-450a-9876-52425d5a4bbd.jpg) |
| O3 | [Original: tabs, searches, identity, and scoring](References/e490e1fe-0dd0-4952-8f91-c79f54480d87.jpg) |
| N1 | [New: profile fields, process scheduling, browser configuration, and recording lifecycle](References/80e321e2-8200-4e53-93f1-a24847335d84.jpg) |
| N2 | [New: project structures, application selection, files, folders, and personalization](References/8ccaaab1-2c59-4fe1-91de-40595fae6447.jpg) |
| N3 | [New: whiteboard lifecycle, task categories, browsing, and configuration](References/9fe21a60-86dc-496e-9f7c-22a7fe2a18cb.jpg) |
