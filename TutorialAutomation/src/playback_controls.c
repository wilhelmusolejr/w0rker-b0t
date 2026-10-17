#include "playback_controls.h"

static Playback *active_playback;
static HHOOK emergency_hook;
static int pause_registered, stop_registered;

/* Inspect only F12. Other keyboard events pass through without being recorded. */
LRESULT CALLBACK playback_emergency_hook(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && active_playback) {
        const KBDLLHOOKSTRUCT *key = (const KBDLLHOOKSTRUCT *)data;
        if (key->vkCode == VK_F12) {
            if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
                InterlockedExchange(&active_playback->user_stop, 1);
                InterlockedExchange(&active_playback->stopped, 1);
            }
            /* Keep the emergency key from triggering a command in the target. */
            return 1;
        }
    }
    return CallNextHookEx(emergency_hook, code, message, data);
}

void playback_controls_stop(void) {
    if (emergency_hook) UnhookWindowsHookEx(emergency_hook);
    emergency_hook = NULL;
    active_playback = NULL;
    if (pause_registered) UnregisterHotKey(NULL, 8);
    if (stop_registered) UnregisterHotKey(NULL, 9);
    pause_registered = stop_registered = 0;
}

int playback_controls_start(Playback *p) {
    if (active_playback) {
        playback_log(p, "playback controls already active"); return 0;
    }
    active_playback = p;
    pause_registered = RegisterHotKey(NULL, 8, MOD_NOREPEAT, VK_F8);
    if (!pause_registered) {
        playback_log(p, "cannot reserve F8 (error %lu); playback refused", GetLastError());
        goto failed;
    }
    stop_registered = RegisterHotKey(NULL, 9, MOD_NOREPEAT, VK_F9);
    if (!stop_registered) {
        playback_log(p, "cannot reserve F9 (error %lu); playback refused", GetLastError());
        goto failed;
    }
    /* F12 is debugger-reserved and must never be passed to RegisterHotKey. */
    emergency_hook = SetWindowsHookExW(WH_KEYBOARD_LL, playback_emergency_hook,
                                     GetModuleHandleW(NULL), 0);
    if (!emergency_hook) {
        playback_log(p, "cannot install F12 emergency control (error %lu); playback refused", GetLastError());
        goto failed;
    }
    return 1;
failed:
    playback_controls_stop();
    return 0;
}

static HANDLE remote_events[3];
int playback_remote_open(void) {
    static const wchar_t *const names[3] = {PLAYBACK_REMOTE_PAUSE_EVENT, PLAYBACK_REMOTE_RESUME_EVENT,
                                            PLAYBACK_REMOTE_STOP_EVENT};
    unsigned i;
    for (i = 0; i < 3; ++i) {
        if (!remote_events[i]) remote_events[i] = CreateEventW(NULL, FALSE, FALSE, names[i]);
        /* Clear a command left over from before this run started. */
        if (remote_events[i]) ResetEvent(remote_events[i]);
    }
    return remote_events[0] && remote_events[1] && remote_events[2];
}
void playback_remote_close(void) {
    unsigned i;
    for (i = 0; i < 3; ++i) if (remote_events[i]) { CloseHandle(remote_events[i]); remote_events[i] = NULL; }
}
int playback_remote_take(void) {
    unsigned i;
    /* Stop is checked first so it wins over a simultaneous pause or resume. */
    static const unsigned order[3] = {2, 0, 1};
    for (i = 0; i < 3; ++i) {
        HANDLE event = remote_events[order[i]];
        if (event && WaitForSingleObject(event, 0) == WAIT_OBJECT_0) return (int)order[i] + 1;
    }
    return PLAYBACK_REMOTE_NONE;
}
