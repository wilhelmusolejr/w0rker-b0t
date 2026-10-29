#ifndef UI_TARGETS_H
#define UI_TARGETS_H
#include "playback.h"
/* Read locations only. Actual actions use the mouse/keyboard controller. */
int ui_desktop_folder_rect(Playback *playback, const wchar_t *name, RECT *rect);
int ui_control_rect(Playback *playback, int control_type, const wchar_t *name,
                    int contains_name, RECT *rect);
int ui_wait_control_rect(Playback *playback, int control_type, const wchar_t *name,
                         int contains_name, RECT *rect, DWORD timeout_ms);
int ui_find_control_rect(Playback *playback, int control_type, const wchar_t *name, RECT *rect);
/* Like ui_find_control_rect, inside another window (such as the taskbar), matching names that contain `name`. */
int ui_window_control_rect(Playback *playback, HWND window, int control_type, const wchar_t *name, RECT *rect);
/* The same, without requiring the guarded target to be in front (taskbar thumbnails, for example). */
int ui_unguarded_control_rect(Playback *playback, HWND window, int control_type, const wchar_t *name, RECT *rect);
/* A visible entry in File Explorer's navigation pane (the sidebar), read through MSAA because the pane
   exposes no UI Automation items: "Documents", "Documents (pinned)", or "... - Documents (pinned)". */
int ui_explorer_sidebar_rect(Playback *playback, const wchar_t *folder, RECT *rect);
/* Read the VS Code quick-input field/value and require focus inside its widget. */
int ui_wait_vscode_input(Playback *playback, const wchar_t *name, const wchar_t *value, DWORD timeout_ms);
/* Visible organic web links in a browser page: excludes Google's own pages and sponsored results. */
typedef struct { RECT rect; wchar_t url[220], name[120]; } UiWebLink;
int ui_web_links(Playback *playback, UiWebLink *links, unsigned capacity, unsigned *count);
#endif
