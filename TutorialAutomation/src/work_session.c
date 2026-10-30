#include "work_session.h"
#include "browser_session.h"
#include "coding_steps.h"
#include "json_config.h"
#include "keyboard.h"
#include "loggy3.h"
#include "mouse.h"
#include "project_action.h"
#include "ui_targets.h"
#include "window_manager.h"
#include <uiautomationclient.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static const wchar_t *const start_keys[START_LOOK_COUNT] = {L"desktop", L"vscode", L"browser", L"app"};
static const wchar_t *const end_keys[END_LOOK_COUNT] = {L"direct", L"close_all", L"close_some"};
static const wchar_t *const close_keys[CLOSE_METHOD_COUNT] = {L"button", L"alt_f4", L"taskbar"};
static const wchar_t *const after_keys[AFTER_STOP_COUNT] = {L"front", L"minimize", L"close"};
static const wchar_t *const browser_keys[BROWSER_AFTER_COUNT] = {L"minimize", L"close", L"leave"};

const char *session_start_look_name(SessionStartLook l) {
    static const char *names[] = {"empty desktop", "VS Code already open", "browser open", "a random app open"};
    return l < START_LOOK_COUNT ? names[l] : "?";
}
const char *session_end_look_name(SessionEndLook l) {
    static const char *names[] = {"direct stop", "close everything", "close 1-2 apps"};
    return l < END_LOOK_COUNT ? names[l] : "?";
}
const char *session_close_method_name(SessionCloseMethod m) {
    static const char *names[] = {"its X button", "Alt+F4", "the taskbar menu"};
    return m < CLOSE_METHOD_COUNT ? names[m] : "?";
}
const char *session_after_stop_name(SessionAfterStop a) {
    static const char *names[] = {"left in front", "minimized", "window closed"};
    return a < AFTER_STOP_COUNT ? names[a] : "?";
}

/* ---------- settings ---------- */

static int fail(char *error, size_t cap, const char *message) { snprintf(error, cap, "%s", message); return 0; }
static int range(const JsonValue *parent, const wchar_t *key, unsigned lo, unsigned hi, unsigned out[2]) {
    const JsonValue *v = json_get(parent, key), *a;
    if (!v || v->kind != JSON_ARRAY || !(a = v->child) || !a->next || a->next->next ||
        a->kind != JSON_NUMBER || a->next->kind != JSON_NUMBER) return 0;
    out[0] = a->number; out[1] = a->next->number;
    return out[0] >= lo && out[1] <= hi && out[0] <= out[1];
}
static int number(const JsonValue *parent, const wchar_t *key, unsigned lo, unsigned hi, unsigned *out) {
    const JsonValue *v = json_get(parent, key);
    if (!v || v->kind != JSON_NUMBER || v->number < lo || v->number > hi) return 0;
    *out = v->number; return 1;
}
/* {"name": weight, ...}: every key known, weights 0-100, at least one above 0. */
static int weights(const JsonValue *parent, const wchar_t *key, const wchar_t *const *names, unsigned count, unsigned *out) {
    const JsonValue *object = json_get(parent, key);
    unsigned i, total = 0;
    if (!object || object->kind != JSON_OBJECT || !json_keys(object, names, count)) return 0;
    for (i = 0; i < count; ++i) {
        const JsonValue *w = json_get(object, names[i]);
        if (w && (w->kind != JSON_NUMBER || w->number > 100)) return 0;
        total += out[i] = w ? w->number : 0;
    }
    return total > 0;
}
int session_config_parse(const wchar_t *text, SessionConfig *c, char *error, size_t cap) {
    static const wchar_t *const keys[] = {L"version", L"target_minutes", L"coding_minutes", L"browsing_minutes",
        L"break_minutes", L"first_task_coding_percent", L"loggy_minimize_percent", L"start_app_close_percent",
        L"ending_reserve_seconds", L"check_seconds", L"restart_wait_seconds", L"weight_jitter_percent",
        L"idle_percent", L"idle_minutes", L"idle_check_minutes",
        L"start_looks", L"end_looks", L"close_methods", L"after_stop", L"browser_after"};
    JsonValue *root = NULL;
    const JsonValue *v;
    int ok = 0;
    memset(c, 0, sizeof(*c));
    if (!cap) return 0;
    *error = 0;
    if (!json_parse(text, &root, error, cap)) return 0;
    v = json_get(root, L"version");
    if (!json_keys(root, keys, sizeof(keys) / sizeof(*keys)) || !v || v->kind != JSON_NUMBER || v->number != 1)
        fail(error, cap, "session settings need \"version\": 1 and only documented fields");
    else if (!range(root, L"target_minutes", 1, 24 * 60, c->target_minutes) || !range(root, L"coding_minutes", 1, 600, c->coding_minutes) ||
             !range(root, L"browsing_minutes", 1, 600, c->browsing_minutes) || !range(root, L"break_minutes", 1, 240, c->break_minutes))
        fail(error, cap, "session settings: target_minutes, coding_minutes, browsing_minutes and break_minutes are [min, max]");
    else if (!number(root, L"first_task_coding_percent", 0, 100, &c->first_coding_percent) ||
             !number(root, L"loggy_minimize_percent", 0, 100, &c->loggy_minimize_percent) ||
             !number(root, L"start_app_close_percent", 0, 100, &c->start_app_close_percent) ||
             !number(root, L"weight_jitter_percent", 0, 90, &c->weight_jitter_percent))
        fail(error, cap, "session settings: the *_percent values are 0-100 (weight_jitter_percent 0-90)");
    else if (!number(root, L"ending_reserve_seconds", 0, 3600, &c->ending_reserve_seconds) ||
             !number(root, L"check_seconds", 5, 600, &c->check_seconds) ||
             !number(root, L"restart_wait_seconds", 10, 3600, &c->restart_wait_seconds))
        fail(error, cap, "session settings: ending_reserve_seconds 0-3600, check_seconds 5-600, restart_wait_seconds 10-3600");
    else if (!number(root, L"idle_percent", 0, 50, &c->idle_percent) || !range(root, L"idle_minutes", 1, 120, c->idle_minutes) ||
             !range(root, L"idle_check_minutes", 1, 600, c->idle_check_minutes))
        fail(error, cap, "session settings: idle_percent 0-50, idle_minutes and idle_check_minutes [min, max]");
    else if (!weights(root, L"start_looks", start_keys, START_LOOK_COUNT, c->start_looks) ||
             !weights(root, L"end_looks", end_keys, END_LOOK_COUNT, c->end_looks) ||
             !weights(root, L"close_methods", close_keys, CLOSE_METHOD_COUNT, c->close_methods) ||
             !weights(root, L"after_stop", after_keys, AFTER_STOP_COUNT, c->after_stop) ||
             !weights(root, L"browser_after", browser_keys, BROWSER_AFTER_COUNT, c->browser_after))
        fail(error, cap, "session settings: start_looks, end_looks, close_methods, after_stop and browser_after are "
            "{name: weight 0-100} with known names and at least one weight above 0");
    else ok = 1;
    json_free(root);
    return ok;
}
int session_config_load(const wchar_t *path, SessionConfig *c, char *error, size_t cap) {
    wchar_t *text = NULL;
    int ok;
    memset(c, 0, sizeof(*c));
    if (!read_utf8(path, &text, error, cap)) return 0;
    ok = session_config_parse(text, c, error, cap);
    free(text);
    return ok;
}

/* ---------- the day's plan ---------- */

unsigned session_pick(const unsigned *w, unsigned count, MotionRng *rng) {
    unsigned total = 0, i, roll;
    for (i = 0; i < count; ++i) total += w[i];
    if (!total) return 0;
    roll = motion_between(rng, 0, total - 1);
    for (i = 0; i < count; ++i) { if (roll < w[i]) return i; roll -= w[i]; }
    return count - 1;
}
/* Today's weights: each moved up or down by up to jitter percent, so no two days use the same odds. */
static void jittered(const unsigned *in, unsigned count, unsigned jitter, MotionRng *rng, unsigned *out) {
    unsigned i;
    for (i = 0; i < count; ++i)
        out[i] = in[i] ? (unsigned)(in[i] * motion_uniform(rng, 1 - jitter / 100.0, 1 + jitter / 100.0) + 0.5) : 0;
    for (i = 0; i < count && !out[i]; ++i) ;
    if (i == count) for (i = 0; i < count; ++i) out[i] = in[i];
}
void session_plan(const SessionConfig *c, uint32_t seed, unsigned recorded, unsigned randomness, SessionPlan *plan) {
    MotionRng r;
    unsigned w[8], remaining, total = 0, at;
    double share, mean_check, mean_idle;
    unsigned chance;
    int coding;
    memset(plan, 0, sizeof(*plan));
    motion_seed(&r, seed ^ UINT32_C(0x51ed270b));
    plan->target_seconds = motion_between(&r, c->target_minutes[0], c->target_minutes[1]) * 60 + motion_between(&r, 0, 59);
    if (plan->target_seconds > c->target_minutes[1] * 60) plan->target_seconds = c->target_minutes[1] * 60;
    jittered(c->start_looks, START_LOOK_COUNT, c->weight_jitter_percent, &r, w);
    plan->start = (SessionStartLook)session_pick(w, START_LOOK_COUNT, &r);
    plan->start_app = (SideApp)motion_between(&r, 0, 2); /* Notepad, File Explorer, or Calculator */
    plan->close_start_app = motion_chance(&r, c->start_app_close_percent);
    plan->minimize_loggy = motion_chance(&r, c->loggy_minimize_percent);
    jittered(c->end_looks, END_LOOK_COUNT, c->weight_jitter_percent, &r, w);
    plan->end = (SessionEndLook)session_pick(w, END_LOOK_COUNT, &r);
    plan->close_count = motion_between(&r, 1, 2);
    jittered(c->after_stop, AFTER_STOP_COUNT, c->weight_jitter_percent, &r, w);
    plan->after = (SessionAfterStop)session_pick(w, AFTER_STOP_COUNT, &r);
    /* Slots until an hour past the target; the target ends the day, not the plan. */
    remaining = plan->target_seconds > recorded ? plan->target_seconds - recorded : 0;
    coding = motion_chance(&r, c->first_coding_percent);
    while (total < remaining + 3600 && plan->slot_count < SESSION_MAX_SLOTS) {
        SessionSlot *slot = &plan->slots[plan->slot_count++];
        slot->coding = coding;
        slot->minutes = coding ? motion_between(&r, c->coding_minutes[0], c->coding_minutes[1])
                               : motion_between(&r, c->browsing_minutes[0], c->browsing_minutes[1]);
        total += slot->minutes * 60;
        coding = !coding;
    }
    /* Idles away from the desk: a chance every idle_check_minutes of work, chosen so that idling
       takes about idle_percent of the day (a little more for a more random profile). */
    share = c->idle_percent / 100.0 * (0.8 + 0.4 * (randomness > 100 ? 100 : randomness) / 100.0);
    mean_check = (c->idle_check_minutes[0] + c->idle_check_minutes[1]) * 30.0;
    /* The largest chosen minute value is capped before its random seconds are added. */
    mean_idle = (c->idle_minutes[0] + c->idle_minutes[1]) * 30.0 +
        29.5 * (c->idle_minutes[1] - c->idle_minutes[0]) /
        (c->idle_minutes[1] - c->idle_minutes[0] + 1);
    /* Share = chance * idle / (check + chance * idle), solved for the chance. */
    chance = share > 0 && share < 1 ? (unsigned)(share * mean_check / (mean_idle * (1 - share)) * 100 + 0.5) : 0;
    if (chance > 100) chance = 100;
    for (at = 0; plan->idle_count < SESSION_MAX_IDLES; ) {
        at += motion_between(&r, c->idle_check_minutes[0], c->idle_check_minutes[1]) * 60;
        if (at + 300 >= remaining) break;
        if (motion_chance(&r, chance)) {
            SessionIdle *idle = &plan->idles[plan->idle_count++];
            idle->at = at;
            idle->seconds = motion_between(&r, c->idle_minutes[0], c->idle_minutes[1]) * 60 + motion_between(&r, 0, 59);
            if (idle->seconds > c->idle_minutes[1] * 60) idle->seconds = c->idle_minutes[1] * 60;
            at += idle->seconds;
        }
    }
}
static void hm(unsigned long seconds, char *out, size_t cap) { snprintf(out, cap, "%luh %02lum", seconds / 3600, seconds / 60 % 60); }
static void clock_at(ULONGLONG offset_seconds, char *out, size_t cap) {
    FILETIME ft;
    ULARGE_INTEGER t;
    SYSTEMTIME local, utc;
    GetSystemTimeAsFileTime(&ft);
    t.LowPart = ft.dwLowDateTime; t.HighPart = ft.dwHighDateTime;
    t.QuadPart += offset_seconds * 10000000ULL;
    ft.dwLowDateTime = t.LowPart; ft.dwHighDateTime = t.HighPart;
    FileTimeToSystemTime(&ft, &utc);
    SystemTimeToTzSpecificLocalTime(NULL, &utc, &local);
    snprintf(out, cap, "%02u:%02u", local.wHour, local.wMinute);
}
void session_print_plan(const SessionConfig *c, const SessionPlan *plan, const Profile *profile, unsigned recorded) {
    char name[160], a[32], b[32], when[16];
    unsigned i, k = 0, idle_total = 0, remaining = plan->target_seconds > recorded ? plan->target_seconds - recorded : 0;
    ULONGLONG at = 60, end_at;
    WideCharToMultiByte(CP_UTF8, 0, profile->name, -1, name, sizeof(name), NULL, NULL);
    hm(recorded, a, sizeof(a)); hm(plan->target_seconds, b, sizeof(b));
    printf("Session for %s: today so far %s (Loggy3), target %s", name, a, b);
    hm(remaining, a, sizeof(a)); printf(", to record now %s.\n", a);
    if (!remaining) { puts("The target is already reached: a live run would press nothing and end."); return; }
    printf("Start look: %s%s%s; then Loggy3 Start, and Loggy3 is %s.\n", session_start_look_name(plan->start),
        plan->start == START_APP ? " (" : "", plan->start == START_APP ? side_app_name(plan->start_app) : "",
        plan->minimize_loggy ? "minimized" : "left behind the work");
    if (plan->start == START_APP) printf("  The start app is %s after recording starts.\n", plan->close_start_app ? "closed" : "left open");
    for (i = 0; i < plan->idle_count; ++i) idle_total += plan->idles[i].seconds;
    printf("Timeline (times are approximate; other-app breaks of %u-%u min happen inside slots;\n"
        "idles away from the desk: %u, %u min in all, counted in the target):\n",
        c->break_minutes[0], c->break_minutes[1], plan->idle_count, (idle_total + 30) / 60);
    end_at = at + remaining;
    for (i = 0; i < plan->slot_count && at < end_at; ++i) {
        ULONGLONG slot_end = at + plan->slots[i].minutes * 60ULL;
        clock_at(at, when, sizeof(when));
        printf("  %s  %-9s %3u min%s\n", when, plan->slots[i].coding ? "CODING" : "BROWSING", plan->slots[i].minutes,
            slot_end > end_at ? "  (cut short: target reached)" : "");
        /* Idles are timed by recorded work since Loggy3 Start, which begins with the first slot. */
        for (; k < plan->idle_count && 60 + plan->idles[k].at < slot_end && 60 + plan->idles[k].at < end_at; ++k) {
            clock_at(60 + plan->idles[k].at, when, sizeof(when));
            printf("  %s    idle (away from the desk) %u min %02u s\n", when, plan->idles[k].seconds / 60, plan->idles[k].seconds % 60);
        }
        at = slot_end;
    }
    clock_at(end_at > c->ending_reserve_seconds ? end_at - c->ending_reserve_seconds : end_at, when, sizeof(when));
    printf("  %s  ENDING    %s", when, session_end_look_name(plan->end));
    if (plan->end == END_CLOSE_SOME) printf(" (%u)", plan->close_count);
    printf("; each app closed by X / Alt+F4 / taskbar menu at random; then Loggy3 Stop, Loggy3 %s.\n", session_after_stop_name(plan->after));
    clock_at(end_at, when, sizeof(when));
    printf("  %s  recording stopped near the target; summary to the log and Telegram.\n", when);
}

/* ---------- running ---------- */

typedef struct {
    Playback *p;
    WorkSession *s;
    SessionPlan plan;
    MotionRng rng;
    SideAppsConfig breaks;
    SideAppsTimer timer;
    ProjectHistory history;
    int history_loaded, coding_done;
    long t0, banked, last_timer;
    unsigned stopped_seen, restarts, files_done, failures, slot;
    int recording, loggy_lost, target_reached;
    ULONGLONG coding_ms, browsing_ms;
    long work_start;          /* recorded seconds when this session's recording began */
    unsigned next_idle, idles_taken, idle_seconds;
    char last_task[160];      /* restored on Telegram after an idle */
} Run;
static Run *watching;

static long recorded(const Run *r) { return r->t0 + r->banked + r->last_timer; }
static void hms(long seconds, char *out, size_t cap) {
    if (seconds < 0) seconds = 0;
    snprintf(out, cap, "%ldh %02ldm %02lds", seconds / 3600, seconds / 60 % 60, seconds % 60);
}
static int stopped_by_user(Playback *p) { return InterlockedCompareExchange(&p->user_stop, 0, 0) != 0; }
static int winding_down(Playback *p) { return InterlockedCompareExchange(&p->wind_down, 0, 0) != 0; }

/* Every check_seconds while working: follow Loggy3's timer, notice a recording that stopped by
   itself, and ask the current task to end once the target is near. Reads only; no input. */
static void watch(Playback *p) {
    Run *r = watching;
    LoggySnapshot s;
    HWND w;
    LoggyState state;
    if (!r || !r->recording || !(w = loggy_window(NULL)) || !loggy_snapshot(w, &s)) return;
    state = loggy_recording_state(&s, NULL);
    if (state == LOGGY_RECORDING) {
        long timer = loggy_timer_seconds(s.timer);
        /* Loggy3 sometimes starts a new recording part while it keeps recording: the timer goes back
           to zero and the earlier part moves into the day's total. Bank it, or the day would come up short. */
        if (timer >= 0 && timer + 5 < r->last_timer) {
            char text[32];
            r->banked += r->last_timer;
            hms(recorded(r) + timer, text, sizeof(text));
            playback_log(p, "Loggy3's timer restarted at %s (was %lds); counting the earlier part, today %s",
                s.timer, r->last_timer, text);
        }
        if (timer >= 0) r->last_timer = timer;
        r->stopped_seen = 0;
    } else if (state == LOGGY_STOPPED && ++r->stopped_seen >= 2 && !r->loggy_lost) {
        char text[32];
        hms(recorded(r), text, sizeof(text));
        r->loggy_lost = 1;
        playback_log(p, "SESSION ALERT: Loggy3 stopped recording by itself (today %s); restarting it", text);
        InterlockedExchange(&p->yield, 1);
    }
    if (!r->target_reached && recorded(r) + (long)r->s->config->ending_reserve_seconds >= (long)r->plan.target_seconds) {
        char text[32];
        hms(recorded(r), text, sizeof(text));
        r->target_reached = 1;
        playback_log(p, "today's recorded time is %s; the target is near, ending the day", text);
        InterlockedExchange(&p->yield, 1);
    }
}

/* After an error: clear a stop that the user did not ask for, and wait out a locked desktop. */
static int recover(Playback *p) {
    if (stopped_by_user(p)) return 0;
    playback_release(p);
    while (!window_desktop_ready()) {
        if (stopped_by_user(p)) return 0;
        Sleep(1000);
    }
    InterlockedExchange(&p->stopped, 0);
    return window_fix_target(p);
}
static void task(Run *r, const char *what, unsigned minutes) {
    char until[16];
    clock_at(minutes * 60ULL, until, sizeof(until));
    if (minutes) snprintf(r->last_task, sizeof(r->last_task), "%s, slot %u, %u min (until about %s)", what, r->slot, minutes, until);
    else snprintf(r->last_task, sizeof(r->last_task), "%s", what);
    playback_log(r->p, "SESSION TASK: %s", r->last_task);
}
/* Away from the desk: no input for a while, now and then one small nudge of the mouse halfway,
   like sitting back down. Ends early when the day has to end. Loggy3 keeps recording. */
static int idle(Run *r, unsigned seconds) {
    Playback *p = r->p;
    unsigned waited = 0, nudge_at = motion_chance(&r->rng, 30) ? seconds * motion_between(&r->rng, 40, 80) / 100 : 0;
    char text[160];
    snprintf(text, sizeof(text), "idle (away from the desk), %u min %02u s", seconds / 60, seconds % 60);
    playback_log(p, "SESSION TASK: %s", text);
    while (waited < seconds) {
        if (InterlockedCompareExchange(&p->wind_down, 0, 0) || InterlockedCompareExchange(&p->yield, 0, 0)) break;
        if (!playback_wait(p, 1000)) return 0;
        ++waited;
        if (nudge_at && waited == nudge_at) {
            POINT point;
            if (GetCursorPos(&point)) {
                point.x += (LONG)motion_between(&r->rng, 0, 120) - 60; point.y += (LONG)motion_between(&r->rng, 0, 60) - 30;
                if (window_point_visible(point) && !mouse_move_human(p, &r->rng, point, MOTION_REST, 160)) return 0;
            }
        }
    }
    r->idle_seconds += waited; ++r->idles_taken;
    playback_log(p, "back from idle after %u min %02u s", waited / 60, waited % 60);
    if (*r->last_task) playback_log(p, "SESSION TASK: %s", r->last_task);
    return 1;
}
/* Called at safe points (coding checkpoints, between browsing steps, between slots): take the next
   planned idle once enough work has been recorded since Loggy3 Start. */
static int safe_point(Playback *p) {
    Run *r = watching;
    if (!r || !r->recording || r->next_idle >= r->plan.idle_count || playback_yield(p)) return 1;
    if (recorded(r) - r->work_start < (long)r->plan.idles[r->next_idle].at) return 1;
    return idle(r, r->plan.idles[r->next_idle++].seconds);
}

/* One coding slot: keep typing the profile's current project (and the next ones) until the slot
   ends; the typer then finishes its line and saves. open_only opens the project in VS Code and
   stops before typing. Returns 1 ok, 0 error, 2 nothing to code. */
static int coding_slot(Run *r, int open_only) {
    Playback *p = r->p;
    char error[512];
    if (!r->history_loaded) {
        if (!project_history_load(r->s->programming, r->s->profile, &r->history, error, sizeof(error))) {
            playback_log(p, "project history: %s", error); return 2;
        }
        r->history_loaded = 1;
    }
    for (;;) {
        ProjectReference reference;
        ProjectPlan plan;
        HistoryProject *started = NULL;
        CodingProject job;
        wchar_t destination[MAX_PATH];
        size_t i, before = 0, after = 0;
        int ok;
        if (!open_only && playback_yield(p)) break;
        memset(&reference, 0, sizeof(reference)); memset(&plan, 0, sizeof(plan)); memset(&job, 0, sizeof(job));
        if (!project_select(r->s->programming, r->s->manifest, r->s->profile, &r->history, 1, &reference, &started,
                destination, error, sizeof(error))) {
            playback_log(p, "SESSION ALERT: no coding project to work on (%s); browsing instead from now on. "
                "Add projects to profiles\\projects.json", error);
            r->coding_done = 1; project_reference_free(&reference); return 2;
        }
        if (!project_plan_build(&reference, started, destination, &plan, error, sizeof(error)) || plan.conflicts) {
            playback_log(p, "the coding project cannot continue (%s); browsing instead from now on",
                plan.conflicts ? "files conflict with the reference" : error);
            r->coding_done = 1; project_plan_free(&plan); project_reference_free(&reference); return 2;
        }
        for (i = 0; i < reference.file_count; ++i) before += plan.steps[i].kind == STEP_CREATE || plan.steps[i].kind == STEP_RESUME;
        job.profile = r->s->profile; job.config = r->s->programming; job.history = &r->history;
        job.reference = &reference; job.plan = &plan; job.seed = r->s->seed + r->slot * 7919u;
        job.side_apps = &r->breaks; job.apps = &r->timer;
        ok = coding_project_run(p, &job);
        for (i = 0; i < reference.file_count; ++i) after += plan.steps[i].kind == STEP_CREATE || plan.steps[i].kind == STEP_RESUME;
        r->files_done += (unsigned)(before - after);
        project_plan_free(&plan); project_reference_free(&reference);
        if (!ok) return 0;
        if (open_only || job.ended_early) break;
        /* That project is complete; the next one starts in the same slot. */
    }
    return 1;
}
static HWND front_of(const wchar_t *process) {
    HWND front = GetForegroundWindow();
    return front && window_process_is(front, process) ? front : NULL;
}
static int click_rect(Playback *p, MotionRng *rng, const RECT *r, int button) {
    POINT point;
    if (!motion_click_point(rng, r, &point) || !mouse_move_human(p, rng, point, MOTION_AIM, r->bottom - r->top) ||
        !playback_wait(p, motion_between(rng, 80, 300))) return 0;
    return mouse_click(p, button, 0);
}
static int shortcut(Playback *p, WORD a, WORD b) {
    WORD keys[2];
    keys[0] = a; keys[1] = b;
    return keyboard_shortcut(p, keys, 2);
}

/* ---------- open apps, and closing them ---------- */

typedef struct { HWND window; const wchar_t *taskbar; const char *name; int vscode; } OpenApp;
typedef struct { OpenApp items[16]; unsigned count; } OpenApps;
static BOOL CALLBACK collect_app(HWND w, LPARAM parameter) {
    OpenApps *apps = (OpenApps *)parameter;
    OpenApp app;
    wchar_t title[256], cls[64];
    if (apps->count >= 16 || !IsWindowVisible(w) || GetWindow(w, GW_OWNER) || !GetWindowTextW(w, title, 256) ||
        !GetClassNameW(w, cls, 64)) return TRUE;
    memset(&app, 0, sizeof(app)); app.window = w;
    if (window_process_is(w, L"Code.exe")) { app.taskbar = L"Visual Studio Code"; app.name = "VS Code"; app.vscode = 1; }
    else if (window_process_is(w, L"chrome.exe") && !wcscmp(cls, L"Chrome_WidgetWin_1")) { app.taskbar = L"Google Chrome"; app.name = "Chrome"; }
    else if (window_process_is(w, L"Notepad.exe")) { app.taskbar = L"Notepad"; app.name = "Notepad"; }
    else if (!wcscmp(cls, L"CabinetWClass") && window_process_is(w, L"explorer.exe")) { app.taskbar = L"File Explorer"; app.name = "File Explorer"; }
    else if (!wcscmp(cls, L"ApplicationFrameWindow") && !wcscmp(title, L"Calculator")) { app.taskbar = L"Calculator"; app.name = "Calculator"; }
    else if (!wcscmp(cls, L"ApplicationFrameWindow") && wcsstr(title, L"Photos")) { app.taskbar = L"Photos"; app.name = "Photos"; }
    else return TRUE;
    apps->items[apps->count++] = app;
    return TRUE;
}
static void open_apps(OpenApps *apps) { memset(apps, 0, sizeof(*apps)); EnumWindows(collect_app, (LPARAM)apps); }
static int gone(HWND w) { return !IsWindow(w) || !IsWindowVisible(w); }
/* Bring a window forward the way a person would: its taskbar icon, else switch to it directly. */
static int bring_front(Playback *p, MotionRng *rng, const OpenApp *app) {
    if (GetForegroundWindow() == app->window && !IsIconic(app->window)) { p->target = app->window; return 1; }
    return taskbar_show_window(p, rng, app->taskbar, app->window);
}
static int close_app(Run *r, const OpenApp *app, SessionCloseMethod method) {
    Playback *p = r->p;
    RECT rect;
    unsigned waited;
    int done = 0;
    if (gone(app->window)) return 1;
    playback_log(p, "closing %s with %s", app->name, session_close_method_name(method));
    if (!bring_front(p, &r->rng, app)) return 0;
    if (method == CLOSE_BUTTON) {
        if (ui_find_control_rect(p, UIA_ButtonControlTypeId, L"Close", &rect)) done = click_rect(p, &r->rng, &rect, 0);
        else playback_log(p, "no Close button found; using Alt+F4");
    } else if (method == CLOSE_TASKBAR) {
        HWND menu;
        if (taskbar_icon_click(p, &r->rng, app->taskbar, TASKBAR_MENU) == 1 && playback_wait(p, motion_between(&r->rng, 900, 1400)) &&
            (menu = GetForegroundWindow())) {
            p->target = menu;
            if (ui_find_control_rect(p, UIA_MenuItemControlTypeId, L"Close window", &rect) ||
                ui_find_control_rect(p, UIA_ButtonControlTypeId, L"Close window", &rect) ||
                ui_find_control_rect(p, UIA_ListItemControlTypeId, L"Close window", &rect) ||
                ui_find_control_rect(p, UIA_MenuItemControlTypeId, L"Close all windows", &rect) ||
                ui_find_control_rect(p, UIA_ButtonControlTypeId, L"Close all windows", &rect) ||
                ui_find_control_rect(p, UIA_ListItemControlTypeId, L"Close all windows", &rect)) done = click_rect(p, &r->rng, &rect, 0);
            else {
                playback_log(p, "the taskbar menu has no Close window entry; using Alt+F4");
                keyboard_press(p, VK_ESCAPE);
                if (!playback_wait(p, 500) || !bring_front(p, &r->rng, app)) return 0;
            }
        } else if (stopped_by_user(p)) return 0;
        else { playback_log(p, "the taskbar menu did not open; using Alt+F4"); if (!bring_front(p, &r->rng, app)) return 0; }
    }
    if (!done) {
        p->target = app->window;
        if (!shortcut(p, VK_MENU, VK_F4)) return 0;
    }
    for (waited = 0; waited < 6000 && !gone(app->window); waited += 200) if (!playback_wait(p, 200)) return 0;
    if (!gone(app->window)) {
        /* A save prompt: VS Code saves (its default), other apps don't save notes twice. */
        HWND front = GetForegroundWindow();
        if (front && front != app->window) {
            p->target = front;
            playback_log(p, "%s asked about unsaved changes; answering %s", app->name, app->vscode ? "Save" : "Don't save");
            if (app->vscode ? !keyboard_press(p, VK_RETURN) : !keyboard_press(p, L'N')) return 0;
            for (waited = 0; waited < 5000 && !gone(app->window); waited += 200) if (!playback_wait(p, 200)) return 0;
        }
    }
    if (!gone(app->window)) { playback_log(p, "%s did not close; leaving it", app->name); return 1; }
    playback_log(p, "%s closed", app->name);
    /* Focus may now be on an invisible window; find a real one before the next step. */
    return window_fix_target(p);
}

/* ---------- slots ---------- */

static int browsing_slot(Run *r, unsigned minutes, int quiet) {
    Playback *p = r->p;
    BrowseSession session;
    HWND browser;
    unsigned weights[BROWSER_AFTER_COUNT];
    int ok;
    memset(&session, 0, sizeof(session));
    session.profile = r->s->profile; session.catalog = r->s->catalog;
    session.duration_ms = minutes ? minutes * 60000 : motion_between(&r->rng, 60, 120) * 1000;
    session.seed = r->s->seed + r->slot * 104729u; session.via_taskbar = 1;
    if (!quiet) { session.side_apps = &r->breaks; session.apps = &r->timer; }
    ok = browser_session_run(p, &session);
    if (!ok || quiet || winding_down(p) || r->target_reached) return ok;
    /* Done with the browser for now: minimize it, close it, or just leave it behind the next task. */
    if (!(browser = front_of(L"chrome.exe"))) return 1;
    jittered(r->s->config->browser_after, BROWSER_AFTER_COUNT, 0, &r->rng, weights);
    switch (session_pick(weights, BROWSER_AFTER_COUNT, &r->rng)) {
        case BROWSER_MINIMIZE: {
            RECT rect;
            p->target = browser;
            playback_log(p, "minimizing Chrome");
            if (ui_find_control_rect(p, UIA_ButtonControlTypeId, L"Minimize", &rect)) return click_rect(p, &r->rng, &rect, 0);
            return shortcut(p, VK_LWIN, VK_DOWN);
        }
        case BROWSER_CLOSE: {
            OpenApp app;
            memset(&app, 0, sizeof(app));
            app.window = browser; app.taskbar = L"Google Chrome"; app.name = "Chrome";
            return close_app(r, &app, (SessionCloseMethod)session_pick(r->s->config->close_methods, CLOSE_METHOD_COUNT, &r->rng));
        }
        default: playback_log(p, "leaving Chrome open behind the next task"); return 1;
    }
}

/* ---------- Loggy3 ---------- */

static int loggy(Run *r, LoggyAction action, int minimize_after) {
    LoggyJob job;
    int ok;
    memset(&job, 0, sizeof(job));
    job.action = action; job.profile = r->s->profile; job.seed = r->s->seed + r->slot * 31u + action;
    job.minimize_after = minimize_after; job.sessions_page = action == LOGGY_RECORD;
    ok = loggy_job(r->p, &job);
    if (action == LOGGY_RECORD || action == LOGGY_STOP) return ok && job.result == (action == LOGGY_RECORD ? LOGGY_RECORDING : LOGGY_STOPPED);
    return ok;
}
static int read_today(Run *r, LoggyToday *today) {
    HWND w = loggy_window(NULL);
    unsigned tries;
    for (tries = 0; tries < 20; ++tries) {
        if (w && loggy_today(w, NULL, today) && (today->state == LOGGY_STOPPED || today->state == LOGGY_RECORDING)) return 1;
        if (!playback_wait(r->p, 500)) return 0;
        w = loggy_window(NULL);
    }
    return 0;
}
/* Loggy3 stopped by itself: bank what it recorded, then Start again (once more after a wait). */
static int restart_loggy(Run *r) {
    Playback *p = r->p;
    unsigned attempt;
    r->banked += r->last_timer; r->last_timer = 0; r->stopped_seen = 0; r->loggy_lost = 0;
    for (attempt = 0; attempt < 2; ++attempt) {
        if (attempt) {
            playback_log(p, "SESSION ALERT: Loggy3 did not start recording; trying again in %u s", r->s->config->restart_wait_seconds);
            if (!playback_wait(p, r->s->config->restart_wait_seconds * 1000)) return 0;
        }
        if (loggy(r, LOGGY_RECORD, 1)) {
            ++r->restarts;
            playback_log(p, "SESSION ALERT: Loggy3 is recording again");
            return 1;
        }
        if (!recover(p)) return 0;
    }
    playback_log(p, "SESSION ALERT: Loggy3 could not be restarted; ending the day");
    r->recording = 0;
    return 0;
}

/* ---------- start and end ---------- */

static int start_look(Run *r) {
    Playback *p = r->p;
    task(r, "getting ready to record", 0);
    playback_log(p, "start look: %s", session_start_look_name(r->plan.start));
    switch (r->plan.start) {
        case START_DESKTOP: return coding_step_show_desktop(p);
        case START_VSCODE: {
            int result;
            /* Open the project in VS Code and stop before typing anything. */
            InterlockedExchange(&p->yield, 1);
            result = coding_slot(r, 1);
            InterlockedExchange(&p->yield, 0);
            return result != 0;
        }
        case START_BROWSER: return browsing_slot(r, 0, 1);
        default: return side_apps_open(p, &r->rng, r->plan.start_app);
    }
}
static int end_look(Run *r) {
    Playback *p = r->p;
    OpenApps apps;
    unsigned order[16], count, i;
    open_apps(&apps);
    task(r, "ending the day", 0);
    playback_log(p, "end look: %s (%u app(s) open)", session_end_look_name(r->plan.end), apps.count);
    if (r->plan.end == END_DIRECT || !apps.count) return 1;
    for (i = 0; i < apps.count; ++i) order[i] = i;
    for (i = apps.count; i > 1; --i) { unsigned k = motion_between(&r->rng, 0, i - 1), t = order[i - 1]; order[i - 1] = order[k]; order[k] = t; }
    count = r->plan.end == END_CLOSE_ALL ? apps.count : r->plan.close_count < apps.count ? r->plan.close_count : apps.count;
    for (i = 0; i < count; ++i) {
        unsigned w[CLOSE_METHOD_COUNT];
        jittered(r->s->config->close_methods, CLOSE_METHOD_COUNT, r->s->config->weight_jitter_percent, &r->rng, w);
        if (!close_app(r, &apps.items[order[i]], (SessionCloseMethod)session_pick(w, CLOSE_METHOD_COUNT, &r->rng))) {
            if (!recover(p)) return 0;
        }
    }
    return 1;
}
static void summary(Run *r, const char *ending) {
    LoggyToday today;
    char total[32], target[32], coding[32], browsing[32], idled[32], name[160];
    long seconds = recorded(r);
    HWND w = loggy_window(NULL);
    if (w && loggy_today(w, NULL, &today) && today.found && loggy_today_seconds(&today) >= seconds) seconds = loggy_today_seconds(&today);
    hms(seconds, total, sizeof(total)); hms((long)r->plan.target_seconds, target, sizeof(target));
    hms((long)(r->coding_ms / 1000), coding, sizeof(coding)); hms((long)(r->browsing_ms / 1000), browsing, sizeof(browsing));
    hms((long)r->idle_seconds, idled, sizeof(idled));
    WideCharToMultiByte(CP_UTF8, 0, r->s->profile->local_id, -1, name, sizeof(name), NULL, NULL);
    playback_log(r->p, "SESSION SUMMARY: %s %s. Today %s recorded (target %s). Coding %s, browsing %s (slot times include "
        "breaks and idles), other-app breaks %u, idles %u (%s). Files finished %u. Loggy3 auto-restarts %u. Ending: %s.", name, ending,
        total, target, coding, browsing, r->timer.breaks, r->idles_taken, idled, r->files_done, r->restarts, session_end_look_name(r->plan.end));
}

int work_session_run(Playback *p, WorkSession *s) {
    Run *r = calloc(1, sizeof(*r));
    LoggyToday today;
    char text[32], target[32];
    int ok = 0, hard = 0;
    unsigned i;
    if (!r) { playback_log(p, "out of memory"); return 0; }
    r->p = p; r->s = s;
    p->typing_speed = s->profile->typing_speed;
    p->typo_percent = profile_mistype_percent(s->profile);
    p->typo_state = s->seed ^ 0x9e3779b9u;
    motion_seed(&r->rng, s->seed * 2654435761u + 3);
    motion_hand_for(&p->hand, motion_hash(s->profile->local_id), s->profile->randomness_score);
    p->client_coords = 0; p->soft_stop = 1;
    if (!p->target) p->target = GetForegroundWindow();
    /* Other-app breaks use the session's lengths and one timer across every slot. */
    r->breaks = *s->side_apps;
    r->breaks.minutes[0] = s->config->break_minutes[0]; r->breaks.minutes[1] = s->config->break_minutes[1];
    side_apps_timer_init(&r->timer, &r->breaks, s->profile, s->seed, p);

    /* 0. Prepare: today's total so far, from Loggy3. */
    if (!loggy_window(NULL)) {
        playback_log(p, "Loggy3 is not open; opening it from the Start menu");
        if (start_menu_open(p, &r->rng, L"Loggy3") != 1 || !playback_wait(p, 8000)) {
            playback_log(p, "SESSION ALERT: Loggy3 could not be opened; no recording, nothing done"); goto done;
        }
    }
    if (!read_today(r, &today)) { playback_log(p, "SESSION ALERT: Loggy3's recording state could not be read; nothing done"); goto done; }
    if (!today.found) {
        playback_log(p, "today's row is not on Loggy3's screen; opening its Sessions page");
        if (!loggy_open_sessions(p, &r->rng) || !read_today(r, &today)) goto done;
        if (!today.found) playback_log(p, "SESSION ALERT: today's total could not be read from Loggy3; counting from 0");
    }
    r->t0 = today.found ? (long)today.day.seconds : 0;
    r->recording = today.state == LOGGY_RECORDING;
    r->last_timer = r->recording && today.timer_seconds > 0 ? today.timer_seconds : 0;
    session_plan(s->config, s->seed, (unsigned)recorded(r), s->profile->randomness_score, &r->plan);
    hms(recorded(r), text, sizeof(text)); hms((long)r->plan.target_seconds, target, sizeof(target));
    playback_log(p, "SESSION TARGET: %s today (so far %s%s)", target, text, r->recording ? ", already recording" : "");
    watching = r;
    p->watch = watch; p->watch_ms = s->config->check_seconds * 1000; p->next_watch = 0;
    p->safe_point = safe_point;
    if (recorded(r) + (long)s->config->ending_reserve_seconds >= (long)r->plan.target_seconds) {
        playback_log(p, "today's target is already reached; nothing to do");
        summary(r, "already done today"); ok = 1; goto done;
    }

    /* 1. Start: the start look, then Loggy3 Start (unless it is already recording). */
    if (!r->recording) {
        if (!start_look(r) && !recover(p)) { hard = 1; goto done; }
        if (winding_down(p)) { summary(r, "stopped before recording started"); ok = 1; goto done; }
        task(r, "starting the Loggy3 recording", 0);
        if (!loggy(r, LOGGY_RECORD, r->plan.minimize_loggy)) {
            if (stopped_by_user(p)) { hard = 1; goto done; }
            playback_log(p, "SESSION ALERT: Loggy3 did not start recording; nothing done"); goto done;
        }
        r->recording = 1; r->last_timer = 0;
        /* The day's row may only now be on screen (the Start step opens the Sessions page). */
        if (!today.found && read_today(r, &today) && today.found) r->t0 = (long)today.day.seconds;
        r->work_start = recorded(r);
        if (r->plan.start == START_APP && r->plan.close_start_app) {
            OpenApps apps;
            open_apps(&apps);
            for (i = 0; i < apps.count; ++i)
                if (!apps.items[i].vscode && strcmp(apps.items[i].name, "Chrome")) {
                    if (!close_app(r, &apps.items[i], (SessionCloseMethod)session_pick(s->config->close_methods, CLOSE_METHOD_COUNT, &r->rng)) &&
                        !recover(p)) { hard = 1; goto stop; }
                    break;
                }
        }
    } else {
        playback_log(p, "Loggy3 is already recording; continuing the day");
        r->work_start = recorded(r);
    }

    /* 2. Work: coding and browsing in turn, with other-app breaks inside, until the target or /stop. */
    for (i = 0; !winding_down(p) && !r->target_reached; ++i) {
        SessionSlot slot = r->plan.slots[i % r->plan.slot_count];
        ULONGLONG started = GetTickCount64();
        int result;
        if (r->loggy_lost) {
            p->deadline = 0; InterlockedExchange(&p->yield, 0);
            if (!restart_loggy(r)) { if (stopped_by_user(p)) { hard = 1; goto done; } goto stop; }
        }
        r->slot = i + 1;
        if (!window_fix_target(p) && !recover(p)) { hard = 1; goto done; }
        if (!safe_point(p)) { if (stopped_by_user(p) || !recover(p)) { hard = 1; goto done; } }
        if (winding_down(p) || r->target_reached) break;
        if (slot.coding && r->coding_done) slot.coding = 0;
        InterlockedExchange(&p->yield, 0);
        p->deadline = GetTickCount64() + slot.minutes * 60000ULL;
        task(r, slot.coding ? "coding" : "browsing", slot.minutes);
        result = slot.coding ? coding_slot(r, 0) : browsing_slot(r, slot.minutes, 0);
        if (slot.coding && result == 2) {
            /* No coding left: the rest of this slot is browsing, and counts as browsing. */
            ULONGLONG now = GetTickCount64();
            unsigned left = p->deadline > now ? (unsigned)((p->deadline - now + 59999) / 60000) : 1;
            r->coding_ms += now - started; started = now; slot.coding = 0;
            task(r, "browsing", left);
            result = browsing_slot(r, left, 0);
        }
        p->deadline = 0;
        if (slot.coding) r->coding_ms += GetTickCount64() - started; else r->browsing_ms += GetTickCount64() - started;
        if (result) { r->failures = 0; continue; }
        if (stopped_by_user(p) || !recover(p)) { hard = 1; goto done; }
        if (++r->failures >= 3) { playback_log(p, "SESSION ALERT: three slots in a row failed; ending the day"); break; }
        playback_log(p, "the slot failed; recovering and moving on (%u in a row)", r->failures);
    }

stop:
    /* 3. End: the end look, then Loggy3 Stop and what happens to its window. */
    p->deadline = 0; InterlockedExchange(&p->yield, 0);
    if (winding_down(p)) playback_log(p, "Telegram /stop: ending the day now");
    watching = NULL; p->watch = NULL; p->safe_point = NULL;
    if (!end_look(r) && !recover(p)) { hard = 1; goto done; }
    if (r->recording) {
        task(r, "stopping the Loggy3 recording", 0);
        /* One more try after recovering focus: never leave the recording running by accident.
           (A Stop that did go through reads as "already stopped" the second time.) */
        if (!loggy(r, LOGGY_STOP, 0) && (stopped_by_user(p) || !recover(p) || !playback_wait(p, 2000) || !loggy(r, LOGGY_STOP, 0))) {
            if (stopped_by_user(p)) { hard = 1; goto done; }
            playback_log(p, "SESSION ALERT: Loggy3 did not confirm the stop; check it");
        } else {
            r->recording = 0;
            if (r->plan.after == AFTER_MINIMIZE) loggy(r, LOGGY_MINIMIZE, 0);
            else if (r->plan.after == AFTER_CLOSE) loggy(r, LOGGY_CLOSE, 0);
        }
    }
    summary(r, winding_down(p) ? "stopped from Telegram" : r->target_reached ? "done" : "ended early");
    ok = 1;
done:
    if (hard) playback_log(p, "SESSION ALERT: stopped with F9/F12 or the desktop became unavailable; Loggy3 was left %s",
        r->recording ? "RECORDING" : "as it was");
    watching = NULL; p->watch = NULL; p->safe_point = NULL; p->deadline = 0; p->soft_stop = 0;
    if (r->history_loaded) project_history_free(&r->history);
    free(r);
    return ok;
}
