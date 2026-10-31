# Project context

This is a personal learning project for an accessibility automation and AI helper
on Windows. The active implementation drives one interactive desktop session at
a time. It contains a C11/Win32 player, Python targeting experiments, teaching
examples, and input-free tests. The player currently follows configured actions;
AI-assisted decisions are experimental rather than a shipped autonomous feature.

Read [README.md](README.md) for the repository map, [task.md](task.md) for the
current work plan, and [TRACKER.md](TRACKER.md) for task status. Native code is
under `TutorialAutomation/src` and `include`; settings are in `profiles`, with
input-free checks under `tests`. Small experiments are under `experiments`.

The root README proposes a new ordered JSON action flow. The existing task and
tracker files describe the current player and earlier profile-driven backlog;
do not treat their scheduler tasks as the final design for the rewrite.

The optional Telegram monitor controls the configured playback session. A
combined session with `--duration-minutes N` uses local active time and continues
if L3 is unavailable. Without that option, the daily L3 target is
10h15m–10h45m by default; an unreadable recorder switches the remaining budget
to a local countdown. Keep the live-validation limits stated in the feature
guides accurate when changing documentation.

For code changes, keep desktop and foreground guards, stop controls, bounded
targets, and saved-output checks intact. Use dry runs and input-free tests before
considering an interactive desktop check. Do not start a live playback or
recording session as part of an ordinary code or documentation review.
