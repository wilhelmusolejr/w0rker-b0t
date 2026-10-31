#include "flow_config.h"
#include "playback.h"
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

static const wchar_t *const minimal =
    L"{\"version\":1,\"workflow_id\":\"unit-flow\",\"profile_id\":1,"
    L"\"execution\":{\"mode\":\"sequential\",\"finish_when\":\"last_step_completes\","
    L"\"checkpoint_after_each_step\":true},"
    L"\"time_plan\":{\"expected_total_minutes\":3,\"estimated_transitions_minutes\":0,"
    L"\"idle_minutes_each\":1,\"idle_placement\":\"between_safe_points\"},"
    L"\"steps\":[{\"id\":\"browse-1\",\"action\":\"browser.browse\","
    L"\"source\":\"profile_catalog\",\"work_minutes\":2,\"idle_minutes\":1}]}";

int main(int argc, char **argv) {
    FlowConfig flow;
    char error[512];
    int i, loggy = 0, coding = 0, browsing = 0, other = 0, idle = 0;
    int wide_argc = 0;
    wchar_t **wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    Playback p = {0};
    (void)argv;
    if (argc != 2 || !wide_argv || wide_argc != 2) { if (wide_argv) LocalFree(wide_argv); return 2; }
    if (!flow_config_load(wide_argv[1], &flow, error, sizeof(error))) {
        fprintf(stderr, "sample flow: %s\n", error);
        LocalFree(wide_argv);
        return 1;
    }
    LocalFree(wide_argv);
    CHECK(flow.step_count == 22);
    for (i = 0; i < (int)flow.step_count; ++i) {
        FlowStep *s = &flow.steps[i];
        if (s->action >= FLOW_LOGGY_STATUS) ++loggy;
        if (s->action == FLOW_CODE) coding += s->work_minutes;
        else if (s->action == FLOW_BROWSE) browsing += s->work_minutes;
        else other += s->work_minutes;
        idle += s->idle_minutes;
    }
    CHECK(loggy == 0);
    CHECK(coding == 427 && browsing == 120 && other == 20 && idle == 58);
    CHECK(flow.expected_total_minutes == coding + browsing + other + idle + flow.estimated_transitions_minutes);
    CHECK(flow_config_parse(minimal, &flow, error, sizeof(error)));
    CHECK(flow.step_count == 1 && flow.steps[0].idle_minutes == 1);
    CHECK(flow_config_parse(
        L"{\"version\":1,\"workflow_id\":\"recorder-only\",\"profile_id\":1,"
        L"\"execution\":{\"mode\":\"sequential\",\"finish_when\":\"last_step_completes\","
        L"\"checkpoint_after_each_step\":false},"
        L"\"time_plan\":{\"expected_total_minutes\":0,\"estimated_transitions_minutes\":0,"
        L"\"idle_minutes_each\":1,\"idle_placement\":\"between_safe_points\"},"
        L"\"steps\":[{\"id\":\"record\",\"action\":\"loggy3.record\"}]}",
        &flow, error, sizeof(error)));
    CHECK(flow.step_count == 1 && flow.steps[0].action == FLOW_LOGGY_RECORD);
    CHECK(!flow_config_parse(
        L"{\"version\":1,\"workflow_id\":\"bad\",\"profile_id\":1,"
        L"\"execution\":{\"mode\":\"sequential\",\"finish_when\":\"last_step_completes\","
        L"\"checkpoint_after_each_step\":true},"
        L"\"time_plan\":{\"expected_total_minutes\":4,\"estimated_transitions_minutes\":0,"
        L"\"idle_minutes_each\":1,\"idle_placement\":\"between_safe_points\"},"
        L"\"steps\":[{\"id\":\"browse-1\",\"action\":\"browser.browse\","
        L"\"source\":\"profile_catalog\",\"work_minutes\":2,\"idle_minutes\":1}]}",
        &flow, error, sizeof(error)));
    CHECK(strstr(error, "time plan mismatch") != NULL);
    CHECK(!flow_config_parse(
        L"{\"version\":1,\"workflow_id\":\"bad\",\"profile_id\":1,"
        L"\"execution\":{\"mode\":\"sequential\",\"finish_when\":\"last_step_completes\","
        L"\"checkpoint_after_each_step\":true},"
        L"\"time_plan\":{\"expected_total_minutes\":3,\"estimated_transitions_minutes\":0,"
        L"\"idle_minutes_each\":1,\"idle_placement\":\"between_safe_points\"},"
        L"\"steps\":[{\"id\":\"browse-1\",\"action\":\"shell.run\","
        L"\"source\":\"profile_catalog\",\"work_minutes\":2,\"idle_minutes\":1}]}",
        &flow, error, sizeof(error)));
    CHECK(strstr(error, "unsupported step action") != NULL);
    p.paused_ms = 2000;
    p.deadline = GetTickCount64() - 1000;
    CHECK(!playback_yield(&p)); /* A deadline in wall time can still be ahead in active time. */
    p.paused_ms = 0;
    CHECK(playback_yield(&p));
    return failures ? 1 : 0;
}
