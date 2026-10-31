# w0rker-b0t: configuration-driven desktop workflows

This repository contains a Windows desktop automation player and smaller UI
experiments. The project is being redesigned around **an ordered JSON workflow**:
the configuration says which application to open, what task to run there, how long
that task may run, what result to check, and what action follows. The redesigned
player will execute the validated steps in order.

This README describes the **proposed workflow format**. The current C player does
not yet accept the example below. For commands that work today, see the
[native player guide](TutorialAutomation/README.md).

## Why change the design?

Today, configuration is spread across profiles, session settings, and individual
demo jobs. The combined session chooses activities from built-in scheduling rules.
That makes a specific sequence of apps and tasks hard to express or review before
it runs.

The new unit of work will be a **plan** with explicit steps. A plan can be written
by a person or drafted by an AI assistant, then checked by the same validator.
An assistant should generate JSON only from a known list of actions and tasks;
it should not invent commands for the player to execute. The workflow example in
[70R34-BASEWOOK-AI](https://github.com/wilhelmusolejr/70R34-BASEWOOK-AI#task-workflow)
is a useful reference for a task object with ordered steps and action parameters.
This project will apply that structure to its own local desktop activities.

## Proposed plan format

```json
{
  "version": 1,
  "name": "coding-and-docs-demo",
  "profile_id": 1,
  "steps": [
    {
      "id": "open-editor",
      "action": "app.open",
      "app": "vscode",
      "params": { "workspace": "demo-project" }
    },
    {
      "id": "code",
      "action": "task.run",
      "app": "vscode",
      "task": "reference_coding",
      "max_minutes": 45,
      "params": {
        "source": "References/scripts/vehicle-lookup.py",
        "destination": "demo-project/vehicle-lookup.py"
      },
      "check": "saved_file_matches_source"
    },
    {
      "id": "open-browser",
      "action": "app.open",
      "app": "chrome"
    },
    {
      "id": "visit-docs",
      "action": "task.run",
      "app": "chrome",
      "task": "visit_urls",
      "max_minutes": 15,
      "params": { "urls": ["https://docs.python.org/3/tutorial/"] }
    },
    { "id": "close-browser", "action": "app.close", "app": "chrome" },
    { "id": "close-editor", "action": "app.close", "app": "vscode" }
  ]
}
```

This is an **illustrative design**, not a file understood by the current player.
The final action names, fields, and task adapters will be settled with the schema
and validator. A browser tab can be a separate context within the browser, with
its own `tab.open`, task, and `tab.close` steps. Each application gets explicit
open and close behavior; tasks operate only while their required app is ready.

The array order answers “what happens after this?” by default. Later versions can
add branches or repetition if real workflows need them. A task's `max_minutes`
is a time budget, not proof that useful work happened for that long. A completion
check may end it sooner. A ten-hour request can produce ten hours of planned
task capacity, but actual actions, results, and elapsed time must be reported
separately from that estimate.

## Action and task model

| Part | Proposed responsibility |
|---|---|
| `app.open` | Find or launch the named app and verify its window is ready. |
| `task.run` | Run one app-specific activity using validated parameters. |
| `tab.open` / `tab.close` | Create or close a browser tab when the plan calls for it. |
| `app.close` | Save or check pending output as required, then close the plan-owned window. |
| `check` | Confirm an observable result, such as a saved file matching its source. |

Tasks will be registered by app. The first adapters can wrap existing reference
coding, browser visits, and Excel demonstration code. A coding task may continue
across multiple steps or be followed by a browser task; the plan makes that
sequence visible. L3 can be an optional application adapter with explicit
recording controls. Its state is separate from the plan's time budget.

The executor should reject unknown actions, missing parameters, unsupported
app/task pairs, invalid paths, and steps that use an app before it opens. It
should preview the plan without desktop input, then run it with foreground and
desktop checks, pause/stop controls, saved-output verification, and a step log.

## AI-assisted planning

An AI assistant can draft a JSON plan from a request such as “work on this
reference coding task, visit these documentation pages, then close both apps.”
The proposed flow is:

1. Generate a plan using the published action and task schema.
2. Validate its structure, app sequence, input paths, and time budgets.
3. Show the ordered steps and estimated total in a dry run for review.
4. Execute the selected plan and record what actually completed.

The AI planner and the universal action-flow executor are **planned work**.
The current player follows configured scripts and profiles; it does not
independently generate a full workday or implement this proposed schema.

## What exists now

| Area | Current state |
|---|---|
| Native player | C11/Win32 player for guarded typing, mouse input, app switching, browser visits, and saved-file checks. |
| Existing JSON | [Reference job](TutorialAutomation/scripts/job.json), [profiles](TutorialAutomation/profiles/profiles.json), and [session settings](TutorialAutomation/profiles/session.json) configure existing modes; none is the proposed step list. |
| Python experiments | [Taskbar targeting](experiments/taskbar-pointer-demo/README.md), [browser exploration](experiments/browser-explorer/README.md), [split coding and browsing](experiments/split-coding-browser/README.md), and [L3 controls](experiments/loggy3-window/README.md). |
| Other examples | [Class demo](TutorialAutomation/CLASS_DEMO.md) and [Excel demo](TutorialAutomation/EXCEL_DEMO.md). |
| Remote control | [Telegram monitor](TutorialAutomation/TELEGRAM.md) can control and report on an existing playback session. |

The current [work plan](task.md) and [tracker](TRACKER.md) describe the existing
profile-driven direction and implementation status. Their scheduling tasks will
need revisiting when the action-flow design is implemented. L3 is the short name
used in these docs; existing source paths and command-line flags retain their
current spelling until the code is migrated.

## Next implementation steps

1. Define the plan schema and a validator, including app readiness, task inputs,
   time budgets, completion checks, and stop behavior.
2. Add a dry run that prints the exact ordered flow and estimated duration.
3. Wrap existing C activities behind app and task handlers.
4. Add the step executor, resume checkpoints, and actual-result logging.
5. Add AI plan drafting only after validation and preview are reliable.

Live playback requires an unlocked interactive Windows session. The current
player's working commands, build instructions, and tested limits remain in the
[native player guide](TutorialAutomation/README.md) and
[test notes](TutorialAutomation/tests/TESTING.md).
