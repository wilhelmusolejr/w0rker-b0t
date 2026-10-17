#ifndef PLAYBACK_H
#define PLAYBACK_H
#include "script_engine.h"
#include "typing_pace.h"
#include "action_pace.h"
#include <stdio.h>
struct ReferenceConfig;
typedef struct {
    volatile LONG stopped;
    volatile LONG user_stop; /* F9, F12, or Ctrl+C: never restarted automatically */
    int paused, dry_run, client_coords;
    int pause_deferred; /* pause requested while input was held; starts after the release */
    DWORD char_ms, line_ms;
    TypingPace typing_pace;
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
} Playback;
void playback_log(Playback *p, const char *format, ...);
int playback_poll(Playback *p);
int playback_wait(Playback *p, DWORD milliseconds);
int playback_run(Playback *p, const Script *script, DWORD countdown_ms);
void playback_release(Playback *p);
int playback_send(Playback *p, INPUT *input, UINT count);
int playback_guard(Playback *p);
#endif
