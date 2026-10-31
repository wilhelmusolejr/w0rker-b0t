#ifndef WORK_SESSION_H
#define WORK_SESSION_H
#include "playback.h"
#include "profile.h"
#include "human_motion.h"
#include "project_manifest.h"
#include "browse_catalog.h"
#include "side_apps.h"
#include "flow_config.h"
/* One accessibility-driven work session. A supplied duration is measured locally; otherwise
   Loggy3's daily total controls the target while readable. Work continues on a local countdown
   if the recorder becomes unavailable. Settings are in profiles/session.json. */
typedef enum { START_DESKTOP, START_VSCODE, START_BROWSER, START_APP, START_LOOK_COUNT } SessionStartLook;
typedef enum { END_DIRECT, END_CLOSE_ALL, END_CLOSE_SOME, END_LOOK_COUNT } SessionEndLook;
typedef enum { CLOSE_BUTTON, CLOSE_ALT_F4, CLOSE_TASKBAR, CLOSE_METHOD_COUNT } SessionCloseMethod;
typedef enum { AFTER_FRONT, AFTER_MINIMIZE, AFTER_CLOSE, AFTER_STOP_COUNT } SessionAfterStop;
typedef enum { BROWSER_MINIMIZE, BROWSER_CLOSE, BROWSER_LEAVE, BROWSER_AFTER_COUNT } SessionBrowserAfter;

typedef struct {
    int pause_on_user_input; /* false: keep working through manual input; stop keys still apply */
    unsigned target_minutes[2], coding_minutes[2], browsing_minutes[2], break_minutes[2];
    unsigned first_coding_percent, loggy_minimize_percent, start_app_close_percent;
    unsigned ending_reserve_seconds, check_seconds, restart_wait_seconds, weight_jitter_percent;
    unsigned idle_percent, idle_minutes[2], idle_check_minutes[2]; /* time away from the desk */
    unsigned start_looks[START_LOOK_COUNT], end_looks[END_LOOK_COUNT], close_methods[CLOSE_METHOD_COUNT];
    unsigned after_stop[AFTER_STOP_COUNT], browser_after[BROWSER_AFTER_COUNT];
} SessionConfig;
int session_config_load(const wchar_t *path, SessionConfig *config, char *error, size_t capacity);
int session_config_parse(const wchar_t *text, SessionConfig *config, char *error, size_t capacity);

/* The session's choices, drawn up front from the seed. Slots extend past the target so the
   selected clock, rather than the last planned slot, ends the session. */
#define SESSION_MAX_SLOTS 96
#define SESSION_MAX_IDLES 96
typedef struct { int coding; unsigned minutes; } SessionSlot;
/* An idle away from the desk: `at` seconds of active work after preparation, `seconds` long. */
typedef struct { unsigned at, seconds; } SessionIdle;
typedef struct {
    unsigned target_seconds;
    SessionStartLook start;
    SideApp start_app;
    int close_start_app, minimize_loggy;
    SessionEndLook end;
    unsigned close_count; /* END_CLOSE_SOME: 1 or 2 */
    SessionAfterStop after;
    unsigned slot_count;
    SessionSlot slots[SESSION_MAX_SLOTS];
    unsigned idle_count;
    SessionIdle idles[SESSION_MAX_IDLES];
} SessionPlan;
/* randomness: the profile's randomness_score (0-100); it scales the idle share by 0.8-1.2. */
void session_plan(const SessionConfig *config, uint32_t seed, unsigned recorded_seconds, unsigned randomness, SessionPlan *plan);
/* Timeline from now; recorded_seconds is today's known total in daily-target mode. */
void session_print_plan(const SessionConfig *config, const SessionPlan *plan, const Profile *profile,
                        unsigned recorded_seconds, int duration_mode);

/* Pure clock operations accept caller-supplied monotonic and paused milliseconds for tests. */
typedef struct {
    unsigned target_seconds;
    int duration_mode, fallback, has_recorded;
    ULONGLONG started_ms, paused_at_start_ms, fallback_due_ms, last_seen_elapsed_ms;
    long last_recorded_seconds;
} SessionClock;
void session_clock_start(SessionClock *clock, unsigned target_seconds, int duration_mode,
                         ULONGLONG now_ms, ULONGLONG paused_ms);
ULONGLONG session_clock_elapsed_ms(const SessionClock *clock, ULONGLONG now_ms, ULONGLONG paused_ms);
void session_clock_observe(SessionClock *clock, long recorded_seconds, ULONGLONG now_ms, ULONGLONG paused_ms);
void session_clock_fallback(SessionClock *clock);
ULONGLONG session_clock_remaining_ms(const SessionClock *clock, long recorded_seconds,
                                     ULONGLONG now_ms, ULONGLONG paused_ms);
int session_clock_near(const SessionClock *clock, long recorded_seconds, unsigned reserve_seconds,
                       ULONGLONG now_ms, ULONGLONG paused_ms);
const char *session_start_look_name(SessionStartLook look);
const char *session_end_look_name(SessionEndLook look);
const char *session_close_method_name(SessionCloseMethod method);
const char *session_after_stop_name(SessionAfterStop after);
/* Pick by weight; weights of 0 are never picked. All-zero picks 0. */
unsigned session_pick(const unsigned *weights, unsigned count, MotionRng *rng);

typedef struct WorkSession {
    const Profile *profile;
    const SessionConfig *config;
    const ProgrammingConfig *programming;
    const ProjectManifest *manifest;
    const BrowseCatalog *catalog;
    const SideAppsConfig *side_apps; /* breaks; minutes are taken from session.json */
    uint32_t seed;
    unsigned duration_minutes; /* 0: Loggy3 daily target; otherwise requested local session budget */
} WorkSession;
int work_session_run(Playback *p, WorkSession *session);
typedef struct FlowJob {
    const FlowConfig *config;
    WorkSession *resources;
    const SideAppsConfig *apps;
    const wchar_t *progress_path;
    uint32_t seed;
    int restart;
} FlowJob;
int flow_run(Playback *p, FlowJob *job);
#endif
