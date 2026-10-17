#ifndef CODING_SCHEDULE_H
#define CODING_SCHEDULE_H
#include <stdint.h>
typedef struct {
    uint32_t state;
    unsigned interval_min_ms, interval_max_ms, visit_min_ms, visit_max_ms;
    unsigned next_interval_ms;
    uint64_t coding_ms;
} CodingSchedule;
void coding_schedule_init(CodingSchedule *s, unsigned interval_min, unsigned interval_max,
                          unsigned visit_min, unsigned visit_max, uint32_t seed);
void coding_schedule_add(CodingSchedule *s, uint64_t milliseconds);
int coding_schedule_due(const CodingSchedule *s);
unsigned coding_schedule_visit(CodingSchedule *s);
unsigned coding_schedule_pick(CodingSchedule *s, unsigned minimum, unsigned maximum);
void coding_schedule_reset(CodingSchedule *s);
#endif
