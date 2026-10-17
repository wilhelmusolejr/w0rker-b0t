#include "presentation.h"
#include "mouse.h"

int presentation_rest(Playback *p, const char *step) {
    unsigned delay = action_pause_next(&p->action_pace);
    if (delay) playback_log(p, "%s; presentation pause %.1f seconds", step, delay / 1000.0);
    return playback_wait(p, delay);
}
int presentation_hover(Playback *p, const RECT *rect) {
    POINT point;
    int coords = p->client_coords, ok = 0;
    if (!action_point_next(&p->action_pace, rect, &point)) {
        playback_log(p, "click target is too small"); return 0;
    }
    p->client_coords = 0;
    playback_log(p, "moving to control at %ld,%ld", point.x, point.y);
    if (!mouse_move_arc(p, point.x, point.y, action_move_next(&p->action_pace),
        action_curve_next(&p->action_pace)) || !playback_wait(p,
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
