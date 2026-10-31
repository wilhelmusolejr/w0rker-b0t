#include "flow_config.h"
#include "json_config.h"
#include "script_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int bad(char *error, size_t cap, const char *message) {
    snprintf(error, cap, "%s", message);
    return 0;
}
static int keys(const JsonValue *v, const wchar_t *const *names, size_t count,
                char *error, size_t cap, const char *where) {
    if (json_keys(v, names, count)) return 1;
    snprintf(error, cap, "%s has an unknown field or is not an object", where);
    return 0;
}
#define KEYS(v, names, error, cap, where) keys(v, names, sizeof(names)/sizeof((names)[0]), error, cap, where)
static const JsonValue *required(const JsonValue *object, const wchar_t *name, JsonKind kind,
                                 char *error, size_t cap) {
    const JsonValue *v = json_get(object, name);
    if (!v || v->kind != kind) {
        char field[80] = {0};
        WideCharToMultiByte(CP_UTF8, 0, name, -1, field, sizeof(field), NULL, NULL);
        snprintf(error, cap, "missing or invalid %s", field);
        return NULL;
    }
    return v;
}
static int identifier(const wchar_t *value, char out[80]) {
    size_t i;
    if (!value || !*value || wcslen(value) >= 80) return 0;
    for (i = 0; value[i]; ++i) {
        wchar_t c = value[i];
        if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
              (c >= L'0' && c <= L'9') || c == L'-' || c == L'_')) return 0;
        out[i] = (char)c;
    }
    out[i] = 0;
    return 1;
}
static const wchar_t *const action_keys[] = {
    L"app.ensure_open", L"app.focus", L"app.save", L"app.close", L"code.work",
    L"browser.browse", L"notepad.write_notes", L"calculator.do_calculations",
    L"loggy3.status", L"loggy3.show", L"loggy3.minimize", L"loggy3.close",
    L"loggy3.record", L"loggy3.stop"
};
static const char *const action_names[] = {
    "app.ensure_open", "app.focus", "app.save", "app.close", "code.work",
    "browser.browse", "notepad.write_notes", "calculator.do_calculations",
    "loggy3.status", "loggy3.show", "loggy3.minimize", "loggy3.close",
    "loggy3.record", "loggy3.stop"
};
const char *flow_action_name(FlowAction action) {
    return (unsigned)action < sizeof(action_names)/sizeof(*action_names) ? action_names[action] : "?";
}
const char *flow_app_name(FlowApp app) {
    static const char *const names[] = {"none", "vscode", "browser", "notepad", "calculator"};
    return (unsigned)app < sizeof(names)/sizeof(*names) ? names[app] : "?";
}
static FlowApp app_from_name(const wchar_t *name) {
    if (!wcscmp(name, L"vscode")) return FLOW_APP_VSCODE;
    if (!wcscmp(name, L"browser")) return FLOW_APP_BROWSER;
    if (!wcscmp(name, L"notepad")) return FLOW_APP_NOTEPAD;
    if (!wcscmp(name, L"calculator")) return FLOW_APP_CALCULATOR;
    return FLOW_APP_NONE;
}
static int parse_lifecycle(const JsonValue *array, unsigned *flags, int before, char *error, size_t cap) {
    const JsonValue *v;
    if (!array) return 1;
    if (array->kind != JSON_ARRAY) return bad(error, cap, "before and after must be arrays");
    for (v = array->child; v; v = v->next) {
        unsigned bit = 0;
        if (v->kind != JSON_STRING) return bad(error, cap, "lifecycle entries must be strings");
        if (before && !wcscmp(v->string, L"ensure_open")) bit = FLOW_BEFORE_OPEN;
        else if (before && !wcscmp(v->string, L"focus")) bit = FLOW_BEFORE_FOCUS;
        else if (!before && !wcscmp(v->string, L"save")) bit = FLOW_AFTER_SAVE;
        else if (!before && !wcscmp(v->string, L"close")) bit = FLOW_AFTER_CLOSE;
        if (!bit || (*flags & bit)) return bad(error, cap, "unknown or duplicate lifecycle action");
        if ((before && bit == FLOW_BEFORE_OPEN && (*flags & FLOW_BEFORE_FOCUS)) ||
            (!before && bit == FLOW_AFTER_SAVE && (*flags & FLOW_AFTER_CLOSE)))
            return bad(error, cap, "lifecycle actions must list ensure_open before focus and save before close");
        *flags |= bit;
    }
    return 1;
}
static int parse_step(const JsonValue *v, FlowStep *step, char *error, size_t cap) {
    static const wchar_t *const names[] = {L"id", L"action", L"app", L"work_minutes", L"idle_minutes",
        L"before", L"after", L"project", L"source"};
    const JsonValue *field;
    unsigned a;
    if (!KEYS(v, names, error, cap, "step")) return 0;
    field = required(v, L"id", JSON_STRING, error, cap);
    if (!field || !identifier(field->string, step->id)) return bad(error, cap, "step id must use 1-79 ASCII letters, digits, '-' or '_'");
    field = required(v, L"action", JSON_STRING, error, cap);
    if (!field) return 0;
    for (a = 0; a < sizeof(action_keys)/sizeof(*action_keys); ++a)
        if (!wcscmp(field->string, action_keys[a])) break;
    if (a == sizeof(action_keys)/sizeof(*action_keys)) return bad(error, cap, "unsupported step action");
    step->action = (FlowAction)a;
    field = json_get(v, L"app");
    if (field) {
        if (field->kind != JSON_STRING || !(step->app = app_from_name(field->string)))
            return bad(error, cap, "app must be vscode, browser, notepad, or calculator");
    }
    if (a <= FLOW_CLOSE && !step->app) return bad(error, cap, "app lifecycle step needs app");
    if (a == FLOW_SAVE && step->app != FLOW_APP_VSCODE && step->app != FLOW_APP_NOTEPAD)
        return bad(error, cap, "app.save supports only vscode or notepad");
    if (a == FLOW_CODE || a == FLOW_BROWSE || a == FLOW_NOTES || a == FLOW_CALCULATE) {
        FlowApp expected = a == FLOW_CODE ? FLOW_APP_VSCODE : a == FLOW_BROWSE ? FLOW_APP_BROWSER :
                           a == FLOW_NOTES ? FLOW_APP_NOTEPAD : FLOW_APP_CALCULATOR;
        const JsonValue *work = required(v, L"work_minutes", JSON_NUMBER, error, cap);
        const JsonValue *idle = required(v, L"idle_minutes", JSON_NUMBER, error, cap);
        if (!work || !idle || !work->number || work->number > 600 || idle->number > 120)
            return bad(error, cap, "timed step needs work_minutes 1-600 and idle_minutes 0-120");
        if (step->app && step->app != expected) return bad(error, cap, "timed action and app disagree");
        step->app = expected; step->work_minutes = work->number; step->idle_minutes = idle->number;
        if (step->idle_minutes >= step->work_minutes) return bad(error, cap, "idle_minutes must be less than work_minutes");
        if (a >= FLOW_NOTES && step->idle_minutes) return bad(error, cap, "other-app actions do not support separate idle_minutes");
    } else if (json_get(v, L"work_minutes") || json_get(v, L"idle_minutes"))
        return bad(error, cap, "untimed action cannot have work_minutes or idle_minutes");
    if (a >= FLOW_LOGGY_STATUS && (step->app || json_get(v, L"before") || json_get(v, L"after")))
        return bad(error, cap, "loggy3 actions cannot have app lifecycle fields");
    if (a <= FLOW_CLOSE && (json_get(v, L"before") || json_get(v, L"after")))
        return bad(error, cap, "app lifecycle steps cannot have before or after");
    if (!parse_lifecycle(json_get(v, L"before"), &step->lifecycle, 1, error, cap) ||
        !parse_lifecycle(json_get(v, L"after"), &step->lifecycle, 0, error, cap)) return 0;
    field = json_get(v, L"project");
    if ((a == FLOW_CODE) != (field != NULL) || (field && (field->kind != JSON_STRING || wcscmp(field->string, L"resume_or_next"))))
        return bad(error, cap, "code.work requires project: resume_or_next");
    field = json_get(v, L"source");
    if ((a == FLOW_BROWSE) != (field != NULL) || (field && (field->kind != JSON_STRING || wcscmp(field->string, L"profile_catalog"))))
        return bad(error, cap, "browser.browse requires source: profile_catalog");
    if ((step->lifecycle & FLOW_AFTER_SAVE) && step->app != FLOW_APP_VSCODE && step->app != FLOW_APP_NOTEPAD)
        return bad(error, cap, "after save is only valid for vscode or notepad");
    return 1;
}
int flow_config_parse(const wchar_t *text, FlowConfig *flow, char *error, size_t cap) {
    static const wchar_t *const root_keys[] = {L"version", L"workflow_id", L"profile_id", L"execution", L"time_plan", L"steps"};
    static const wchar_t *const exec_keys[] = {L"mode", L"finish_when", L"checkpoint_after_each_step"};
    static const wchar_t *const time_keys[] = {L"expected_total_minutes", L"estimated_transitions_minutes",
        L"idle_minutes_each", L"idle_placement"};
    JsonValue *root = NULL;
    const JsonValue *v, *execution, *time_plan, *steps;
    unsigned timed = 0, i;
    uint64_t hash = UINT64_C(14695981039346656037);
    const wchar_t *scan;
    memset(flow, 0, sizeof(*flow));
    for (scan = text; *scan; ++scan) { hash ^= (unsigned)*scan; hash *= UINT64_C(1099511628211); }
    flow->fingerprint = hash;
    if (!json_parse(text, &root, error, cap)) return 0;
    if (!KEYS(root, root_keys, error, cap, "flow")) goto fail;
    v = required(root, L"version", JSON_NUMBER, error, cap);
    if (!v || v->number != 1) { bad(error, cap, "flow version must be 1"); goto fail; }
    v = required(root, L"workflow_id", JSON_STRING, error, cap);
    if (!v || !identifier(v->string, flow->workflow_id)) { bad(error, cap, "workflow_id must use 1-79 ASCII letters, digits, '-' or '_'"); goto fail; }
    v = required(root, L"profile_id", JSON_NUMBER, error, cap);
    if (!v || !v->number) { bad(error, cap, "profile_id must be positive"); goto fail; }
    flow->profile_id = v->number;
    execution = required(root, L"execution", JSON_OBJECT, error, cap);
    if (!execution || !KEYS(execution, exec_keys, error, cap, "execution")) goto fail;
    v = required(execution, L"mode", JSON_STRING, error, cap);
    if (!v || wcscmp(v->string, L"sequential")) { bad(error, cap, "execution.mode must be sequential"); goto fail; }
    v = required(execution, L"finish_when", JSON_STRING, error, cap);
    if (!v || wcscmp(v->string, L"last_step_completes")) { bad(error, cap, "execution.finish_when must be last_step_completes"); goto fail; }
    v = required(execution, L"checkpoint_after_each_step", JSON_BOOL, error, cap);
    if (!v) goto fail;
    flow->checkpoint_after_each_step = (int)v->number;
    time_plan = required(root, L"time_plan", JSON_OBJECT, error, cap);
    if (!time_plan || !KEYS(time_plan, time_keys, error, cap, "time_plan")) goto fail;
    v = required(time_plan, L"expected_total_minutes", JSON_NUMBER, error, cap);
    if (!v || v->number > 1440) { bad(error, cap, "expected_total_minutes must be 0-1440"); goto fail; }
    flow->expected_total_minutes = v->number;
    v = required(time_plan, L"estimated_transitions_minutes", JSON_NUMBER, error, cap);
    if (!v || v->number > 240) { bad(error, cap, "estimated_transitions_minutes must be 0-240"); goto fail; }
    flow->estimated_transitions_minutes = v->number;
    v = required(time_plan, L"idle_minutes_each", JSON_NUMBER, error, cap);
    if (!v || !v->number || v->number > 10) { bad(error, cap, "idle_minutes_each must be 1-10"); goto fail; }
    flow->idle_minutes_each = v->number;
    v = required(time_plan, L"idle_placement", JSON_STRING, error, cap);
    if (!v || wcscmp(v->string, L"between_safe_points")) { bad(error, cap, "idle_placement must be between_safe_points"); goto fail; }
    steps = required(root, L"steps", JSON_ARRAY, error, cap);
    if (!steps || !steps->child) { bad(error, cap, "steps must be a nonempty array"); goto fail; }
    for (v = steps->child; v; v = v->next) {
        FlowStep *step;
        if (flow->step_count == FLOW_MAX_STEPS) { bad(error, cap, "too many steps"); goto fail; }
        step = &flow->steps[flow->step_count];
        if (!parse_step(v, step, error, cap)) {
            char detail[256]; snprintf(detail, sizeof(detail), "step %u: %s", flow->step_count + 1, error);
            snprintf(error, cap, "%s", detail); goto fail;
        }
        for (i = 0; i < flow->step_count; ++i)
            if (!strcmp(flow->steps[i].id, step->id)) { bad(error, cap, "duplicate step id"); goto fail; }
        if (step->idle_minutes % flow->idle_minutes_each) { bad(error, cap, "idle_minutes must be a multiple of idle_minutes_each"); goto fail; }
        timed += step->work_minutes + step->idle_minutes;
        ++flow->step_count;
    }
    if (timed + flow->estimated_transitions_minutes != flow->expected_total_minutes) {
        snprintf(error, cap, "time plan mismatch: steps %u + transitions %u != expected %u minutes",
                 timed, flow->estimated_transitions_minutes, flow->expected_total_minutes);
        goto fail;
    }
    json_free(root);
    return 1;
fail:
    json_free(root);
    memset(flow, 0, sizeof(*flow));
    return 0;
}
int flow_config_load(const wchar_t *path, FlowConfig *flow, char *error, size_t cap) {
    wchar_t *text = NULL;
    int ok;
    if (!read_utf8(path, &text, error, cap)) return 0;
    ok = flow_config_parse(text, flow, error, cap);
    free(text);
    return ok;
}
void flow_config_print(const FlowConfig *flow) {
    unsigned i, coding = 0, browsing = 0, other = 0, idle = 0;
    printf("Flow %s, profile %u, %u sequential steps.\n", flow->workflow_id, flow->profile_id, flow->step_count);
    for (i = 0; i < flow->step_count; ++i) {
        const FlowStep *s = &flow->steps[i];
        printf("%2u. %-18s %-27s", i + 1, s->id, flow_action_name(s->action));
        if (s->work_minutes) printf(" %u min work + %u min idle", s->work_minutes, s->idle_minutes);
        else if (s->app) printf(" %s", flow_app_name(s->app));
        putchar('\n');
        if (s->lifecycle) {
            if (s->lifecycle & (FLOW_BEFORE_OPEN | FLOW_BEFORE_FOCUS)) {
                printf("     before:");
                if (s->lifecycle & FLOW_BEFORE_OPEN) printf(" ensure_open");
                if (s->lifecycle & FLOW_BEFORE_FOCUS) printf(" focus");
                putchar('\n');
            }
            if (s->lifecycle & (FLOW_AFTER_SAVE | FLOW_AFTER_CLOSE)) {
                printf("     after:");
                if (s->lifecycle & FLOW_AFTER_SAVE) printf(" save");
                if (s->lifecycle & FLOW_AFTER_CLOSE) printf(" close");
                putchar('\n');
            }
        }
        if (s->action == FLOW_CODE) coding += s->work_minutes;
        else if (s->action == FLOW_BROWSE) browsing += s->work_minutes;
        else other += s->work_minutes;
        idle += s->idle_minutes;
    }
    printf("Coding %u + browsing %u + other apps %u + idle %u + estimated transitions %u = %u minutes.\n",
        coding, browsing, other, idle, flow->estimated_transitions_minutes, flow->expected_total_minutes);
    puts("Transitions are an estimate, not an added wait. The flow ends after its last step. Loggy3 runs only when listed.");
}
