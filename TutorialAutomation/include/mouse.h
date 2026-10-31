#ifndef MOUSE_H
#define MOUSE_H
#include "playback.h"
#include "human_motion.h"
double mouse_ease(double fraction);
int mouse_move(Playback *p, int x, int y, DWORD duration, int curve);
int mouse_move_arc(Playback *p, int x, int y, DWORD duration, double curvature);
int mouse_button(Playback *p, int button, int down);
int mouse_click(Playback *p, int button, int twice);
int mouse_scroll(Playback *p, int notches);
/* Follow a planned path in screen coordinates; pauses and stop keys are honored between steps. */
int mouse_follow(Playback *p, const MotionPath *path);
/* Move to a screen point the way the profile's hand does (p->hand): a wobbling curve that
   sometimes overshoots and corrects. Already there (within 2 px) does nothing. */
int mouse_move_human(Playback *p, MotionRng *rng, POINT target, int purpose, double target_width);
/* Aim at a control with the profile's hand, scaling the configured time by travel distance. */
int mouse_move_control(Playback *p, MotionRng *rng, POINT target, double target_width, unsigned reference_ms);
/* One wheel event of several notches, with no added pacing. */
int mouse_wheel(Playback *p, int notches);
/* Move a few pixels while scrolling without adding a separate movement delay. */
int mouse_scroll_nudge(Playback *p, MotionRng *rng, POINT anchor, const RECT *bounds);
int mouse_drag(Playback *p, int x1, int y1, int x2, int y2, DWORD duration, int curve);
#endif
