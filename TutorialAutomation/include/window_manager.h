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
/* A window input can safely go to: it exists, is visible, and is not minimized. */
int window_usable(HWND window);
/* Make p->target a usable foreground window. Windows sometimes hands focus to an invisible window
   (after an app closes, for example); then switch to a visible one: VS Code, Chrome, File
   Explorer, or else the desktop. Returns 0 only when nothing could be brought forward. */
int window_fix_target(Playback *p);
#endif
