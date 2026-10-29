#ifndef CODING_STEPS_H
#define CODING_STEPS_H
#include "playback.h"
/* VS Code and Explorer steps from coding_action.c, shared with the project typer. */
int coding_step_show_desktop(Playback *p);
int coding_step_wait_window(Playback *p, const wchar_t *class_name, const wchar_t *process,
                            const wchar_t *title, HWND exact);
/* Win+key, from any foreground window. */
int coding_step_win_key(Playback *p, WORD key);
/* From the focused Explorer window: address bar, "cmd", and wait for the new Command Prompt. */
int coding_step_console_here(Playback *p);
/* Ensure the workspace types Plain Text literally (no auto-indent, auto-close, or suggestions). */
int coding_step_workspace(Playback *p, const wchar_t *directory);
/* Ctrl+N, then Save As with the exact absolute path (its folder must already exist). */
int coding_step_create_file(Playback *p, const wchar_t *path);
int coding_step_open_file(Playback *p, const wchar_t *path, HWND editor);
int coding_step_select_tab(Playback *p, const wchar_t *name);
int coding_step_plain_text(Playback *p);
int coding_step_lf(Playback *p);
int coding_step_revert(Playback *p);
int coding_step_wait_saved(Playback *p, const wchar_t *path, const wchar_t *expected);
/* Ctrl+S, then wait until the saved file is a prefix of `expected` (of length *required when given). */
int coding_step_saved_offset(Playback *p, const wchar_t *path, const wchar_t *expected,
                             const size_t *required, size_t *offset);
#endif
