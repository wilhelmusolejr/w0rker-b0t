#ifndef MOUSE_H
#define MOUSE_H
#include "playback.h"
double mouse_ease(double fraction);
int mouse_move(Playback *p, int x, int y, DWORD duration, int curve);
int mouse_move_arc(Playback *p, int x, int y, DWORD duration, double curvature);
int mouse_button(Playback *p, int button, int down);
int mouse_click(Playback *p, int button, int twice);
int mouse_scroll(Playback *p, int notches);
int mouse_drag(Playback *p, int x1, int y1, int x2, int y2, DWORD duration, int curve);
#endif
