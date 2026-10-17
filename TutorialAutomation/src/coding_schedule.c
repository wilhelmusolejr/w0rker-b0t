#include "coding_schedule.h"
#include <string.h>
#include <stdint.h>
unsigned coding_schedule_pick(CodingSchedule *s, unsigned minimum, unsigned maximum) {
    uint32_t n = s->state;
    n ^= n << 13; n ^= n >> 17; n ^= n << 5; s->state = n;
    return minimum + n % (maximum-minimum+1);
}
void coding_schedule_reset(CodingSchedule *s) {
    s->coding_ms = 0;
    s->next_interval_ms = coding_schedule_pick(s, s->interval_min_ms, s->interval_max_ms);
}
void coding_schedule_init(CodingSchedule *s, unsigned lo, unsigned hi, unsigned vlo, unsigned vhi, uint32_t seed) {
    memset(s, 0, sizeof(*s)); s->state = seed ? seed : 1;
    s->interval_min_ms = lo; s->interval_max_ms = hi; s->visit_min_ms = vlo; s->visit_max_ms = vhi;
    coding_schedule_reset(s);
}
void coding_schedule_add(CodingSchedule *s, uint64_t milliseconds) {
    if (UINT64_MAX-s->coding_ms < milliseconds) s->coding_ms = UINT64_MAX;
    else s->coding_ms += milliseconds;
}
int coding_schedule_due(const CodingSchedule *s) { return s->coding_ms >= s->next_interval_ms; }
unsigned coding_schedule_visit(CodingSchedule *s) { return coding_schedule_pick(s, s->visit_min_ms, s->visit_max_ms); }
