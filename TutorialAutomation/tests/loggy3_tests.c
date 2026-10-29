#include "loggy3.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static int checks, failures;
#define CHECK(t) do { ++checks; if (!(t)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#t); } } while (0)

/* One visible, enabled, pressable recording button with this label. */
static LoggySnapshot snapshot(const wchar_t *label) {
    LoggySnapshot s;
    memset(&s, 0, sizeof(s));
    wcscpy(s.buttons[0].name, label);
    s.buttons[0].enabled = 1; s.buttons[0].invoke = 1;
    SetRect(&s.buttons[0].rect, 10, 700, 110, 735);
    s.button_count = 1;
    wcscpy(s.timer, L"--:--:--");
    return s;
}
static void state_tests(void) {
    LoggySnapshot s;
    unsigned index = 9;
    CHECK((s = snapshot(L"Start"), loggy_recording_state(&s, &index) == LOGGY_STOPPED && index == 0));
    CHECK((s = snapshot(L"Start recording"), loggy_recording_state(&s, NULL) == LOGGY_STOPPED));
    CHECK((s = snapshot(L"Stop"), loggy_recording_state(&s, NULL) == LOGGY_RECORDING));
    CHECK((s = snapshot(L"  Stop   recording "), loggy_recording_state(&s, NULL) == LOGGY_RECORDING));
    CHECK((s = snapshot(L"stop"), loggy_recording_state(&s, NULL) == LOGGY_RECORDING));
    CHECK((s = snapshot(L"Preparing"), loggy_recording_state(&s, NULL) == LOGGY_UNKNOWN));
    s = snapshot(L"Start"); s.buttons[0].enabled = 0;
    CHECK(loggy_recording_state(&s, NULL) == LOGGY_BUSY);
    s = snapshot(L"Start"); s.buttons[0].offscreen = 1;
    CHECK(loggy_recording_state(&s, NULL) == LOGGY_UNKNOWN);
    s = snapshot(L"Start"); s.button_count = 0;
    CHECK(loggy_recording_state(&s, NULL) == LOGGY_UNKNOWN);
    /* Two current controls is ambiguous; an offscreen second one is ignored. */
    s = snapshot(L"Start"); s.buttons[1] = s.buttons[0]; wcscpy(s.buttons[1].name, L"Stop"); s.button_count = 2;
    CHECK(loggy_recording_state(&s, NULL) == LOGGY_UNKNOWN);
    s.buttons[0].offscreen = 1;
    CHECK(loggy_recording_state(&s, &index) == LOGGY_RECORDING && index == 1);
    s = snapshot(L"Start"); s.button_count = LOGGY_MAX_BUTTONS + 1;
    CHECK(loggy_recording_state(&s, NULL) == LOGGY_UNKNOWN);
}
static void plan_tests(void) {
    LoggySnapshot s;
    LoggyPlan plan;
    const wchar_t *refused[] = {L"Preparing", L"Start"};
    unsigned i;
    /* Start and Stop are planned only from the opposite known state. */
    s = snapshot(L"Start");
    CHECK(loggy_plan(LOGGY_RECORD, &s, &plan) && plan.press && !wcscmp(plan.expected, L"Start") && plan.desired == LOGGY_RECORDING);
    s = snapshot(L"Stop");
    CHECK(loggy_plan(LOGGY_STOP, &s, &plan) && plan.press && !wcscmp(plan.expected, L"Stop") && plan.desired == LOGGY_STOPPED);
    /* Repeating a command never toggles the recording back. */
    s = snapshot(L"Stop");
    CHECK(loggy_plan(LOGGY_RECORD, &s, &plan) && !plan.press);
    s = snapshot(L"Start");
    CHECK(loggy_plan(LOGGY_STOP, &s, &plan) && !plan.press);
    /* Status never presses anything. */
    CHECK(loggy_plan(LOGGY_STATUS, &s, &plan) && !plan.press && plan.state == LOGGY_STOPPED);
    /* Unknown, disabled, hidden, missing, and conflicting controls are refused. */
    for (i = 0; i < 2; ++i) {
        s = snapshot(refused[i]);
        if (i == 1) s.buttons[0].enabled = 0;
        CHECK(!loggy_plan(LOGGY_RECORD, &s, &plan) && !plan.press && plan.reason[0]);
    }
    s = snapshot(L"Start"); s.buttons[0].offscreen = 1;
    CHECK(!loggy_plan(LOGGY_RECORD, &s, &plan) && !plan.press);
    s = snapshot(L"Start"); s.button_count = 0;
    CHECK(!loggy_plan(LOGGY_RECORD, &s, &plan) && !plan.press);
    s = snapshot(L"Start"); s.buttons[1] = s.buttons[0]; wcscpy(s.buttons[1].name, L"Stop"); s.button_count = 2;
    CHECK(!loggy_plan(LOGGY_RECORD, &s, &plan) && !plan.press);
    /* A control that cannot be pressed is not pressed. */
    s = snapshot(L"Start"); s.buttons[0].invoke = 0;
    CHECK(!loggy_plan(LOGGY_RECORD, &s, &plan) && !plan.press);
    CHECK(!loggy_plan(LOGGY_SHOW, &s, &plan));
}
static void name_tests(void) {
    LoggyAction action;
    CHECK(loggy_title_matches(L"Loggy3") && loggy_title_matches(L"Loggy3 - Sessions") && loggy_title_matches(L"loggy3"));
    CHECK(!loggy_title_matches(L"com.loggy3.recorder-siw") && !loggy_title_matches(L"Loggy30") && !loggy_title_matches(L"My Loggy3"));
    CHECK(loggy_class_has(L"btn btn-record btn-primary", L"btn-record") && loggy_class_has(L"btn-record", L"btn-record"));
    CHECK(!loggy_class_has(L"btn btn-record-history", L"btn-record") && !loggy_class_has(L"history-start", L"btn-record"));
    CHECK(!loggy_class_has(NULL, L"btn-record") && loggy_class_has(L"bottom-bar", L"bottom-bar"));
    CHECK(loggy_action_from_name(L"record", &action) && action == LOGGY_RECORD);
    CHECK(loggy_action_from_name(L"STATUS", &action) && action == LOGGY_STATUS);
    CHECK(!loggy_action_from_name(L"pause", &action));
    CHECK(!strcmp(loggy_state_name(LOGGY_RECORDING), "recording") && !strcmp(loggy_action_name(LOGGY_DEMO), "demo"));
}
static void today_tests(void) {
    LoggyDay d;
    /* Rows exactly as Loggy3 names them. */
    CHECK(loggy_parse_day(L"Wednesday, October 28, 2026 (UTC): 10h 17m 27s, 26 sessions, 1h 16m of this total is above the paid limit", &d));
    CHECK(d.year == 2026 && d.month == 10 && d.day == 28 && d.seconds == 10 * 3600 + 17 * 60 + 27 && d.sessions == 26);
    CHECK(loggy_parse_day(L"Thursday, October 29, 2026 (UTC): 48s, 1 sessions", &d) && d.seconds == 48 && d.sessions == 1 && d.day == 29);
    CHECK(loggy_parse_day(L"Saturday, October 24, 2026 (UTC): 24h 46m 52s, 21 sessions, 4h 46m of this total is above the paid limit", &d) &&
        d.seconds == 24 * 3600 + 46 * 60 + 52);
    CHECK(loggy_parse_day(L"Thursday, October 15, 2026 (UTC): 49m 24s, 7 sessions", &d) && d.seconds == 49 * 60 + 24);
    CHECK(loggy_parse_day(L"Sunday, October 25, 2026 (UTC): 0s, 0 sessions", &d) && d.seconds == 0 && d.sessions == 0);
    CHECK(loggy_parse_day(L"Friday, January 1, 2027 (UTC): 1h 0m 0s, 1 session", &d) && d.month == 1 && d.year == 2027 && d.seconds == 3600);
    /* Other buttons and malformed text are not day rows. */
    CHECK(!loggy_parse_day(L"Date range: Pay period. Switch to Month", &d));
    CHECK(!loggy_parse_day(L"Previous pay period", &d));
    CHECK(!loggy_parse_day(L"Thursday, Octember 29, 2026 (UTC): 48s, 1 sessions", &d));
    CHECK(!loggy_parse_day(L"Thursday, October 29, 2026 (UTC): 48x, 1 sessions", &d));
    CHECK(!loggy_parse_day(L"Thursday, October 29, 2026 (UTC): , 1 sessions", &d));
    CHECK(!loggy_parse_day(NULL, &d));
    /* Today's recorded time: finished sessions plus the running recording. */
    {
        LoggyToday t;
        memset(&t, 0, sizeof(t));
        CHECK(loggy_today_seconds(&t) == -1);
        t.found = 1; t.day.seconds = 48; t.state = LOGGY_STOPPED; t.timer_seconds = -1;
        CHECK(loggy_today_seconds(&t) == 48);
        t.state = LOGGY_RECORDING; t.timer_seconds = 78;
        CHECK(loggy_today_seconds(&t) == 126);
        t.state = LOGGY_BUSY;
        CHECK(loggy_today_seconds(&t) == 48);
    }
    /* The recording timer. */
    CHECK(loggy_timer_seconds(L"00:00:14") == 14 && loggy_timer_seconds(L"00:12:34") == 754);
    CHECK(loggy_timer_seconds(L"1:02:03") == 3723 && loggy_timer_seconds(L"10:30:00") == 37800);
    CHECK(loggy_timer_seconds(L"--:--:--") == -1 && loggy_timer_seconds(L"") == -1 && loggy_timer_seconds(NULL) == -1);
    CHECK(loggy_timer_seconds(L"00:61:00") == -1 && loggy_timer_seconds(L"00:1:00") == -1 && loggy_timer_seconds(L"00:00:00x") == -1);
}
int main(void) {
    state_tests(); plan_tests(); name_tests(); today_tests();
    printf("%d checks, %d failures (no desktop input generated)\n", checks, failures);
    return failures ? 1 : 0;
}
