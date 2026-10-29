#include "typing_pace.h"
#include <string.h>

/* A recorded seed gives each presentation a repeatable sequence of delays. */
static unsigned between(TypingPace *p, unsigned minimum, unsigned maximum) {
    uint32_t value = p->state ? p->state : UINT32_C(0x9e3779b9);
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    p->state = value;
    return minimum + value % (maximum - minimum + 1);
}

void typing_pace_configure(TypingPace *p, unsigned char_min, unsigned char_max,
                           unsigned line_min, unsigned line_max, uint32_t seed) {
    p->enabled = 1;
    p->state = seed;
    p->char_min_ms = char_min; p->char_max_ms = char_max;
    p->line_min_ms = line_min; p->line_max_ms = line_max;
    p->previous = 0;
    p->remaining = p->break_after_min ? between(p, p->break_after_min, p->break_after_max) : 0;
}

void typing_pace_breaks(TypingPace *p, unsigned after_min, unsigned after_max,
                        unsigned pause_min, unsigned pause_max) {
    p->break_after_min = after_min; p->break_after_max = after_max;
    p->break_min_ms = pause_min; p->break_max_ms = pause_max;
    p->remaining = after_min ? between(p, after_min, after_max) : 0;
}

TypingDelay typing_pace_next(TypingPace *p, wchar_t character) {
    TypingDelay delay = {0};
    int boundary;
    if (!p->enabled) return delay;
    delay.delay_ms = character == L'\n' ? between(p, p->line_min_ms, p->line_max_ms) :
                                         between(p, p->char_min_ms, p->char_max_ms);
    boundary = character == L'\n' || character == L',' || character == L';' ||
        ((character == L' ' || character == L'\t') && p->previous &&
         p->previous != L' ' && p->previous != L'\t' && p->previous != L'\n');
    if (p->break_after_min) {
        if (p->remaining) --p->remaining;
        if (!p->remaining && boundary) {
            delay.break_ms = between(p, p->break_min_ms, p->break_max_ms);
            p->remaining = between(p, p->break_after_min, p->break_after_max);
        }
    }
    p->previous = character;
    return delay;
}
wchar_t typing_typo_neighbor(wchar_t ch, uint32_t roll) {
    static const char *const near_keys[26] = {
        "qwsz", "vghn", "xdfv", "serfcx", "wsdr", "drtgvc", "ftyhbv", "gyujnb", "ujko", "huikmn",
        "jiolm", "kop", "njk", "bhjm", "iklp", "ol", "wa", "edft", "awedxz", "rfgy", "yhji",
        "cfgb", "qase", "zsdc", "tghu", "asx"
    };
    int upper = ch >= L'A' && ch <= L'Z';
    const char *keys;
    wchar_t pick;
    if (!upper && !(ch >= L'a' && ch <= L'z')) return 0;
    keys = near_keys[(upper ? ch - L'A' : ch - L'a')];
    pick = (wchar_t)keys[roll % strlen(keys)];
    return upper ? (wchar_t)(pick - L'a' + L'A') : pick;
}
