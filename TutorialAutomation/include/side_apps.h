#ifndef SIDE_APPS_H
#define SIDE_APPS_H
#include "playback.h"
#include "profile.h"
#include "human_motion.h"
/* Side-app breaks during the main task: now and then, open another everyday app for a while
   (Notepad, File Explorer, Calculator, an image viewer), then return to where the task left off.
   Settings in profiles/apps.json. */
typedef enum { APP_NOTEPAD, APP_EXPLORER, APP_CALCULATOR, APP_IMAGES, APP_COUNT } SideApp;
#define SIDE_APPS_MAX_ITEMS 32
typedef struct SideAppsConfig {
    unsigned chance_percent, check_minutes[2], minutes[2], leave_open_percent;
    unsigned weights[APP_COUNT];
    wchar_t notes_folder[MAX_PATH];
    wchar_t *notes[SIDE_APPS_MAX_ITEMS], *folders[SIDE_APPS_MAX_ITEMS], *image_folders[SIDE_APPS_MAX_ITEMS];
    size_t note_count, folder_count, image_folder_count;
} SideAppsConfig;
int side_apps_load(const wchar_t *path, SideAppsConfig *config, char *error, size_t capacity);
int side_apps_parse(const wchar_t *text, SideAppsConfig *config, char *error, size_t capacity);
void side_apps_free(SideAppsConfig *config);
const char *side_app_name(SideApp app);
int side_app_from_name(const wchar_t *name, SideApp *app);
SideApp side_apps_pick(const SideAppsConfig *config, MotionRng *rng);
/* A note from the templates, with {interest} and {hobby} filled from the profile. */
int side_apps_note(const SideAppsConfig *config, const Profile *profile, MotionRng *rng, wchar_t *out, size_t capacity);
/* Desktop, Documents, Downloads, Pictures, Music, Videos, Home, or Projects (the profile's project_path). */
int side_apps_folder(const wchar_t *token, const Profile *profile, wchar_t out[MAX_PATH]);

/* Rolls for a break every check_every_minutes of main-task time. */
typedef struct SideAppsTimer {
    const SideAppsConfig *config;
    const Profile *profile;
    MotionRng rng;
    ULONGLONG last, paused;
    DWORD elapsed_ms, next_check_ms;
    unsigned breaks;
} SideAppsTimer;
void side_apps_timer_init(SideAppsTimer *timer, const SideAppsConfig *config, const Profile *profile,
                          uint32_t seed, const Playback *p);
/* Call between steps of the main task. Sets *took when a break ran; p->target is then the
   window that was in front before the break. Returns 0 only to stop playback. */
int side_apps_tick(Playback *p, SideAppsTimer *timer, int *took);
/* One break: open the app, use it for duration_ms, close or minimize it, and return to the
   window that was in front before. */
int side_apps_run(Playback *p, const SideAppsConfig *config, const Profile *profile, SideApp app,
                  DWORD duration_ms, MotionRng *rng);
/* A single break on its own (tutorial --apps), for trying an app out. */
typedef struct SideAppJob {
    const SideAppsConfig *config;
    const Profile *profile;
    SideApp app;
    DWORD duration_ms;
    uint32_t seed;
} SideAppJob;
int side_apps_job(Playback *p, const SideAppJob *job);

/* Open an app and leave it in front, without using it (a session's start screen). Images opens
   File Explorer. p->target is the app window afterwards. */
int side_apps_open(Playback *p, MotionRng *rng, SideApp app);
/* Shared with other app actions (Loggy3, sessions). Click a taskbar icon by its accessible name (a
   name that contains `button`). TASKBAR_NEW_WINDOW middle-clicks an app that already has windows,
   which opens another; TASKBAR_MENU right-clicks for its menu; TASKBAR_NEXT_WINDOW Ctrl+clicks, which
   switches to the app's next window without thumbnails. Returns 1 clicked, 2 no such icon, 0 stop. */
enum { TASKBAR_LEFT, TASKBAR_NEW_WINDOW, TASKBAR_MENU, TASKBAR_NEXT_WINDOW };
int taskbar_icon_click(Playback *p, MotionRng *rng, const wchar_t *button, int mode);
/* Bring one exact window forward from its taskbar icon, choosing its thumbnail when the app has
   several windows. 1 when it is in front (p->target), 0 otherwise. */
int taskbar_show_window(Playback *p, MotionRng *rng, const wchar_t *icon, HWND window);
/* Click Start, then the app's pinned tile or its search result. Returns 1 launched, 2 the Start
   menu could not be used, 0 stop. */
int start_menu_open(Playback *p, MotionRng *rng, const wchar_t *app);
#endif
