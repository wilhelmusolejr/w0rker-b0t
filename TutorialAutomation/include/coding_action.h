#ifndef CODING_ACTION_H
#define CODING_ACTION_H
#include "playback.h"
int coding_reference(Playback *p, const wchar_t *directory, const wchar_t *source);
int coding_desktop_reference(Playback *p, const wchar_t *directory, const wchar_t *source);
int coding_reference_batch(Playback *p);
int coding_same_text(const wchar_t *expected, const wchar_t *actual);
int coding_workspace_pacing_ready(const wchar_t *settings_json);
#endif
