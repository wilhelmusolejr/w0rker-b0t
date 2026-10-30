/* Fake receiver: exercise sequencing and interruption without SendInput or Sleep. */
#include "keyboard.h"
#include <stdio.h>
#include <string.h>
static int checks, failures;
static INPUT events[64];
static DWORD waits[32];
static unsigned used, send_calls, wait_calls, guards, interrupt_at, fail_send_at, held_at_interrupt;
static int switch_after_send, focus_changed;
#define CHECK(test) do { ++checks; if (!(test)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#test); } } while (0)
int playback_guard(Playback *p) { (void)p; ++guards; return !focus_changed; }
int playback_poll(Playback *p) { (void)p; return 1; }
void playback_log(Playback *p, const char *format, ...) { (void)p; (void)format; }
int playback_send(Playback *p, INPUT *input, UINT count) {
    (void)p;
    ++send_calls;
    if (send_calls == fail_send_at || used + count > 64) return 0;
    memcpy(events + used, input, count * sizeof(*input)); used += count;
    if (switch_after_send) focus_changed = 1;
    return 1;
}
int playback_wait(Playback *p, DWORD milliseconds) {
    unsigned i;
    if (wait_calls >= 32) return 0;
    waits[wait_calls++] = milliseconds;
    if (wait_calls != interrupt_at) return 1;
    for (i = 0; i < 256; ++i) held_at_interrupt += p->held[i] != 0;
    return 0;
}
static void reset(Playback *p) {
    memset(p, 0, sizeof(*p)); memset(events, 0, sizeof(events)); memset(waits, 0, sizeof(waits));
    used = send_calls = wait_calls = guards = interrupt_at = fail_send_at = held_at_interrupt = 0;
    switch_after_send = focus_changed = 0;
    action_pace_configure(&p->action_pace, 1400, 3000, 1000, 1800, 42);
}
int main(void) {
    Playback p;
    WORD chord[] = {VK_CONTROL, VK_SHIFT, L'P'};
    unsigned i;
    reset(&p); switch_after_send = 1;
    CHECK(keyboard_press(&p, VK_RETURN));
    CHECK(guards == 1 && send_calls == 2 && used == 2 && wait_calls == 2);
    CHECK(events[0].ki.wVk == VK_RETURN && !(events[0].ki.dwFlags & KEYEVENTF_KEYUP));
    CHECK(events[1].ki.wVk == VK_RETURN && (events[1].ki.dwFlags & KEYEVENTF_KEYUP));
    CHECK(waits[0] >= 70 && waits[0] <= 140 && waits[1] >= 140 && waits[1] <= 320);
    CHECK(!p.held[VK_RETURN]);
    reset(&p); switch_after_send = 1;
    CHECK(keyboard_shortcut(&p, chord, 3));
    CHECK(guards == 1 && send_calls == 2 && used == 6 && wait_calls == 2);
    for (i = 0; i < 3; ++i) {
        CHECK(events[i].ki.wVk == chord[i] && !(events[i].ki.dwFlags & KEYEVENTF_KEYUP));
        CHECK(events[5-i].ki.wVk == chord[i] && (events[5-i].ki.dwFlags & KEYEVENTF_KEYUP));
        CHECK(!p.held[chord[i]]);
    }
    CHECK(waits[0] >= 90 && waits[0] <= 180 && waits[1] >= 180 && waits[1] <= 400);
    reset(&p); interrupt_at = 1;
    CHECK(!keyboard_shortcut(&p, chord, 3));
    CHECK(held_at_interrupt == 3 && send_calls == 1);
    reset(&p); interrupt_at = 1;
    CHECK(!keyboard_press(&p, VK_RETURN));
    CHECK(held_at_interrupt == 1 && p.held[VK_RETURN]);
    reset(&p); fail_send_at = 2;
    CHECK(!keyboard_shortcut(&p, chord, 3));
    CHECK(p.held[VK_CONTROL] && p.held[VK_SHIFT] && p.held[L'P']);
    reset(&p); p.action_pace.enabled = 0;
    CHECK(keyboard_shortcut(&p, chord, 3) && send_calls == 1 && used == 6 && wait_calls == 0);
    reset(&p);
    typing_pace_configure(&p.typing_pace, 140, 300, 900, 2200, 42);
    CHECK(keyboard_type(&p, L"a\r\nb"));
    CHECK(wait_calls == 5 && used == 6);
    CHECK(waits[0] >= 140 && waits[0] <= 300);
    CHECK(waits[1] >= 70 && waits[1] <= 140);
    CHECK(waits[2] >= 140 && waits[2] <= 320);
    CHECK(waits[3] >= 900 && waits[3] <= 2200);
    CHECK(waits[4] >= 140 && waits[4] <= 300);
    CHECK(events[0].ki.wScan == L'a' && events[4].ki.wScan == L'b');
    reset(&p); p.action_pace.enabled = 0; p.typing_speed = 4;
    typing_pace_configure(&p.typing_pace, 185, 185, 600, 600, 42);
    CHECK(keyboard_type(&p, L"ab"));
    CHECK(wait_calls == 2 && waits[0] == 261 && waits[1] == 261);
    { size_t consumed = 99;
      reset(&p); p.action_pace.enabled = 0;
      CHECK(keyboard_type_slice(&p,L"a\r\n\xd83d\xde00",NULL,60000,&consumed));
      CHECK(consumed == 5 && used == 8); /* Never split CRLF or a Unicode surrogate pair. */
      CHECK(events[4].ki.wScan == 0xd83d && events[6].ki.wScan == 0xde00);
      reset(&p); p.action_pace.enabled = 0; interrupt_at = 2;
      CHECK(!keyboard_type_slice(&p,L"abcd",NULL,60000,&consumed));
      CHECK(consumed == 1); /* The last fully completed character is retained on interruption. */
    }
    { /* A closing bracket that starts a line gets its source indentation typed again, since VS Code
         may re-indent it. Brackets after other text on the line are left alone. */
      static const wchar_t doc[] = L"x');\n  }) y}\n}";
      size_t consumed = 0;
      reset(&p); p.action_pace.enabled = 0;
      CHECK(keyboard_type_slice(&p,doc+5,doc,60000,&consumed) && consumed == 9);
      /* "  }" then Home, Shift+Home, two spaces, End; then ") y}", Enter, "}" then Home,
         Shift+Home, space, Backspace, End. */
      CHECK(events[4].ki.wScan == L'}' && events[6].ki.wVk == VK_HOME && events[8].ki.wVk == VK_SHIFT &&
            events[9].ki.wVk == VK_HOME && events[12].ki.wScan == L' ' && events[14].ki.wScan == L' ' &&
            events[16].ki.wVk == VK_END && events[18].ki.wScan == L')');
      CHECK(events[26].ki.wVk == VK_RETURN && events[28].ki.wScan == L'}' && events[36].ki.wScan == L' ' &&
            events[38].ki.wVk == VK_BACK && events[40].ki.wVk == VK_END && used == 42);
    }
    printf("Keyboard pacing: %d checks, %d failures; fake receiver, no input or sleep\n", checks, failures);
    return failures ? 1 : 0;
}
