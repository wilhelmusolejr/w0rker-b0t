#ifndef VSCODE_EDITOR_H
#define VSCODE_EDITOR_H
#include "playback.h"
int vscode_select_plain_text(Playback *playback);
int vscode_select_lf(Playback *playback);
/* File: Revert File: the editor goes back to what is saved on disk (unsaved typing is dropped). */
int vscode_revert_file(Playback *playback);
#endif
