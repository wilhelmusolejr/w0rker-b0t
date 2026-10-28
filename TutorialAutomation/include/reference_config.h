#ifndef REFERENCE_CONFIG_H
#define REFERENCE_CONFIG_H
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include "coding_schedule.h"
typedef struct {
    wchar_t *name, *source, *content, *normalized;
    char sha256[65];
    int enabled, owned, done;
    size_t offset;
} ReferenceFile;
typedef enum { REFERENCE_PYTHON, REFERENCE_JAVASCRIPT, REFERENCE_URL, REFERENCE_RESEARCH, REFERENCE_GITHUB, REFERENCE_CATEGORY_COUNT } ReferenceCategory;
/* How a new browser window is opened: Windows Run, or the pinned taskbar icon via the mouse. */
typedef enum { REFERENCE_LAUNCH_RUN, REFERENCE_LAUNCH_TASKBAR } ReferenceLaunch;
typedef struct { wchar_t *query, *url; ReferenceCategory category; } ReferenceTopic;
typedef struct ReferenceConfig {
    wchar_t destination[MAX_PATH], progress_path[MAX_PATH];
    wchar_t url_list_path[MAX_PATH], research_path[MAX_PATH];
    unsigned countdown_seconds;
    uint32_t seed;
    unsigned char_ms[2], newline_ms[2], pause_every_chars[2], typing_pause_ms[2];
    unsigned action_pause_ms[2], mouse_move_ms[2], interval_seconds[2], visit_seconds[2];
    int browser_enabled;
    ReferenceLaunch browser_launch;
    /* Relative chance of each browsing category; all zero means equal chance. */
    unsigned weights[REFERENCE_CATEGORY_COUNT];
    unsigned results_to_open[2];
    int auto_restart;
    unsigned max_restarts, restart_wait_seconds;
    wchar_t source_folder[MAX_PATH];
    ReferenceFile *files;
    size_t file_count;
    ReferenceTopic *topics;
    size_t topic_count;
} ReferenceConfig;
int reference_config_load(const wchar_t *path, ReferenceConfig *config, char *error, size_t capacity);
void reference_config_free(ReferenceConfig *config);
int reference_config_preflight(ReferenceConfig *config, char *error, size_t capacity);
int reference_config_claim(ReferenceConfig *config, size_t index, char *error, size_t capacity);
int reference_sha256(const wchar_t *text, char hex[65]);
wchar_t *reference_normalize(const wchar_t *text);
int reference_prefix(const wchar_t *source, const wchar_t *saved, size_t *offset);
size_t reference_pick_topic(const ReferenceConfig *config, CodingSchedule *schedule, size_t previous);
const char *reference_category_name(ReferenceCategory category);
#endif
