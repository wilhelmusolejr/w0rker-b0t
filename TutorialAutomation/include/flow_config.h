#ifndef FLOW_CONFIG_H
#define FLOW_CONFIG_H
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

#define FLOW_MAX_STEPS 256
typedef enum { FLOW_APP_NONE, FLOW_APP_VSCODE, FLOW_APP_BROWSER, FLOW_APP_NOTEPAD, FLOW_APP_CALCULATOR } FlowApp;
typedef enum { FLOW_ENSURE_OPEN, FLOW_FOCUS, FLOW_SAVE, FLOW_CLOSE, FLOW_CODE, FLOW_BROWSE,
               FLOW_NOTES, FLOW_CALCULATE, FLOW_LOGGY_STATUS, FLOW_LOGGY_SHOW,
               FLOW_LOGGY_MINIMIZE, FLOW_LOGGY_CLOSE, FLOW_LOGGY_RECORD, FLOW_LOGGY_STOP } FlowAction;
enum { FLOW_BEFORE_OPEN = 1, FLOW_BEFORE_FOCUS = 2, FLOW_AFTER_SAVE = 4, FLOW_AFTER_CLOSE = 8 };
typedef struct {
    char id[80];
    FlowAction action;
    FlowApp app;
    unsigned work_minutes, idle_minutes;
    unsigned lifecycle;
} FlowStep;
typedef struct {
    char workflow_id[80];
    unsigned profile_id, expected_total_minutes, estimated_transitions_minutes;
    unsigned idle_minutes_each, step_count;
    int checkpoint_after_each_step;
    uint64_t fingerprint;
    FlowStep steps[FLOW_MAX_STEPS];
} FlowConfig;
int flow_config_parse(const wchar_t *text, FlowConfig *flow, char *error, size_t capacity);
int flow_config_load(const wchar_t *path, FlowConfig *flow, char *error, size_t capacity);
const char *flow_action_name(FlowAction action);
const char *flow_app_name(FlowApp app);
void flow_config_print(const FlowConfig *flow);
#endif
