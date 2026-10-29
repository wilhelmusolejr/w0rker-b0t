#ifndef LOGGY3_H
#define LOGGY3_H
#include "playback.h"
#include "profile.h"
#include "human_motion.h"
/* Loggy3, the screen recorder: show / maximize / minimize / close its window, read whether it is
   recording, and press its Start or Stop button. Ported from experiments/loggy3-window. Everything
   is found through Windows accessibility (the taskbar icon, the window's title-bar buttons, the
   recording button and timer in its web UI) and pressed with the profile's mouse hand. */
typedef enum {
    LOGGY_STATUS, LOGGY_SHOW, LOGGY_MINIMIZE, LOGGY_CLOSE, LOGGY_RECORD, LOGGY_STOP, LOGGY_DEMO, LOGGY_ACTION_COUNT
} LoggyAction;
typedef enum { LOGGY_UNKNOWN, LOGGY_BUSY, LOGGY_STOPPED, LOGGY_RECORDING } LoggyState;
#define LOGGY_MAX_BUTTONS 4
/* One button whose class list has `btn-record` (session-history rows never do). */
typedef struct { wchar_t name[64]; int enabled, offscreen, invoke; RECT rect; } LoggyButton;
typedef struct {
    LoggyButton buttons[LOGGY_MAX_BUTTONS];
    unsigned button_count; /* may exceed LOGGY_MAX_BUTTONS; only the first ones are kept */
    wchar_t timer[16];     /* "--:--:--" while stopped, "0:01:23" while recording */
} LoggySnapshot;
typedef struct {
    LoggyState state, desired;
    int press;           /* 1: press the recording button once */
    wchar_t expected[64]; /* its label right before the press */
    char reason[160];
} LoggyPlan;

const char *loggy_action_name(LoggyAction action);
int loggy_action_from_name(const wchar_t *name, LoggyAction *action);
const char *loggy_state_name(LoggyState state);
/* "Loggy3" or "Loggy3 <anything>", not the recorder's helper window. */
int loggy_title_matches(const wchar_t *title);
/* Whether a space-separated class list contains token. */
int loggy_class_has(const wchar_t *classes, const wchar_t *token);
/* The state shown by the one visible recording button; *button is its index. */
LoggyState loggy_recording_state(const LoggySnapshot *snapshot, unsigned *button);
/* What a status / record / stop request should do. 0 with a reason when it must not act. */
int loggy_plan(LoggyAction action, const LoggySnapshot *snapshot, LoggyPlan *plan);

/* One day on the Sessions page: "Thursday, October 29, 2026 (UTC): 10h 17m 27s, 26 sessions, ...".
   Days are UTC, and the totals cover recordings from all computers on the account. */
typedef struct { unsigned year, month, day, seconds, sessions; } LoggyDay;
int loggy_parse_day(const wchar_t *name, LoggyDay *day);
/* "00:12:34" or "1:02:03" in seconds; -1 for "--:--:--" or anything else. */
long loggy_timer_seconds(const wchar_t *timer);
/* Today's (UTC) recorded time as the app shows it, plus the current recording. */
typedef struct {
    int found;           /* today's row is on screen (Sessions page, current period) */
    LoggyDay day;
    LoggyState state;
    long timer_seconds;  /* the running recording, or -1 */
} LoggyToday;

/* Live (no input): the single Loggy3 main window, or NULL; *count is how many matched. */
HWND loggy_window(unsigned *count);
int loggy_snapshot(HWND window, LoggySnapshot *snapshot);
/* No input. utc is the date to look up (NULL: now). Returns 0 only when Loggy3 cannot be read. */
int loggy_today(HWND window, const SYSTEMTIME *utc, LoggyToday *today);
/* Seconds recorded today: the day row counts finished sessions only (checked live 2026-10-29),
   so the running recording is added on top. -1 when today's row is not on screen. */
long loggy_today_seconds(const LoggyToday *today);

typedef struct LoggyJob {
    LoggyAction action;
    const Profile *profile; /* the mouse hand; may be NULL */
    int dry_run, hover_only, minimize_after;
    int sessions_page;  /* record/stop: show the Sessions page first if today's row is not on screen */
    DWORD hold_ms, hover_ms;
    uint32_t seed;
    LoggyState result; /* filled in: the recording state at the end */
} LoggyJob;
int loggy_job(Playback *p, LoggyJob *job);
/* Bring Loggy3 forward and open its Sessions page when today's row is not on screen. */
int loggy_open_sessions(Playback *p, MotionRng *rng);
#endif
