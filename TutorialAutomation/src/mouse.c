#include "mouse.h"
#include "window_manager.h"
#include "playback_controls.h"

double mouse_ease(double t) { return t * t * (3.0 - 2.0 * t); }

int mouse_move(Playback *p, int x, int y, DWORD duration, int curve) {
    if (p->action_pace.enabled) {
        DWORD paced = action_move_next(&p->action_pace);
        if (duration < paced) duration = paced;
    }
    return mouse_move_arc(p, x, y, duration, curve ? 0.15 : 0);
}

int mouse_move_arc(Playback *p, int x, int y, DWORD duration, double curvature) {
    POINT start, destination, point;
    ULONGLONG elapsed, started = GetTickCount64(), paused_before = p->paused_ms;
    if (!window_point(p, x, y, &destination)) return 0;
    if (!GetCursorPos(&start)) {
        playback_log(p, "GetCursorPos failed (error %lu)", GetLastError()); return 0;
    }
    for (;;) {
        double t, bend;
        DWORD step;
        if (!playback_poll(p) || !window_point(p, x, y, &destination)) return 0;
        elapsed = GetTickCount64() - started - (p->paused_ms - paused_before);
        if (elapsed > duration) elapsed = duration;
        t = duration ? mouse_ease((double)elapsed / duration) : 1.0;
        bend = curvature * 4.0 * t * (1.0-t);
        point.x = (LONG)(start.x + (destination.x-start.x)*t - (destination.y-start.y)*bend);
        point.y = (LONG)(start.y + (destination.y-start.y)*t + (destination.x-start.x)*bend);
        if (!window_point_visible(point)) {
            playback_log(p, "animated cursor path leaves connected monitors"); return 0;
        }
        playback_note_cursor(point.x, point.y);
        if (!SetCursorPos(point.x, point.y)) {
            playback_log(p, "SetCursorPos failed (error %lu)", GetLastError()); return 0;
        }
        if (elapsed >= duration) return 1;
        step = (DWORD)(duration - elapsed < 10 ? duration - elapsed : 10);
        if (!playback_wait(p, step)) return 0;
    }
}

static int mouse_pointer_ready(Playback *p) {
    POINT point;
    HWND hit;
    if (!playback_guard(p)) return 0;
    if (!GetCursorPos(&point) || !window_point_visible(point)) {
        playback_log(p, "cursor position unavailable or outside connected monitors"); return 0;
    }
    hit = WindowFromPoint(point);
    if (!hit || GetAncestor(hit, GA_ROOT) != p->target) {
        playback_log(p, "cursor is over another window; refusing button or wheel input"); return 0;
    }
    if (p->client_coords) {
        RECT rect;
        POINT client = point;
        if (!ScreenToClient(p->target, &client) || !GetClientRect(p->target, &rect) ||
            !PtInRect(&rect, client)) {
            playback_log(p, "cursor outside target client area"); return 0;
        }
    }
    return 1;
}

int mouse_button(Playback *p, int button, int down) {
    static const DWORD flags[3][2] = {
        {MOUSEEVENTF_LEFTUP, MOUSEEVENTF_LEFTDOWN},
        {MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_RIGHTDOWN},
        {MOUSEEVENTF_MIDDLEUP, MOUSEEVENTF_MIDDLEDOWN}};
    INPUT input = {0};
    if (!mouse_pointer_ready(p)) return 0;
    input.type = INPUT_MOUSE; input.mi.dwFlags = flags[button][down != 0];
    if (!playback_send(p, &input, 1)) return 0;
    if (down) p->mouse_held |= 1u << button;
    else p->mouse_held &= ~(1u << button);
    return 1;
}
int mouse_click(Playback *p, int button, int twice) {
    unsigned clicks = twice ? 2 : 1, i, generation;
    DWORD double_time = GetDoubleClickTime(), hold_max = double_time / 5, gap_max = double_time / 3;
    if (hold_max > 100) hold_max = 100;
    if (gap_max > 180) gap_max = 180;
    for (i = 0; i < clicks; ++i) {
        if (!mouse_button(p, button, 1) || !playback_wait(p, p->action_pace.enabled ?
            action_pace_pick(&p->action_pace, hold_max / 2, hold_max) : 0) ||
            !mouse_button(p, button, 0)) return 0;
        if (i + 1 < clicks) {
            generation = p->pause_generation;
            if (!playback_wait(p, p->action_pace.enabled ?
                action_pace_pick(&p->action_pace, gap_max / 2, gap_max) : double_time / 4) ||
                generation != p->pause_generation) return 0;
        }
    }
    return playback_wait(p, p->action_pace.enabled ? action_pace_pick(&p->action_pace, 180, 400) : 0);
}
int mouse_scroll(Playback *p, int notches) {
    INPUT input = {0};
    int remaining = notches;
    do {
        int step = p->action_pace.enabled ? (remaining < 0 ? -1 : remaining > 0 ? 1 : 0) : remaining;
        if (!mouse_pointer_ready(p)) return 0;
        input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = (DWORD)(step * WHEEL_DELTA);
        if (!playback_send(p, &input, 1)) return 0;
        remaining -= step;
        if (p->action_pace.enabled && !playback_wait(p, action_pace_pick(&p->action_pace, 130, 260))) return 0;
    } while (remaining);
    return 1;
}
int mouse_drag(Playback *p, int x1, int y1, int x2, int y2, DWORD duration, int curve) {
    POINT end;
    if (!window_point(p, x2, y2, &end)) return 0;
    return mouse_move(p, x1, y1, 0, 0) && mouse_button(p, 0, 1) &&
           mouse_move(p, x2, y2, duration, curve) && mouse_button(p, 0, 0);
}
int mouse_follow(Playback *p, const MotionPath *path) {
    ULONGLONG started = GetTickCount64(), paused_before = p->paused_ms;
    for (;;) {
        ULONGLONG elapsed;
        POINT point;
        if (!playback_poll(p) || !playback_guard(p)) return 0;
        elapsed = GetTickCount64() - started - (p->paused_ms - paused_before);
        if (elapsed > path->duration_ms) elapsed = path->duration_ms;
        motion_path_at(path, path->duration_ms ? (double)elapsed / path->duration_ms : 1.0, &point);
        if (!window_point_visible(point)) {
            playback_log(p, "cursor path leaves connected monitors"); return 0;
        }
        playback_note_cursor(point.x, point.y);
        if (!SetCursorPos(point.x, point.y)) {
            playback_log(p, "SetCursorPos failed (error %lu)", GetLastError()); return 0;
        }
        if (elapsed >= path->duration_ms) return 1;
        /* 8-14 ms steps, like the Python prototype. */
        if (!playback_wait(p, 8 + (DWORD)(elapsed % 7))) return 0;
    }
}
static int mouse_move_planned(Playback *p, MotionRng *rng, POINT target, int purpose,
    double width, unsigned reference_ms, int paced) {
    POINT start;
    MotionMove move;
    MONITORINFO info;
    const RECT *bounds = NULL;
    unsigned i;
    if (!GetCursorPos(&start)) return 0;
    if (labs(start.x - target.x) < 2 && labs(start.y - target.y) < 2) return 1;
    /* A miss must stay on the target's monitor. */
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(MonitorFromPoint(target, MONITOR_DEFAULTTONULL), &info)) {
        InflateRect(&info.rcMonitor, -2, -2); bounds = &info.rcMonitor;
    }
    if (paced) motion_control_move(rng, &p->hand, start, target, width, bounds, reference_ms, &move);
    else motion_move(rng, &p->hand, start, target, purpose, width, bounds, &move);
    for (i = 0; i < move.count; ++i)
        if ((i && !playback_wait(p, move.pause_ms)) || !mouse_follow(p, &move.segments[i])) return 0;
    return 1;
}
int mouse_move_human(Playback *p, MotionRng *rng, POINT target, int purpose, double width) {
    return mouse_move_planned(p, rng, target, purpose, width, 0, 0);
}
int mouse_move_control(Playback *p, MotionRng *rng, POINT target, double width, unsigned reference_ms) {
    return mouse_move_planned(p, rng, target, MOTION_AIM, width, reference_ms, 1);
}
int mouse_wheel(Playback *p, int notches) {
    INPUT input = {0};
    if (!notches || !mouse_pointer_ready(p)) return notches == 0;
    input.type = INPUT_MOUSE; input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = (DWORD)(notches * WHEEL_DELTA);
    return playback_send(p, &input, 1);
}
int mouse_scroll_nudge(Playback *p, MotionRng *rng, POINT anchor, const RECT *bounds) {
    POINT current, next;
    HWND hit;
    if (!playback_poll(p) || !playback_guard(p)) return 0;
    if (!GetCursorPos(&current)) {
        playback_log(p, "GetCursorPos failed while scrolling (error %lu)", GetLastError()); return 0;
    }
    if (!motion_scroll_nudge(rng, anchor, current, bounds, &next) || !window_point_visible(next)) return 1;
    hit = WindowFromPoint(next);
    if (!hit || GetAncestor(hit, GA_ROOT) != p->target) return 1;
    playback_note_cursor(next.x, next.y);
    if (!SetCursorPos(next.x, next.y)) {
        playback_log(p, "SetCursorPos failed while scrolling (error %lu)", GetLastError()); return 0;
    }
    return 1;
}
