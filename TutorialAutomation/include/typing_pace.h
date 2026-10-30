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
/* Rescale text-entry delays so speed 5 is 52 WPM (five characters per word).
   Speed 0 leaves legacy, non-profile scripts alone. */
unsigned typing_speed_delay(unsigned delay_ms, unsigned base_char_ms, unsigned speed);
/* A neighbouring QWERTY key for a letter, keeping its case (0 for anything that is not a-z/A-Z). */
wchar_t typing_typo_neighbor(wchar_t character, uint32_t roll);
#endif
