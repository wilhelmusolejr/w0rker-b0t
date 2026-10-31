# Project context

This is a personal learning project for accessibility-based Windows UI automation
and AI-assisted interaction experiments. The active implementation drives one
interactive desktop session at a time. It contains a C11/Win32 player, Python
prototypes, disposable teaching examples, and tests.

Read [README.md](README.md) for the component map, [task.md](task.md) for the
current work plan, and [TRACKER.md](TRACKER.md) for task status. Earlier notebook
photos and Git history include a retired multi-instance manager proposal; it is
outside the active scope.

The optional Telegram monitor controls the configured playback session. The
Loggy3 integration and its long-duration settings are documented as implemented
experiments, with live-validation limits stated in the feature guides. Preserve
those facts when changing documentation.

For code changes, keep desktop and foreground guards, stop controls, bounded
targets, and saved-output checks intact. Use dry runs and input-free tests before
considering an interactive desktop check. Do not start a live playback or
recording session as part of an ordinary code or documentation review.
