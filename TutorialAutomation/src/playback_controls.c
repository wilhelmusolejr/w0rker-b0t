#include "playback_controls.h"
#include <limits.h>
#include <stdlib.h>

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

/* ---------- the person at the computer ---------- */

static volatile LONG64 user_tick;            /* GetTickCount64 of their last input; 0: none yet */
static volatile LONG cursor_x = -100000, cursor_y = -100000;
static HANDLE activity_thread;
static DWORD activity_thread_id;
void playback_note_cursor(int x, int y) { InterlockedExchange(&cursor_x, x); InterlockedExchange(&cursor_y, y); }
unsigned long playback_user_idle_ms(void) {
    LONG64 last = InterlockedCompareExchange64(&user_tick, 0, 0);
    ULONGLONG now = GetTickCount64();
    return !last ? ULONG_MAX : now > (ULONGLONG)last ? (unsigned long)(now - (ULONGLONG)last) : 0;
}
static LRESULT CALLBACK activity_keyboard(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && ((const KBDLLHOOKSTRUCT *)data)->dwExtraInfo != PLAYBACK_INPUT_TAG)
        InterlockedExchange64(&user_tick, (LONG64)GetTickCount64());
    return CallNextHookEx(NULL, code, message, data);
}
static LRESULT CALLBACK activity_mouse(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION) {
        const MSLLHOOKSTRUCT *m = (const MSLLHOOKSTRUCT *)data;
        int ours = m->dwExtraInfo == PLAYBACK_INPUT_TAG;
        /* Playback moves the pointer with SetCursorPos; a move to the point it just set is its own. */
        if (!ours && message == WM_MOUSEMOVE)
            ours = labs(m->pt.x - InterlockedCompareExchange(&cursor_x, 0, 0)) <= 1 &&
                   labs(m->pt.y - InterlockedCompareExchange(&cursor_y, 0, 0)) <= 1;
        if (!ours) InterlockedExchange64(&user_tick, (LONG64)GetTickCount64());
    }
    return CallNextHookEx(NULL, code, message, data);
}
/* A thread of its own, so the person's mouse never waits on playback being busy. */
static DWORD WINAPI activity_listener(LPVOID unused) {
    HHOOK keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, activity_keyboard, GetModuleHandleW(NULL), 0);
    HHOOK mouse = SetWindowsHookExW(WH_MOUSE_LL, activity_mouse, GetModuleHandleW(NULL), 0);
    MSG message;
    (void)unused;
    while (GetMessageW(&message, NULL, 0, 0) > 0) DispatchMessageW(&message);
    if (keyboard) UnhookWindowsHookEx(keyboard);
    if (mouse) UnhookWindowsHookEx(mouse);
    return 0;
}
static void activity_stop(void) {
    if (!activity_thread) return;
    PostThreadMessageW(activity_thread_id, WM_QUIT, 0, 0);
    WaitForSingleObject(activity_thread, 2000);
    CloseHandle(activity_thread);
    activity_thread = NULL;
}

void playback_controls_stop(void) {
    activity_stop();
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
    InterlockedExchange64(&user_tick, 0);
    activity_thread = CreateThread(NULL, 0, activity_listener, NULL, 0, &activity_thread_id);
    if (!activity_thread) playback_log(p, "cannot watch for the person's own input (error %lu); no pause when they use the computer", GetLastError());
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
