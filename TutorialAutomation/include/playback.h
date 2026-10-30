#ifndef PLAYBACK_H
#define PLAYBACK_H
#include "script_engine.h"
#include "typing_pace.h"
#include "action_pace.h"
#include "human_motion.h"
#include <stdio.h>
struct ReferenceConfig;
struct BrowseSession;
struct CodingProject;
struct SideAppJob;
struct LoggyJob;
typedef struct Playback {
    volatile LONG stopped;
    volatile LONG user_stop; /* F9, F12, or Ctrl+C: never restarted automatically */
    int paused, dry_run, client_coords;
    int pause_deferred; /* pause requested while input was held; starts after the release */
    DWORD char_ms, line_ms;
    TypingPace typing_pace;
    /* 0 means no profile is attached; 1-10 follows the profile's typing_speed. */
    unsigned typing_speed;
    /* Percentage of letters first typed as a neighbouring key, then erased and corrected. */
    unsigned typo_percent, typos;
    uint32_t typo_state;
    /* The running profile's hand for visible pointer moves (all zero: plain paths). */
    MotionHand hand;
    ActionPace action_pace;
    struct {
        const wchar_t *url;
        unsigned every_lines, next_line, visits;
        DWORD duration_ms;
        uint32_t seed;
        HWND window;
    } browser;
    HWND target;
    FILE *log;
    unsigned line;
    unsigned pause_generation;
    ULONGLONG paused_ms;
    unsigned char held[256];
    unsigned mouse_held;
    struct ReferenceConfig *reference_config;
    const struct BrowseSession *browse_session;
    struct CodingProject *coding_project;
    const struct SideAppJob *side_app_job;
    struct LoggyJob *loggy_job;
    struct WorkSession *work_session;
    /* Session control. With soft_stop, a Telegram /stop sets wind_down (finish the line, save, run
       the ending) instead of stopping input at once; F9/F12 still stop at once. yield and deadline
       ask the running task to end at its next safe point. */
    int soft_stop;
    volatile LONG wind_down, yield;
    ULONGLONG deadline;
    /* Called from playback_poll every watch_ms while nothing is held (never re-entered). */
    void (*watch)(struct Playback *p);
    DWORD watch_ms;
    ULONGLONG next_watch;
    int in_watch;
    /* Called at a task's safe points (where side-app breaks may start); 0 stops playback. */
    int (*safe_point)(struct Playback *p);
    /* Pause while the person uses the computer, until they leave it alone this long (0: off). */
    unsigned long user_quiet_ms;
    int user_hold;
} Playback;
void playback_log(Playback *p, const char *format, ...);
int playback_poll(Playback *p);
int playback_wait(Playback *p, DWORD milliseconds);
int playback_run(Playback *p, const Script *script, DWORD countdown_ms);
void playback_release(Playback *p);
int playback_send(Playback *p, INPUT *input, UINT count);
int playback_guard(Playback *p);
/* Whether the current task should end at its next safe point (wind down, yield, or deadline). */
int playback_yield(const Playback *p);
#endif
