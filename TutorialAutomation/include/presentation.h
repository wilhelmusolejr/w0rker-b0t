#ifndef PRESENTATION_H
#define PRESENTATION_H
#include "playback.h"
int presentation_rest(Playback *playback, const char *step);
int presentation_hover(Playback *playback, const RECT *rect);
int presentation_click(Playback *playback, const RECT *rect, int twice);
#endif
