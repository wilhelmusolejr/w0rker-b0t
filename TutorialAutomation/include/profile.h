#ifndef PROFILE_H
#define PROFILE_H
#include <stddef.h>
#include <wchar.h>
/* A scenario profile: who the simulated person is and how they behave. Loaded
   from profiles/profiles.json, which acts as a small local database. */
typedef enum { PROFILE_MALE, PROFILE_FEMALE, PROFILE_GENDER_COUNT } ProfileGender;
typedef enum {
    PROFILE_TASK_PROGRAMMING, PROFILE_TASK_BROWSER, PROFILE_TASK_EXCEL, PROFILE_TASK_NOTEPAD,
    PROFILE_TASK_IMAGE_VIEWER, PROFILE_TASK_CAD, PROFILE_TASK_FILES, PROFILE_TASK_COUNT
} ProfileTask;
#define PROFILE_MAX_ITEMS 32
typedef struct {
    unsigned id;
    wchar_t *local_id, *name, *occupation, *computer_name;
    /* Where this person keeps coding projects: "Desktop", "Documents", either followed by
       \subfolders, or an absolute path. NULL when not set. */
    wchar_t *project_path;
    ProfileGender gender;
    ProfileTask main_task;
    ProfileTask side_tasks[PROFILE_TASK_COUNT];
    size_t side_task_count;
    wchar_t *interests[PROFILE_MAX_ITEMS], *hobbies[PROFILE_MAX_ITEMS];
    size_t interest_count, hobby_count;
    /* 0-100. Its effect on playback is still an open decision (TRACKER.md D4). */
    unsigned randomness_score;
    /* Percentage of typed characters that start a typo-and-correct sequence. */
    unsigned typo_percent;
} Profile;
typedef struct { Profile *items; size_t count; } ProfileSet;
int profile_load(const wchar_t *path, ProfileSet *set, char *error, size_t capacity);
int profile_parse(const wchar_t *text, ProfileSet *set, char *error, size_t capacity);
void profile_free(ProfileSet *set);
const Profile *profile_find(const ProfileSet *set, unsigned id);
const char *profile_task_name(ProfileTask task);
const char *profile_gender_name(ProfileGender gender);
/* Resolve project_path to an absolute folder (it is not created). */
int profile_project_root(const Profile *profile, wchar_t *out, size_t capacity);
#endif
