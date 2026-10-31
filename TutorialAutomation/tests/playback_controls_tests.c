#include "playback_controls.h"
#include "window_manager.h"
#include "work_session.h"
#include <stdio.h>

/* Exercise the hook callback without injecting any keyboard events. */
LRESULT CALLBACK playback_emergency_hook(int code, WPARAM message, LPARAM data);
static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

int main(void) {
    Playback p = {0};
    KBDLLHOOKSTRUCT key = {0};
    if (!window_desktop_ready()) {
        puts("SKIP: no interactive desktop for playback control installation"); return 77;
    }
    /* Regression: startup must succeed without registering reserved F12. */
    if (!playback_controls_start(&p)) return 1;
    key.vkCode = VK_F12;
    CHECK(playback_emergency_hook(HC_ACTION, WM_KEYUP, (LPARAM)&key) == 1);
    CHECK(!p.stopped);
    CHECK(playback_emergency_hook(HC_ACTION, WM_KEYDOWN, (LPARAM)&key) == 1);
    CHECK(p.stopped);
    InterlockedExchange(&p.stopped, 0);
    key.vkCode = L'A';
    playback_emergency_hook(HC_ACTION, WM_KEYDOWN, (LPARAM)&key);
    CHECK(!p.stopped);
    key.vkCode = VK_F12;
    CHECK(playback_emergency_hook(HC_ACTION, WM_SYSKEYDOWN, (LPARAM)&key) == 1);
    CHECK(p.stopped);
    InterlockedExchange(&p.stopped, 0);
    playback_emergency_hook(-1, WM_KEYDOWN, 0);
    CHECK(!p.stopped);
    playback_controls_stop();
    CHECK(playback_controls_start(&p));
    playback_controls_stop();
    /* Regression: F8 while a key is held must defer the pause, not stop playback. */
    {
        Playback held = {0};
        held.held[VK_RETURN] = 1; held.paused = 1;
        CHECK(playback_poll(&held));
        CHECK(!held.stopped && !held.user_stop);
        CHECK(held.paused && held.pause_deferred);
        held.paused = 0;
        CHECK(playback_poll(&held));
        CHECK(!held.pause_deferred);
    }
    /* End-only live playback installs and removes controls without generating input. */
    {
        Command end = {0}; Script script = {&end, 1};
        end.kind = CMD_END; end.line = 1;
        CHECK(playback_run(&p, &script, 0));
        {
            SessionConfig config = {0};
            WorkSession session = {0};
            Playback running = {0};
            session.config = &config;
            running.work_session = &session;
            CHECK(playback_run(&running, &script, 0));
            CHECK(running.user_quiet_ms == 0);
            config.pause_on_user_input = 1;
            CHECK(playback_run(&running, &script, 0));
            CHECK(running.user_quiet_ms == 20000);
        }
    }
    printf("Playback control regression: %d failures; no input generated\n", failures);
    return failures ? 1 : 0;
}
