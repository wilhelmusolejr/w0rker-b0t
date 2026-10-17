#ifndef KEYBOARD_H
#define KEYBOARD_H
#include "playback.h"
int keyboard_key(Playback *p, WORD key, int down);
int keyboard_press(Playback *p, WORD key);
int keyboard_shortcut(Playback *p, const WORD *keys, unsigned count);
int keyboard_type(Playback *p, const wchar_t *text);
/* Types code into VS Code. `text` lies inside `document`, which is read back to the start of the
   current line so a closing bracket's indentation can be restored after VS Code re-indents it. */
int keyboard_type_code(Playback *p, const wchar_t *text, const wchar_t *document);
/* Stop only between complete characters, retaining the ordinary typing profile.
   With `document`, this types code as keyboard_type_code does. */
int keyboard_type_slice(Playback *p, const wchar_t *text, const wchar_t *document, DWORD budget_ms,
                        size_t *consumed);
#endif
