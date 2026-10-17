# Changelog

## 2026-10-16

### Fixed
- **F8 / `/pause` no longer stops playback during a held key.** Pausing while a key press, shortcut, or drag was in progress used to log "pause interrupted held input" and stop the run. Because that stop did not count as a user stop, it then triggered an automatic restart. Playback now finishes the release and pauses right after ("pause requested; finishing held input first"). Changes are in `src/playback.c` (`playback_poll`) and the new `pause_deferred` field in `include/playback.h`.

- **Indentation of lines that start with `}`, `]` or `)`.** VS Code re-indents a closing bracket typed as the first thing on a line so that it lines up with the opening bracket it finds. `editor.autoIndent: none` does not turn this off; VS Code checks only whether the language defines brackets. In Plain Text a bracket inside a string such as `'}'` can be taken as the match, which moved `    } else if` in `catalog-store.cjs` to column 0. The saved-text check then stopped the run at character 1452, at the same place every time. After such a bracket, typing now presses Home and Shift+Home and retypes the source indentation, so the line always matches the reference (`src/keyboard.c`, `keyboard_type_code`, plus a `document` argument for `keyboard_type_slice`). New checks are in `tests/keyboard_pace_tests.c`.
- **Save As path is checked before Enter.** Once, the `:` after `C` was lost while the path was typed into Save As. Windows answered "Path does not exist", and the message box and the Save As dialog stayed open. They disabled the VS Code window, so the following restarts all ran out with "expected application/dialog did not reach foreground". Before pressing Enter, `create_reference_file` now reads the File name box back (the `Edit` inside the dialog's `ComboBox`; checked against a real dialog). If the text differs, it types the path once more. If it is still wrong, it closes Save As with Escape before stopping.
- **Telegram retries once.** A request that fails at the network level (error 12002, a timeout) is sent again after 3 seconds. These failures came in bursts while Remote Desktop was reconnecting, and before this change the message was lost.
- **Stop alerts now say why.** When playback stops on its own, `monitor.exe` writes the reason to `logs\monitor.log`, saves a screenshot as `logs\stop-DATE-TIME.jpg`, and sends that picture to Telegram with the reason (the log lines just before the stop) as its caption. If the automatic restart then fails ("restart check failed") or is cancelled, a second alert says so. A run that never starts because its startup check fails now also gets an alert. Before, `tutorial.exe` exited between two checks by the monitor, so nothing was sent. Before, the only alert was "an automatic restart may follow", even when the restart never happened.

### Added
- Regression test in `tests/playback_controls_tests.c`: a pause requested while Enter is held returns without stopping and is recorded as deferred.
- **Class demo** ([CLASS_DEMO.md](CLASS_DEMO.md)): `start-class-demo.cmd`, `scripts/class-demo.json`, two teaching programs, and a GitHub link list of professors' and educators' projects. About 6-9 minutes per run.
- **Telegram guide** ([TELEGRAM.md](TELEGRAM.md)): setup, commands, starting playback with `/run`, and troubleshooting for the existing `monitor.exe`.
- `build/notify.json` configured for the bot (not committed; `build/` is ignored).
- **`switch-to-console.cmd`** (in `worken`) hands the RDP session to the console with `tscon` instead of disconnecting it, so the desktop stays unlocked for RustDesk. It asks for admin rights itself.
- **`tools/set_resolution.c`** builds `set_resolution.exe`, which sets the primary display to a fixed size (default 1920x1080) so the layout matches RDP. `switch-to-console.cmd` runs it right after the switch and logs to `logs\resolution.log`. Use `set_resolution --list` to see the displays and the modes they support. On this VM the console's Basic Display Driver only offers 640x480. When the main display can't show the requested size, the tool moves the desktop onto a display that can and turns the other displays off. If no display can show it, the tool plugs in its own virtual monitor through the Amyuni driver that RustDesk ships (interface `{b5ffd75f-da40-4353-8ff8-b6daf6f1d8ca}`, control code `2307084`, the same request RustDesk sends). That monitor stays when RustDesk disconnects. With `--watch`, the tool re-checks every 5 seconds and re-applies the size whenever it drifts; `switch-to-console.cmd` starts it this way. In an RDP session it changes nothing, because the RDP client sets that size.

### Build
- `build/` rebuilt with the fix. `tutorial.exe` and `monitor.exe` are up to date, there are no compiler warnings, and all 21 CTest tests pass.

### Review notes (not changed)
- Log lines show only the time, not the date, so runs that cross midnight are ambiguous.
- The VS Code path is fixed to the per-user install (`%LOCALAPPDATA%\Programs\Microsoft VS Code`). A system-wide install is not found.
- The list of extended keys is duplicated in `keyboard.c` and `playback.c`.
- `coding_action.c` and `reference_config.c` are large and would be easier to maintain if split by workflow step.
