#include "typing_pace.h"
#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

int main(void) {
    TypingPace a = {0}, b = {0}, other = {0};
    const wchar_t sample[] = L"def lookup(name):\n    return name.strip(), None\n\n";
    unsigned i, rests = 0, changed = 0;
    unsigned min_delay = 10000, max_delay = 0;
    TypingDelay delay;
    CHECK(typing_speed_delay(185, 185, 0) == 185);
    CHECK(typing_speed_delay(185, 185, 1) == 429);
    CHECK(typing_speed_delay(185, 185, 4) == 261);
    CHECK(typing_speed_delay(185, 185, 5) == 231);
    CHECK(typing_speed_delay(185, 185, 10) == 146);
    CHECK(typing_pace_next(&a, L'x').delay_ms == 0);
    typing_pace_configure(&a, 80, 200, 600, 1800, 42);
    typing_pace_breaks(&a, 16, 32, 4000, 10000);
    b = a;
    typing_pace_configure(&other, 80, 200, 600, 1800, 43);
    typing_pace_breaks(&other, 16, 32, 4000, 10000);
    for (i = 0; i < 1000; ++i) {
        wchar_t ch = sample[i % (sizeof(sample)/sizeof(sample[0])-1)];
        TypingDelay identical = typing_pace_next(&b, ch);
        TypingDelay different = typing_pace_next(&other, ch);
        delay = typing_pace_next(&a, ch);
        CHECK(delay.delay_ms == identical.delay_ms && delay.break_ms == identical.break_ms);
        changed += delay.delay_ms != different.delay_ms || delay.break_ms != different.break_ms;
        CHECK(ch == L'\n' ? delay.delay_ms >= 600 && delay.delay_ms <= 1800 :
                            delay.delay_ms >= 80 && delay.delay_ms <= 200);
        if (ch != L'\n') {
            if (delay.delay_ms < min_delay) min_delay = delay.delay_ms;
            if (delay.delay_ms > max_delay) max_delay = delay.delay_ms;
        }
        if (delay.break_ms) {
            CHECK(delay.break_ms >= 4000 && delay.break_ms <= 10000);
            CHECK(ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L',' || ch == L';');
            ++rests;
        }
    }
    CHECK(changed > 0);
    CHECK(rests > 10 && rests < 60);
    CHECK(min_delay < max_delay);

    memset(&a, 0, sizeof(a));
    typing_pace_configure(&a, 10, 10, 20, 20, 7);
    typing_pace_breaks(&a, 1, 1, 50, 50);
    delay = typing_pace_next(&a, L'a');
    CHECK(delay.delay_ms == 10 && delay.break_ms == 0);
    /* Reaching the interval inside an identifier waits for a boundary. */
    CHECK(typing_pace_next(&a, L'b').break_ms == 0);
    delay = typing_pace_next(&a, L' ');
    CHECK(delay.delay_ms == 10 && delay.break_ms == 50);
    delay = typing_pace_next(&a, L'\n');
    CHECK(delay.delay_ms == 20 && delay.break_ms == 50);
    typing_pace_breaks(&a, 0, 0, 0, 0);
    CHECK(typing_pace_next(&a, L'\n').break_ms == 0);

    /* Profile state survives TYPEFILE chunks and ignores temporarily disabled setup text. */
    typing_pace_configure(&a, 80, 200, 600, 1800, 42);
    typing_pace_breaks(&a, 2, 4, 100, 200);
    b = a;
    a.enabled = 0;
    CHECK(typing_pace_next(&a, L'X').delay_ms == 0);
    a.enabled = 1;
    for (i = 0; i < 100; ++i) {
        wchar_t ch = sample[i % (sizeof(sample)/sizeof(sample[0])-1)];
        TypingDelay identical = typing_pace_next(&b, ch);
        delay = typing_pace_next(&a, ch);
        CHECK(delay.delay_ms == identical.delay_ms && delay.break_ms == identical.break_ms);
    }
    printf("Typing presentation pacing: %d checks, %d failures; no input or sleep\n", checks, failures);
    return failures ? 1 : 0;
}
