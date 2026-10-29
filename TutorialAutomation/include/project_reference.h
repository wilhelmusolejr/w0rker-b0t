#ifndef PROJECT_REFERENCE_H
#define PROJECT_REFERENCE_H
#include <windows.h>
#include <stddef.h>
#include "profile.h"
/* Reference projects (programming_files/<project>) that a profile recreates by typing,
   the settings in profiles/programming.json, and each profile's project history. */
#define PROJECT_MAX_PATTERNS 32
typedef struct {
    wchar_t references[MAX_PATH];          /* folder holding one subfolder per reference project */
    wchar_t progress_folder[MAX_PATH];     /* profiles/progress: one history file per profile */
    wchar_t project_list[MAX_PATH];        /* profiles/projects.json: GitHub projects and pinned commits */
    wchar_t *copy_patterns[PROJECT_MAX_PATTERNS];
    wchar_t *ignore_patterns[PROJECT_MAX_PATTERNS];
    size_t copy_count, ignore_count;
    unsigned max_typed_bytes, max_project_characters;
    unsigned char_ms[2], newline_ms[2], pause_every_chars[2], pause_ms[2];
    unsigned action_pause_ms[2], mouse_move_ms[2];
    unsigned checkpoint_seconds, idle_percent, idle_seconds[2], wander_percent;
} ProgrammingConfig;
int programming_config_load(const wchar_t *path, ProgrammingConfig *config, char *error, size_t capacity);
void programming_config_free(ProgrammingConfig *config);
int project_pattern_match(const wchar_t *pattern, const wchar_t *name);

typedef enum { PROJECT_TYPE, PROJECT_COPY } ProjectKind;
typedef struct {
    wchar_t relative[MAX_PATH];  /* backslash-separated, relative to the project root */
    ProjectKind kind;
    wchar_t *text;               /* normalized LF text for typed files */
    size_t length;
    char sha256[65];
    unsigned long long bytes;
} ProjectFile;
typedef struct {
    wchar_t name[128], root[MAX_PATH];
    ProjectFile *files;
    size_t file_count;
    wchar_t (*folders)[MAX_PATH]; /* every folder, parents before children */
    size_t folder_count;
} ProjectReference;
/* Reads one reference project; files are ordered folder by folder, files before subfolders. */
int project_reference_load(const ProgrammingConfig *config, const wchar_t *name, ProjectReference *project,
                           char *error, size_t capacity);
void project_reference_free(ProjectReference *project);

/* Project history: what this profile has created, finished, and still owns. */
typedef enum { HISTORY_PARTIAL, HISTORY_TYPED, HISTORY_COPIED } HistoryFileStatus;
typedef struct { wchar_t relative[MAX_PATH]; char sha256[65]; HistoryFileStatus status; } HistoryFile;
typedef struct {
    wchar_t reference[128], destination[MAX_PATH], started[32], finished[32];
    char commit[41];
    int completed, skipped; /* skipped: over max_project_characters, never started */
    HistoryFile *files;
    size_t file_count, file_capacity;
} HistoryProject;
typedef struct {
    wchar_t path[MAX_PATH], profile[64];
    HistoryProject *projects;
    size_t project_count;
} ProjectHistory;
int project_history_load(const ProgrammingConfig *config, const Profile *profile, ProjectHistory *history,
                         char *error, size_t capacity);
int project_history_save(const ProjectHistory *history, char *error, size_t capacity);
void project_history_free(ProjectHistory *history);
HistoryProject *project_history_add(ProjectHistory *history, const wchar_t *reference, const wchar_t *destination);
HistoryFile *project_history_file(HistoryProject *project, const wchar_t *relative);
int project_history_mark(HistoryProject *project, const wchar_t *relative, const char *sha256, HistoryFileStatus status);

/* What to do with each file, decided before any input is sent. */
typedef enum { STEP_CREATE, STEP_RESUME, STEP_DONE, STEP_COPY, STEP_CONFLICT } ProjectStepKind;
/* trim: characters at the end of a partial file that do not match the reference (typing that
   went astray when the run was interrupted); they are deleted before typing resumes at offset. */
typedef struct { ProjectStepKind kind; size_t offset, trim; const char *reason; } ProjectStep;
typedef struct {
    const ProjectReference *reference;
    HistoryProject *history;  /* NULL until the project is started */
    wchar_t destination[MAX_PATH];
    ProjectStep *steps;
    size_t remaining_files, remaining_characters, conflicts, missing_folders;
    int fresh;                /* the project folder does not exist yet */
} ProjectPlan;
/* A free project folder under the profile's project_path: name, then name-2 ... name-9. */
int project_destination(const Profile *profile, const wchar_t *name, wchar_t destination[MAX_PATH], char *error, size_t capacity);
size_t project_reference_characters(const ProjectReference *reference);
int project_plan_build(const ProjectReference *reference, HistoryProject *history, const wchar_t *destination,
                       ProjectPlan *plan, char *error, size_t capacity);
void project_plan_free(ProjectPlan *plan);
const char *project_step_name(ProjectStepKind kind);
#endif
