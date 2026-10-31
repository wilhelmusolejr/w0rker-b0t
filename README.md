# Accessibility automation and AI helper

This personal learning project explores how an AI helper can use Windows
accessibility information to operate visible desktop applications. It includes
guarded typing, mouse movement, scrolling, window switching, browser navigation,
reference-code playback in VS Code, and Excel demonstrations. Its current actions
are scripted and profile driven; AI-assisted choices are an area for experiments,
not a claim that the C player independently reasons about the desktop.

The main player is written in C11 using Win32 APIs. Standalone Python experiments
test interaction concepts before integration into the C player. Timing ranges
and random seeds configure playback pacing and navigation choices. Source-code
playback enters existing reference text through the editor and verifies saved
contents; it does not generate new code.

## Active scope

The active workflows run in one interactive, unlocked Windows desktop session at
a time. `tutorial.exe` performs local actions; optional `monitor.exe` reports
status and accepts commands from a configured Telegram chat. A combined session
can use a user-selected duration or a daily Loggy3 recording target. The desktop
work continues if Loggy3 fails. See the [work plan](task.md) and
[tracker](TRACKER.md) for current and planned capabilities.

**Setting up optional remote control:** build `TutorialAutomation`, put the Telegram token in `build\notify.json`, and run
`setup-monitor.cmd` once (administrator prompt). Then Telegram `/start` moves the session to the console if
needed and runs the configured launcher (for example `start-session.cmd`); `/stop` ends it. See
[TELEGRAM.md](TutorialAutomation/TELEGRAM.md).

## Repository structure

| Path | Purpose |
|---|---|
| `TutorialAutomation/src`, `include`, `profiles`, `tests` | Native C11 player, configuration, and input-free checks. |
| `TutorialAutomation/scripts` | Configured demonstrations and reference playback assets. |
| `experiments/` | Smaller Python experiments for accessibility targeting and browser behavior. |
| `References/` | Teaching inputs and sample data; outputs are saved separately. |
| `programming_files/` | Ignored local clones of pinned reference projects; its README keeps the directory in fresh checkouts. |
| `start-*.cmd` | Local launchers, including `start-session.cmd` for the combined workflow. |
| `task.md`, `TRACKER.md` | Planned work and implementation status. |

The native player queries accessible names, states, and screen rectangles before
acting. Input guards require an active desktop and the intended foreground
window. A profile supplies pacing and task settings; logs and saved-file checks
provide evidence of what happened.

## Components and documentation

| Component | Documentation | Status and behavior |
|---|---|---|
| Native desktop player | [TutorialAutomation](TutorialAutomation/README.md) | C player for typing, mouse input, application switching, checkpoints, and browser visits. |
| Classroom example | [Class demo](TutorialAutomation/CLASS_DEMO.md) | Short configured coding and browsing demonstration. |
| Excel playback | [Excel demo](TutorialAutomation/EXCEL_DEMO.md) | Enters fictional reference data and formulas into separate output workbooks and verifies saved results. |
| Taskbar targeting | [Taskbar experiment](experiments/taskbar-pointer-demo/README.md) | Python prototype for locating accessible app buttons, hovering/clicking, and showing the desktop. |
| Browser navigation | [Browser experiment](experiments/browser-explorer/README.md) | Python prototype for discovering accessible links and controls, varying scroll/input pacing, and observing click outcomes. |
| Side-by-side demo | [Split coding/browser experiment](experiments/split-coding-browser/README.md) | One minute of sample typing, one minute of browsing, then a return to VS Code. Opens the desktop project folder while saving the sample separately. |
| Loggy3 controls | [Loggy3 experiment](experiments/loggy3-window/README.md) | Window activation/minimization, recording-state inspection, and explicit Start/Stop commands. Recording commands operate the real application. |
| Remote playback control | [Telegram monitor](TutorialAutomation/TELEGRAM.md) | Starts, pauses, resumes, and stops playback; reports status and screenshots. |
| Combined session | [Session guide](TutorialAutomation/README.md#combined-work-session) | Coding and browsing with a duration override or daily target; Loggy3 recording is optional when its state is unavailable. |
| Integration backlog | [Work plan](task.md) | Proposed work and unresolved reference notes, separate from implemented features. |

## Running a demonstration

To preview a 20-minute combined session without desktop input, run
`TutorialAutomation\build\tutorial.exe --dry-run-session 1 --duration-minutes 20`.
To run it on an unlocked desktop, use `start-session.cmd 1 --duration-minutes 20`.
Without the duration option, the default is a random 10h15m–10h45m **daily
Loggy3 total**; if Loggy3 fails, the session continues for the remaining time
on a local clock. See the [session guide](TutorialAutomation/README.md#combined-work-session)
for timing and fallback details.

For the separate side-by-side example, use
[start-split-demo.cmd](experiments/split-coding-browser/start-split-demo.cmd).
VS Code and Chrome must be installed. Startup and phase switching add time to the
two active minutes.

For the native reference-code player, build the executable using the
[build instructions](TutorialAutomation/README.md#build), then run
[start-reference-coding.cmd](start-reference-coding.cmd). It uses
[scripts/job.json](TutorialAutomation/scripts/job.json), the version 2 job format.
The older version 1 reference-snapshot configuration remains available separately.

Desktop playback needs an active, unlocked interactive Windows session and the
intended application in focus. The native player supports **F8** to pause/resume
and **F9/F12** to stop. Prototype stop keys and focus requirements are documented
in their individual READMEs. Logs, saved-file checks, and test results describe
what automation ran and what was verified.
