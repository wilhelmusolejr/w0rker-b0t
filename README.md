# w0rker-b0t: JSON desktop workflows

This repository contains a native C11/Win32 desktop automation player and smaller UI experiments. The current player runs an ordered JSON flow: each step names a supported action and, for timed tasks, a work duration. It uses the existing coding, browsing, Notepad, Calculator, and optional Loggy3 actions.

The [10h30m example](TutorialAutomation/scripts/workday-flow.json) contains 22 explicit steps. Its planned time is 427 minutes coding, 120 browsing, 20 in other apps, 58 idle, and 5 estimated transition minutes. The transition estimate is metadata; actual elapsed time depends on launches, pages, saves, and safe points. The runner ends after the final step. Loggy3 runs only if the JSON lists a `loggy3.*` action.

## Start with 10 minutes of browsing

The [10 minute browsing flow](TutorialAutomation/scripts/browse-10m-flow.json) uses profile 1 for one task: open or focus the browser, browse the profile catalog for **10 active minutes**, then leave the browser open. It has no planned idle minutes, checkpoint, or Loggy3 action, so repeating it starts a new run. From `TutorialAutomation/build`:

```powershell
.\tutorial.exe --validate-flow ..\scripts\browse-10m-flow.json
.\tutorial.exe --dry-run-flow ..\scripts\browse-10m-flow.json
.\tutorial.exe --flow ..\scripts\browse-10m-flow.json
```

The live command starts a 10-second countdown, then needs an unlocked interactive Windows desktop. Browser startup and page loading can make elapsed time longer than 10 minutes. See [build instructions](TutorialAutomation/README.md#build) if `tutorial.exe` has not been built.

Validation checks the schema and time totals. Dry run also checks the selected profile and shows each task's before/after actions without desktop input. The [native player guide](TutorialAutomation/README.md#json-work-flows) explains how to edit flows, choose a profile, use checkpoints, and add app or Loggy3 actions.

For the longer example, use the [10h30m workday flow](TutorialAutomation/scripts/workday-flow.json). Its coding steps need reference projects; `tutorial.exe --fetch-projects` prepares the pinned references. It enables step checkpoints under `TutorialAutomation/profiles/progress`, so repeating `--flow` resumes at the next step. Use `--restart-flow` to start the step list again.

## Why this design

The older combined session chooses coding, browsing, breaks, and recording from built-in scheduling rules. A JSON flow makes a particular sequence reviewable before execution and gives each app an explicit place in the day. `expected_total_minutes` checks the arithmetic; it is not a wall-clock stop. A task can finish late if it is saving at a safe point or waiting for an app.

An AI assistant can draft this JSON from a request, but the player executes only the supported actions. It rejects unknown actions and malformed fields before live playback. AI generation itself is not part of the native player. The [70R34-BASEWOOK-AI workflow example](https://github.com/wilhelmusolejr/70R34-BASEWOOK-AI#task-workflow) motivated the ordered step structure.

## Other parts of the repository

| Area | Location |
|---|---|
| Native player, profile modes, build instructions | [TutorialAutomation](TutorialAutomation/README.md) |
| Workflow JSON examples | [10 minute browsing](TutorialAutomation/scripts/browse-10m-flow.json), [10h30m workday](TutorialAutomation/scripts/workday-flow.json) |
| Class and Excel demos | [Class demo](TutorialAutomation/CLASS_DEMO.md), [Excel demo](TutorialAutomation/EXCEL_DEMO.md) |
| Python UI experiments | [experiments](experiments) |
| Remote control | [Telegram monitor](TutorialAutomation/TELEGRAM.md) |

The older `--session` command remains available and uses its own duration or daily recording target. `--flow` follows the JSON step list. The full workday flow has passed validation and dry run, but has not been run end to end on a live desktop.
