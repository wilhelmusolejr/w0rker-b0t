#include "presentation.h"
#include "mouse.h"

int presentation_rest(Playback *p, const char *step) {
    unsigned delay = action_pause_next(&p->action_pace);
    if (delay) playback_log(p, "%s; presentation pause %.1f seconds", step, delay / 1000.0);
    return playback_wait(p, delay);
}
int presentation_hover(Playback *p, const RECT *rect) {
    POINT point;
    MotionRng rng;
    unsigned duration;
    int coords = p->client_coords, ok = 0;
    if (!action_point_next(&p->action_pace, rect, &point)) {
        playback_log(p, "click target is too small"); return 0;
    }
    p->client_coords = 0;
    playback_log(p, "moving to control at %ld,%ld", point.x, point.y);
    duration = action_move_next(&p->action_pace);
    motion_seed(&rng, p->action_pace.enabled ?
        action_pace_pick(&p->action_pace, 1, UINT32_C(0x7fffffff)) : action_recording_seed());
    if (!mouse_move_control(p, &rng, point,
        rect->right - rect->left < rect->bottom - rect->top ?
        rect->right - rect->left : rect->bottom - rect->top, duration) || !playback_wait(p,
        p->action_pace.enabled ? action_pace_pick(&p->action_pace, 120, 350) : 0)) goto done;
    ok = 1;
done:
    p->client_coords = coords;
    return ok;
}
int presentation_click(Playback *p, const RECT *rect, int twice) {
    int coords = p->client_coords, ok;
    if (!presentation_hover(p, rect)) return 0;
    p->client_coords = 0;
    ok = mouse_click(p, 0, twice);
    p->client_coords = coords;
    return ok;
}
