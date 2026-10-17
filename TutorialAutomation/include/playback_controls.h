#ifndef PLAYBACK_CONTROLS_H
#define PLAYBACK_CONTROLS_H
#include "playback.h"
int playback_controls_start(Playback *p);
void playback_controls_stop(void);

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
