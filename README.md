# Windows desktop automation

This project builds scripted Windows desktop demonstrations and tests application
interaction through visible typing, mouse movement, scrolling, and window
switching. It includes reference-code playback in VS Code, browser navigation,
Excel examples, and accessibility-based application controls.

The main player is written in C11 using Win32 APIs. Standalone Python experiments
test interaction concepts before integration into the C player. Timing ranges
and random seeds configure playback pacing and navigation choices. Source-code
playback enters existing reference text through the editor and verifies saved
contents; it does not generate new code.

## Roles: manager and bots

| Role | What it is |
|---|---|
| **Bot** | A machine or RDP session where the automation runs (`tutorial.exe`, Loggy3, one profile); about 20 of them. It runs the work session and reports status, alerts, and screenshots. |
| **Manager** | One separate place that keeps the list of bots, asks each for a screenshot or status, and sends start, pause, resume, and stop. Not built yet: today each bot reports on its own through its Telegram monitor. |

See [task.md](task.md) ("Roles: manager and bots") and Phase 8 in [TRACKER.md](TRACKER.md).

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
| Integration backlog | [Work plan](task.md) | Proposed work and unresolved reference notes, separate from implemented features. |

## Running a demonstration

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
