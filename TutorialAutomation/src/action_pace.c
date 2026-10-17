#include "action_pace.h"

unsigned action_pace_pick(ActionPace *p, unsigned minimum, unsigned maximum) {
    uint32_t value = p->state ? p->state : UINT32_C(0x9e3779b9);
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    p->state = value;
    return minimum + value % (maximum - minimum + 1);
}
void action_pace_configure(ActionPace *p, unsigned pause_min, unsigned pause_max,
                           unsigned move_min, unsigned move_max, uint32_t seed) {
    p->enabled = 1; p->state = seed;
    p->pause_min_ms = pause_min; p->pause_max_ms = pause_max;
    p->move_min_ms = move_min; p->move_max_ms = move_max;
}
unsigned action_pause_next(ActionPace *p) {
    return p->enabled ? action_pace_pick(p, p->pause_min_ms, p->pause_max_ms) : 0;
}
unsigned action_move_next(ActionPace *p) {
    return p->enabled ? action_pace_pick(p, p->move_min_ms, p->move_max_ms) : 600;
}
double action_curve_next(ActionPace *p) {
    return p->enabled ? ((int)action_pace_pick(p, 0, 160) - 80) / 1000.0 : 0;
}
int action_point_next(ActionPace *p, const RECT *rect, POINT *point) {
    LONG width = rect->right - rect->left, height = rect->bottom - rect->top;
    LONG span_x, span_y;
    if (width < 4 || height < 4) return 0;
    /* Keep click variation close to the center, inside the actual control. */
    span_x = width / 8; if (span_x > 12) span_x = 12;
    span_y = height / 8; if (span_y > 6) span_y = 6;
    point->x = rect->left + width / 2;
    point->y = rect->top + height / 2;
    if (p->enabled) {
        point->x += (LONG)action_pace_pick(p, 0, (unsigned)(2 * span_x)) - span_x;
        point->y += (LONG)action_pace_pick(p, 0, (unsigned)(2 * span_y)) - span_y;
    }
    return PtInRect(rect, *point);
}
uint32_t action_recording_seed(void) {
    LARGE_INTEGER ticks = {0};
    uint32_t seed;
    QueryPerformanceCounter(&ticks);
    seed = ((uint32_t)ticks.LowPart ^ GetCurrentProcessId() ^ (uint32_t)GetTickCount64()) & INT32_MAX;
    return seed ? seed : 1;
}
