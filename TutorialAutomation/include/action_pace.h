#ifndef ACTION_PACE_H
#define ACTION_PACE_H
#include <windows.h>
#include <stdint.h>
typedef struct {
    int enabled;
    uint32_t state;
    unsigned pause_min_ms, pause_max_ms, move_min_ms, move_max_ms;
} ActionPace;
void action_pace_configure(ActionPace *pace, unsigned pause_min, unsigned pause_max,
                           unsigned move_min, unsigned move_max, uint32_t seed);
unsigned action_pace_pick(ActionPace *pace, unsigned minimum, unsigned maximum);
unsigned action_pause_next(ActionPace *pace);
unsigned action_move_next(ActionPace *pace);
double action_curve_next(ActionPace *pace);
int action_point_next(ActionPace *pace, const RECT *rect, POINT *point);
uint32_t action_recording_seed(void);
#endif
