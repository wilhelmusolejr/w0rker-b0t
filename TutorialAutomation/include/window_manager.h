#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H
#include "playback.h"
void window_enable_dpi(void);
int window_desktop_ready(void);
int window_target_foreground(HWND target);
int window_activate(Playback *p, const wchar_t *title);
int window_activate_handle(Playback *p, HWND window);
int window_process_is(HWND window, const wchar_t *expected);
int window_point(Playback *p, int x, int y, POINT *point);
int window_point_visible(POINT point);
#endif
