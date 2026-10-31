# Telegram remote control

`monitor.exe` runs next to `tutorial.exe` and connects playback to a configured Telegram chat. Use it to pause, resume, stop, start, and check playback without touching the playback machine. This is useful when the machine is only being watched through view-only RustDesk.

The monitor never sends desktop input itself. It reads the playback log, takes screenshots on request, and passes `/pause`, `/resume`, and `/stop` to playback through named Windows events.

## One-time setup

1. **Create the Telegram connection.** In Telegram, open **@BotFather** (blue checkmark), send `/newbot`, choose a name and a username ending in `bot`, and copy the token it gives you. These are Telegram's required setup names.
2. **Create `build\notify.json`** next to `monitor.exe`:
   ```json
   {
       "telegram_bot_token": "PASTE_TOKEN_HERE",
       "telegram_chat_id": "",
       "heartbeat_minutes": 0,
       "stall_minutes": 10,
       "log_folder": "../../logs",
       "launcher": "../../start-reference-coding.cmd"
   }
   ```
3. **Find your chat id.** Run `monitor.exe` from the `build` folder. With an empty `telegram_chat_id` it starts in setup mode. Open the new Telegram chat, tap **Start**, and send any message; the monitor replies with your chat id.
4. **Save the chat id.** Stop the monitor with Ctrl+C, put the number in `"telegram_chat_id": "123456789"`, and run `monitor.exe` again. Telegram shows **"Monitor started"**.

| Setting | Meaning |
|---|---|
| `telegram_bot_token` | Token from @BotFather. Keep it secret: anyone with it can access the chat's messages and screenshots. If it leaks, send `/revoke` to @BotFather and paste the new token. |
| `telegram_chat_id` | The only chat allowed to control the monitor. Messages from other chats are ignored and logged. |
| `heartbeat_minutes` | Periodic status message; `0` = only on request (0-1440). |
| `stall_minutes` | Alert when the playback log has not changed for this long (2-1440). |
| `log_folder` | Where playback logs are read and `monitor.log` is written, relative to `notify.json`. |
| `launcher` | The `.cmd` file that `/start` runs, relative to `notify.json`. It must exist. `../../start-session.cmd` runs the optional long-duration session; `../../start-class-demo.cmd` runs the class demo. |

`build/` is listed in `.gitignore`, so `notify.json` and its token are never committed.

## Commands

| Command | What it does |
|---|---|
| `/status` | Recorder state, session clock mode, current task, file progress, and recent browser visit |
| `/screenshot` | JPEG of the whole desktop with a status caption |
| `/log` | Status plus the last 25 playback log lines |
| `/pause` | Same as F8: pauses at the next safe point (after any held key is released) |
| `/resume` | Continues a paused playback |
| `/stop` | Same as F9: releases input and stops; no automatic restart |
| `/start` | Starts the configured `launcher` from `notify.json`. If it points to `start-session.cmd`, that session uses either `--duration-minutes N` or the daily L3 target and continues if the recorder fails. `/run` still works the same. |

Any other message returns the command list.

## Starting playback from Telegram

1. Make sure `monitor.exe` is running on the playback machine. It must be started **before** playback, or `/pause` and `/resume` cannot reach that run.
2. Leave the desktop unlocked and untouched, with VS Code and Chrome not in the middle of a dialog.
3. Send **`/start`**. The monitor replies "Started", and playback begins after the launcher's 10-second countdown.
4. After about a minute, send `/screenshot` or `/status` to confirm it is typing.

`/start` refuses to start when playback is already running or when the desktop is locked or disconnected. It starts the launcher in its own console window, exactly like double-clicking the `.cmd`.

To switch what `/start` runs (long reference job vs. class demo), change `launcher` in `notify.json` and restart `monitor.exe`.

### Starting the monitor automatically at sign-in (optional)

Press Win+R, type `shell:startup`, and create a shortcut to `build\monitor.exe` with **Start in** set to the `build` folder. The monitor then starts whenever you sign in, and `/start` is available without opening a terminal.

## Alerts it sends without being asked

- **Playback STOPPED**: a screenshot plus the last log lines, sent once per stop.
- **Stall**: no log activity for `stall_minutes`.
- **Heartbeat**: only if `heartbeat_minutes` is above 0.

## Troubleshooting

| Message | Fix |
|---|---|
| `telegram_bot_token is missing` | The token was not pasted correctly; it must contain a `:`. |
| `Telegram ... request failed (error 12002)` | Network timeout. The monitor keeps retrying. If it repeats, the network may be blocking `api.telegram.org`; test with a phone hotspot. |
| `launcher file was not found` | `launcher` does not point at an existing `.cmd`. |
| `The monitor is already running.` | Only one monitor runs per sign-in session; use the existing one. |
| "Playback is running but did not accept remote commands" | Playback was started before the monitor or with an old build. Stop it with F9 and start again. |

Check screen capture locally without sending anything: `monitor.exe --screenshot test.jpg`. The monitor's own log is `logs\monitor.log`.

## Optional one-time setup on the playback machine: `setup-monitor.cmd`

Run `setup-monitor.cmd` from the repository root once, and accept the administrator prompt. It sets up:

- **A scheduled task** `\TutorialAutomation\SwitchToConsole` that runs `switch-to-console.cmd --quiet`
  with administrator rights, so nothing has to be clicked later.
- **A Startup shortcut** to `start-monitor.cmd`, so the monitor runs whenever you sign in. Only one
  monitor runs at a time.

With that in place, `/start` first checks the session:

- **Through Remote Desktop, or disconnected or locked:** it runs the task. The session moves to the
  console and stays unlocked, and the resolution watcher keeps it at 1920x1080. An open RDP window
  closes; reconnect with RDP or RustDesk later. The monitor reports this in Telegram.
- **Already on the console:** nothing changes.

Then it starts the launcher as before. Without the setup, `/start` still works on an unlocked
desktop, and says to run `setup-monitor.cmd` when it would have needed the switch.
