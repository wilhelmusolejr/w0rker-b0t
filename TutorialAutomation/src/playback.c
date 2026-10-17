#include "playback.h"
#include "window_manager.h"
#include "mouse.h"
#include "keyboard.h"
#include "playback_controls.h"
#include "coding_action.h"
#include "browser_action.h"
#include <stdarg.h>
#include <string.h>

void playback_log(Playback *p, const char *format, ...) {
    SYSTEMTIME now;
    va_list args;
    GetLocalTime(&now);
    printf("[%02u:%02u:%02u] line %u: ", now.wHour, now.wMinute, now.wSecond, p->line);
    va_start(args, format); vprintf(format, args); va_end(args);
    putchar('\n'); fflush(stdout);
    if (p->log) {
        fprintf(p->log, "[%02u:%02u:%02u] line %u: ", now.wHour, now.wMinute, now.wSecond, p->line);
        va_start(args, format); vfprintf(p->log, format, args); va_end(args);
        fputc('\n', p->log); fflush(p->log);
    }
}

int playback_send(Playback *p, INPUT *input, UINT count) {
    UINT sent = SendInput(count, input, sizeof(INPUT));
    if (sent == count) return 1;
    /* Complete release of any partially delivered batch, including Unicode units. */
    { UINT i; for (i = 0; i < sent; ++i) {
        INPUT release = input[i];
        if (release.type == INPUT_KEYBOARD && !(release.ki.dwFlags & KEYEVENTF_KEYUP)) {
            release.ki.dwFlags |= KEYEVENTF_KEYUP; SendInput(1, &release, sizeof(release));
        }
    } }
    playback_log(p, "SendInput delivered %u/%u events (error %lu); check integrity level/UIPI", sent, count, GetLastError());
    InterlockedExchange(&p->stopped, 1);
    return 0;
}

void playback_release(Playback *p) {
    unsigned i;
    static const DWORD mouse_up[] = {MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP};
    for (i = 0; i < 256; ++i) if (p->held[i]) {
        INPUT input = {0};
        input.type = INPUT_KEYBOARD; input.ki.wVk = (WORD)i; input.ki.dwFlags = KEYEVENTF_KEYUP;
        input.ki.wScan = (WORD)MapVirtualKeyW(i, MAPVK_VK_TO_VSC);
        if (i == VK_LWIN || (i >= VK_PRIOR && i <= VK_DOWN) || i == VK_INSERT || i == VK_DELETE)
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        if (SendInput(1, &input, sizeof(input)) != 1) playback_log(p, "key release failed for %u", i);
        p->held[i] = 0;
    }
    for (i = 0; i < 3; ++i) if (p->mouse_held & (1u << i)) {
        INPUT input = {0}; input.type = INPUT_MOUSE; input.mi.dwFlags = mouse_up[i];
        if (SendInput(1, &input, sizeof(input)) != 1) playback_log(p, "mouse release failed for button %u", i);
    }
    p->mouse_held = 0;
}

int playback_guard(Playback *p) {
    if (InterlockedCompareExchange(&p->stopped, 0, 0)) return 0;
    if (!window_desktop_ready()) {
        playback_log(p, "interactive desktop unavailable (locked, disconnected, or inaccessible)");
        InterlockedExchange(&p->stopped, 1); return 0;
    }
    /* No foreground window at all is a transient state (focus hand-off, RDP repaint); allow it to settle. */
    if (p->target && IsWindow(p->target) && !GetForegroundWindow()) {
        ULONGLONG started = GetTickCount64();
        while (!GetForegroundWindow() && GetTickCount64() - started < 5000) {
            if (!playback_poll(p)) return 0;
            Sleep(20);
        }
        if (window_target_foreground(p->target))
            playback_log(p, "foreground was briefly empty; target regained focus after %llu ms",
                GetTickCount64() - started);
    }
    if (!p->target || !IsWindow(p->target) || IsIconic(p->target) || !IsWindowVisible(p->target) ||
        !window_target_foreground(p->target)) {
        playback_log(p, "target is missing, minimized, or no longer foreground (target %p, foreground %p)",
            (void *)p->target, (void *)GetForegroundWindow());
        InterlockedExchange(&p->stopped, 1); return 0;
    }
    return 1;
}

int playback_poll(Playback *p) {
    ULONGLONG pause_started = p->paused ? GetTickCount64() : 0;
    for (;;) {
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            if (message.message == WM_HOTKEY) {
                if (message.wParam == 12 || message.wParam == 9) {
                    playback_log(p, "stop requested"); InterlockedExchange(&p->user_stop, 1);
                    InterlockedExchange(&p->stopped, 1);
                } else if (message.wParam == 8) {
                    p->paused = !p->paused;
                    if (p->paused) {
                        ++p->pause_generation;
                        if (!pause_started) pause_started = GetTickCount64();
                    }
                    playback_log(p, p->paused ? "paused; F8 resumes" : "resumed");
                }
            }
        }
        switch (playback_remote_take()) {
            case PLAYBACK_REMOTE_PAUSE:
                if (!p->paused) {
                    p->paused = 1; ++p->pause_generation;
                    if (!pause_started) pause_started = GetTickCount64();
                    playback_log(p, "paused remotely; /resume or F8 resumes");
                }
                break;
            case PLAYBACK_REMOTE_RESUME:
                if (p->paused) { p->paused = 0; playback_log(p, "resumed remotely"); }
                break;
            case PLAYBACK_REMOTE_STOP:
                playback_log(p, "remote stop requested");
                InterlockedExchange(&p->user_stop, 1); InterlockedExchange(&p->stopped, 1);
                break;
        }
        if (InterlockedCompareExchange(&p->stopped, 0, 0)) {
            playback_log(p, "stop requested; releasing held input");
            playback_release(p); return 0;
        }
        if (!window_desktop_ready()) {
            playback_log(p, "interactive desktop unavailable; stopping");
            InterlockedExchange(&p->stopped, 1); playback_release(p); return 0;
        }
        if (!p->paused) {
            if (pause_started) p->paused_ms += GetTickCount64() - pause_started;
            p->pause_deferred = 0;
            return 1;
        }
        /* Never pause mid key press, chord, or drag: let the caller finish its release first.
           The pause begins at the next poll once nothing is held. */
        { unsigned i; int held = p->mouse_held != 0;
          for (i = 0; i < 256; ++i) held |= p->held[i];
          if (held) {
              if (!p->pause_deferred) playback_log(p, "pause requested; finishing held input first");
              p->pause_deferred = 1;
              return 1;
          }
          if (p->pause_deferred) { p->pause_deferred = 0; pause_started = GetTickCount64(); }
        }
        Sleep(10);
    }
}

int playback_wait(Playback *p, DWORD milliseconds) {
    ULONGLONG started = GetTickCount64(), paused_before = p->paused_ms;
    for (;;) {
        ULONGLONG elapsed, remaining;
        if (!playback_poll(p)) return 0;
        elapsed = GetTickCount64() - started - (p->paused_ms - paused_before);
        if (elapsed >= milliseconds) return 1;
        remaining = milliseconds - elapsed;
        Sleep((DWORD)(remaining > 10 ? 10 : remaining));
    }
}

static int execute(Playback *p, const Command *c) {
    switch (c->kind) {
        case CMD_ACTIVATE: return window_activate(p, c->text);
        case CMD_MOVE: return mouse_move(p, c->n[0], c->n[1], (DWORD)c->n[2], c->n[3]);
        case CMD_CLICK: return mouse_click(p, c->n[0], 0);
        case CMD_DOUBLE: return mouse_click(p, 0, 1);
        case CMD_RIGHT: return mouse_click(p, 1, 0);
        case CMD_DRAG: return mouse_drag(p, c->n[0], c->n[1], c->n[2], c->n[3], (DWORD)c->n[4], c->n[5]);
        case CMD_SCROLL: return mouse_scroll(p, c->n[0]);
        case CMD_TYPE: case CMD_TYPEFILE: return keyboard_type(p, c->text);
        case CMD_KEY: return keyboard_press(p, c->keys[0]);
        case CMD_SHORTCUT: return keyboard_shortcut(p, c->keys, c->key_count);
        case CMD_WAIT: return playback_wait(p, (DWORD)c->n[0]);
        case CMD_PAUSE:
            p->paused = 1; ++p->pause_generation;
            playback_log(p, "script paused; F8 resumes"); return playback_poll(p);
        case CMD_COORDS: p->client_coords = c->n[0]; return 1;
        case CMD_TIMING:
            p->char_ms = (DWORD)c->n[0]; p->line_ms = (DWORD)c->n[1];
            p->typing_pace.enabled = 0;
            return 1;
        case CMD_TYPING_PACE:
            if (c->n[0] < 0) p->typing_pace.enabled = 0;
            else {
                uint32_t seed = c->n[4] ? (uint32_t)c->n[4] : action_recording_seed();
                typing_pace_configure(&p->typing_pace, (unsigned)c->n[0], (unsigned)c->n[1],
                    (unsigned)c->n[2], (unsigned)c->n[3], seed);
                playback_log(p, "presentation typing: %d-%d ms/character, %d-%d ms/line, seed %lu",
                    c->n[0],c->n[1],c->n[2],c->n[3],(unsigned long)seed);
            }
            return 1;
        case CMD_TYPING_BREAKS:
            typing_pace_breaks(&p->typing_pace, (unsigned)c->n[0], (unsigned)c->n[1],
                (unsigned)c->n[2], (unsigned)c->n[3]);
            return 1;
        case CMD_END: return 1;
        case CMD_CODE_REFERENCE: return coding_reference(p, c->path, c->text);
        case CMD_DESKTOP_CODE_REFERENCE: return coding_desktop_reference(p, c->path, c->text);
        case CMD_REFERENCE_BATCH: return coding_reference_batch(p);
        case CMD_ACTION_PACE:
            if (c->n[0] < 0) p->action_pace.enabled = 0;
            else {
                uint32_t seed = c->n[4] ? (uint32_t)c->n[4] : action_recording_seed();
                action_pace_configure(&p->action_pace, (unsigned)c->n[0], (unsigned)c->n[1],
                    (unsigned)c->n[2], (unsigned)c->n[3], seed);
                playback_log(p, "presentation actions: %d-%d ms pauses, %d-%d ms cursor moves, seed %lu",
                    c->n[0], c->n[1], c->n[2], c->n[3], (unsigned long)seed);
            }
            return 1;
        case CMD_BROWSER_SCROLL: {
            HWND browser = NULL;
            return browser_reading_visit(p, c->text, (DWORD)c->n[0], (uint32_t)c->n[1], &browser);
        }
        case CMD_BROWSER_BREAK:
            memset(&p->browser, 0, sizeof(p->browser));
            p->browser.every_lines = (unsigned)c->n[0];
            p->browser.next_line = (unsigned)c->n[0];
            p->browser.url = c->text;
            p->browser.duration_ms = (DWORD)c->n[1];
            p->browser.seed = (uint32_t)c->n[2];
            return 1;
    }
    return 0;
}

int playback_run(Playback *p, const Script *script, DWORD countdown_ms) {
    size_t i; int ok = 1;
    p->client_coords = 1; p->char_ms = 220; p->line_ms = 1500;
    action_pace_configure(&p->action_pace, 1400, 3000, 1000, 1800,
        p->dry_run ? 1 : action_recording_seed());
    typing_pace_configure(&p->typing_pace, 140, 300, 900, 2200,
        p->dry_run ? 1 : action_recording_seed());
    typing_pace_breaks(&p->typing_pace, 160, 320, 6000, 14000);
    if (!p->dry_run) {
        if (!window_desktop_ready()) { playback_log(p, "no usable interactive desktop"); return 0; }
        if (!playback_controls_start(p)) { ok = 0; goto done; }
        { static const int physical[] = {VK_CONTROL,VK_SHIFT,VK_MENU,VK_LWIN,VK_RWIN,VK_LBUTTON,VK_RBUTTON,VK_MBUTTON};
          for (i = 0; i < sizeof(physical)/sizeof(physical[0]); ++i) {
              if (GetAsyncKeyState(physical[i]) & 0x8000) {
                  playback_log(p, "release physical modifiers and mouse buttons before starting"); ok = 0; goto done;
              }
          }
        }
        playback_log(p, "F8 pause/resume | F9 stop | F12 emergency stop | countdown %lu ms", countdown_ms);
        {
            DWORD remaining = countdown_ms;
            while (remaining) {
                DWORD step = remaining > 1000 ? 1000 : remaining;
                playback_log(p, "starting in %lu second(s)", (remaining + 999) / 1000);
                if (!playback_wait(p, step)) { ok = 0; goto done; }
                remaining -= step;
            }
        }
    }
    for (i = 0; i < script->count; ++i) {
        const Command *c = &script->commands[i];
        p->line = c->line;
        playback_log(p, "%s%s", p->dry_run ? "DRY-RUN " : "", command_name(c->kind));
        if (p->dry_run) {
            if (c->kind == CMD_BROWSER_SCROLL)
                playback_log(p, "  browser scrolling: %d ms, seed %d", c->n[0], c->n[1]);
            else if (c->kind == CMD_BROWSER_BREAK)
                playback_log(p, "  browser visits: every %d saved lines, %d ms, seed %d (0 lines disables visits)",
                    c->n[0], c->n[1], c->n[2]);
            else if (c->text) playback_log(p, "  %zu UTF-16 units of preloaded text", wcslen(c->text));
            else if (c->kind == CMD_KEY || c->kind == CMD_SHORTCUT)
                playback_log(p, "  %u key(s), final virtual key 0x%02x", c->key_count, c->keys[c->key_count-1]);
            else playback_log(p, "  arguments: %d %d %d %d %d %d", c->n[0],c->n[1],c->n[2],c->n[3],c->n[4],c->n[5]);
            continue;
        }
        if (!playback_poll(p) || !execute(p, c)) { ok = 0; break; }
    }
done:
    if (!p->dry_run) {
        playback_release(p);
        playback_controls_stop();
    }
    playback_log(p, ok ? "completed" : "stopped before completion");
    return ok;
}
