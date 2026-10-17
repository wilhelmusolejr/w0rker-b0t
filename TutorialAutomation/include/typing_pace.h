#ifndef TYPING_PACE_H
#define TYPING_PACE_H
#include <stdint.h>
#include <wchar.h>

typedef struct {
    int enabled;
    uint32_t state;
    unsigned char_min_ms, char_max_ms, line_min_ms, line_max_ms;
    unsigned break_after_min, break_after_max, break_min_ms, break_max_ms;
    unsigned remaining;
    wchar_t previous;
} TypingPace;

typedef struct { unsigned delay_ms, break_ms; } TypingDelay;

void typing_pace_configure(TypingPace *pace, unsigned char_min, unsigned char_max,
                           unsigned line_min, unsigned line_max, uint32_t seed);
void typing_pace_breaks(TypingPace *pace, unsigned after_min, unsigned after_max,
                        unsigned pause_min, unsigned pause_max);
TypingDelay typing_pace_next(TypingPace *pace, wchar_t character);
#endif
