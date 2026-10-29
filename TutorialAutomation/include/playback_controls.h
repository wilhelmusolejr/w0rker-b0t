#ifndef PLAYBACK_CONTROLS_H
#define PLAYBACK_CONTROLS_H
#include "playback.h"
int playback_controls_start(Playback *p);
void playback_controls_stop(void);

/* Someone using the computer while playback runs. Every input playback sends carries this tag
   (dwExtraInfo), and the pointer positions it sets are remembered, so a background listener can
   tell the person's own mouse and keyboard apart from playback's. */
#define PLAYBACK_INPUT_TAG ((ULONG_PTR)0x54414E31u)
void playback_note_cursor(int x, int y);
/* Milliseconds since the person last moved the mouse, clicked, scrolled, or pressed a key
   (ULONG_MAX when they have not since playback started). */
unsigned long playback_user_idle_ms(void);

/* Remote control from the monitor: named per-session events, signalled with SetEvent. */
#define PLAYBACK_REMOTE_PAUSE_EVENT  L"Local\\TutorialAutomation.Pause"
#define PLAYBACK_REMOTE_RESUME_EVENT L"Local\\TutorialAutomation.Resume"
#define PLAYBACK_REMOTE_STOP_EVENT   L"Local\\TutorialAutomation.Stop"
enum { PLAYBACK_REMOTE_NONE, PLAYBACK_REMOTE_PAUSE, PLAYBACK_REMOTE_RESUME, PLAYBACK_REMOTE_STOP };
int playback_remote_open(void);
void playback_remote_close(void);
/* Returns and consumes one pending remote command, or PLAYBACK_REMOTE_NONE. */
int playback_remote_take(void);
#endif
