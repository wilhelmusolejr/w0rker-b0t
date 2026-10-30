#define COBJMACROS
#include <uiautomation.h>
#include "loggy3.h"
#include "mouse.h"
#include "keyboard.h"
#include "ui_targets.h"
#include "window_manager.h"
#include "side_apps.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

static const char *action_names[LOGGY_ACTION_COUNT] = {"status", "show", "minimize", "close", "record", "stop", "demo"};
const char *loggy_action_name(LoggyAction a) { return a < LOGGY_ACTION_COUNT ? action_names[a] : "?"; }
int loggy_action_from_name(const wchar_t *name, LoggyAction *action) {
    unsigned i;
    for (i = 0; i < LOGGY_ACTION_COUNT; ++i) {
        wchar_t wide[16];
        MultiByteToWideChar(CP_UTF8, 0, action_names[i], -1, wide, 16);
        if (!_wcsicmp(name, wide)) { *action = (LoggyAction)i; return 1; }
    }
    return 0;
}
const char *loggy_state_name(LoggyState s) {
    return s == LOGGY_STOPPED ? "stopped" : s == LOGGY_RECORDING ? "recording" : s == LOGGY_BUSY ? "busy" : "unknown";
}
int loggy_title_matches(const wchar_t *title) {
    return title && !_wcsnicmp(title, L"Loggy3", 6) && (!title[6] || iswspace(title[6]));
}
int loggy_class_has(const wchar_t *classes, const wchar_t *token) {
    size_t length = wcslen(token);
    const wchar_t *at;
    if (!classes || !length) return 0;
    for (at = wcsstr(classes, token); at; at = wcsstr(at + 1, token))
        if ((at == classes || iswspace(at[-1])) && (!at[length] || iswspace(at[length]))) return 1;
    return 0;
}
/* The label with runs of spaces collapsed, compared without case. */
static LoggyState label_state(const wchar_t *name) {
    wchar_t label[64];
    size_t n = 0;
    for (; *name && n < 63; ++name)
        if (!iswspace(*name) || (n && label[n - 1] != L' ')) label[n++] = iswspace(*name) ? L' ' : *name;
    while (n && label[n - 1] == L' ') --n;
    label[n] = 0;
    if (!_wcsicmp(label, L"Start") || !_wcsicmp(label, L"Start recording")) return LOGGY_STOPPED;
    if (!_wcsicmp(label, L"Stop") || !_wcsicmp(label, L"Stop recording")) return LOGGY_RECORDING;
    return LOGGY_UNKNOWN;
}
LoggyState loggy_recording_state(const LoggySnapshot *s, unsigned *button) {
    unsigned i, visible = 0, index = 0;
    for (i = 0; i < s->button_count && i < LOGGY_MAX_BUTTONS; ++i)
        if (!s->buttons[i].offscreen) { ++visible; index = i; }
    if (s->button_count > LOGGY_MAX_BUTTONS || visible != 1) return LOGGY_UNKNOWN;
    if (button) *button = index;
    if (!s->buttons[index].enabled) return LOGGY_BUSY;
    return label_state(s->buttons[index].name);
}
int loggy_plan(LoggyAction action, const LoggySnapshot *s, LoggyPlan *plan) {
    unsigned index = 0;
    memset(plan, 0, sizeof(*plan));
    plan->state = loggy_recording_state(s, &index);
    if (action == LOGGY_STATUS) { snprintf(plan->reason, sizeof(plan->reason), "status only"); return 1; }
    if (action != LOGGY_RECORD && action != LOGGY_STOP) { snprintf(plan->reason, sizeof(plan->reason), "not a recording action"); return 0; }
    plan->desired = action == LOGGY_RECORD ? LOGGY_RECORDING : LOGGY_STOPPED;
    if (plan->state == plan->desired) {
        snprintf(plan->reason, sizeof(plan->reason), "already %s; nothing to press", loggy_state_name(plan->desired));
        return 1;
    }
    if (plan->state == LOGGY_UNKNOWN) {
        snprintf(plan->reason, sizeof(plan->reason), "expected one visible recording button with a Start or Stop label; nothing pressed");
        return 0;
    }
    if (plan->state == LOGGY_BUSY) {
        snprintf(plan->reason, sizeof(plan->reason), "the recording button is disabled or changing; nothing pressed");
        return 0;
    }
    if (!s->buttons[index].invoke) {
        snprintf(plan->reason, sizeof(plan->reason), "the recording control is not a pressable button; nothing pressed");
        return 0;
    }
    plan->press = 1;
    wcscpy(plan->expected, s->buttons[index].name);
    snprintf(plan->reason, sizeof(plan->reason), "%s -> %s", loggy_state_name(plan->state), loggy_state_name(plan->desired));
    return 1;
}

static const wchar_t *month_names[12] = {L"January", L"February", L"March", L"April", L"May", L"June",
    L"July", L"August", L"September", L"October", L"November", L"December"};
int loggy_parse_day(const wchar_t *name, LoggyDay *d) {
    const wchar_t *p = name ? wcschr(name, L',') : NULL, *utc = name ? wcsstr(name, L" (UTC): ") : NULL;
    wchar_t month[16], *end;
    unsigned n = 0, units = 0, m;
    memset(d, 0, sizeof(*d));
    if (!p || !utc || p > utc) return 0;
    /* ", October 29, 2026" */
    for (++p; *p == L' '; ++p) ;
    while (iswalpha(*p) && n < 15) month[n++] = *p++;
    month[n] = 0;
    for (m = 0; m < 12 && _wcsicmp(month, month_names[m]); ++m) ;
    if (m == 12 || *p != L' ') return 0;
    d->month = m + 1;
    d->day = (unsigned)wcstoul(p, &end, 10);
    if (end == p || *end != L',' || !d->day || d->day > 31) return 0;
    d->year = (unsigned)wcstoul(end + 1, &end, 10);
    if (end != utc || d->year < 2000) return 0;
    /* "10h 17m 27s" or "0s", then ", 26 sessions" */
    for (p = utc + 8; iswdigit(*p); ++units) {
        unsigned long value = wcstoul(p, &end, 10);
        if (*end == L'h') value *= 3600; else if (*end == L'm') value *= 60; else if (*end != L's') return 0;
        d->seconds += (unsigned)value;
        for (p = end + 1; *p == L' '; ++p) ;
    }
    if (!units || *p != L',') return 0;
    for (++p; *p == L' '; ++p) ;
    d->sessions = (unsigned)wcstoul(p, &end, 10);
    return end != p && !wcsncmp(end, L" session", 8);
}
long loggy_timer_seconds(const wchar_t *timer) {
    wchar_t *end;
    unsigned long h, m, s;
    if (!timer || !iswdigit(*timer)) return -1;
    h = wcstoul(timer, &end, 10);
    if (*end != L':' || !iswdigit(end[1]) || !iswdigit(end[2]) || end[3] != L':') return -1;
    m = wcstoul(end + 1, &end, 10);
    if (!iswdigit(end[1]) || !iswdigit(end[2]) || end[3]) return -1;
    s = wcstoul(end + 1, &end, 10);
    return m < 60 && s < 60 ? (long)(h * 3600 + m * 60 + s) : -1;
}

/* ---------- reading the window (no input) ---------- */

typedef struct { HWND found; unsigned count; } WindowSearch;
static BOOL CALLBACK find_window(HWND window, LPARAM parameter) {
    WindowSearch *search = (WindowSearch *)parameter;
    wchar_t title[256];
    if (GetWindow(window, GW_OWNER) || !GetWindowTextW(window, title, 256) || !loggy_title_matches(title)) return TRUE;
    if (window_process_is(window, L"loggy3-gui.exe") || window_process_is(window, L"loggy3.exe")) {
        search->found = window; ++search->count;
    }
    return TRUE;
}
HWND loggy_window(unsigned *count) {
    WindowSearch search = {NULL, 0};
    EnumWindows(find_window, (LPARAM)&search);
    if (count) *count = search.count;
    return search.count == 1 ? search.found : NULL;
}
static int timer_like(const wchar_t *text) {
    unsigned digits = 0;
    if (!wcscmp(text, L"--:--:--")) return 1;
    while (iswdigit(text[digits])) ++digits;
    return digits >= 1 && digits <= 3 && text[digits] == L':' && iswdigit(text[digits + 1]) && iswdigit(text[digits + 2]) &&
        text[digits + 3] == L':' && iswdigit(text[digits + 4]) && iswdigit(text[digits + 5]) && !text[digits + 6];
}
static void copy_bstr(wchar_t *out, size_t capacity, BSTR text) {
    wcsncpy(out, text ? text : L"", capacity - 1);
    out[capacity - 1] = 0;
}
/* The timer text next to the recording button, inside the bottom bar. */
static void read_timer(IUIAutomation *automation, IUIAutomationElement *button, wchar_t *timer) {
    IUIAutomationTreeWalker *walker = NULL;
    IUIAutomationElement *parent = NULL, *next = NULL;
    unsigned depth;
    if (FAILED(IUIAutomation_get_ControlViewWalker(automation, &walker))) return;
    if (FAILED(IUIAutomationTreeWalker_GetParentElement(walker, button, &parent))) parent = NULL;
    for (depth = 0; parent && depth < 5; ++depth) {
        BSTR cls = NULL;
        int bar = SUCCEEDED(IUIAutomationElement_get_CurrentClassName(parent, &cls)) && loggy_class_has(cls, L"bottom-bar");
        SysFreeString(cls);
        if (bar) {
            IUIAutomationCondition *condition = NULL;
            IUIAutomationElementArray *texts = NULL;
            VARIANT type;
            int count = 0, i;
            VariantInit(&type); type.vt = VT_I4; type.lVal = UIA_TextControlTypeId;
            if (SUCCEEDED(IUIAutomation_CreatePropertyCondition(automation, UIA_ControlTypePropertyId, type, &condition)) &&
                SUCCEEDED(IUIAutomationElement_FindAll(parent, TreeScope_Descendants, condition, &texts)) &&
                SUCCEEDED(IUIAutomationElementArray_get_Length(texts, &count)))
                for (i = 0; i < count && !*timer; ++i) {
                    IUIAutomationElement *text = NULL;
                    BSTR name = NULL;
                    if (FAILED(IUIAutomationElementArray_GetElement(texts, i, &text))) continue;
                    if (SUCCEEDED(IUIAutomationElement_get_CurrentName(text, &name)) && name && timer_like(name))
                        copy_bstr(timer, 16, name);
                    SysFreeString(name);
                    IUIAutomationElement_Release(text);
                }
            if (texts) IUIAutomationElementArray_Release(texts);
            if (condition) IUIAutomationCondition_Release(condition);
            break;
        }
        if (FAILED(IUIAutomationTreeWalker_GetParentElement(walker, parent, &next))) next = NULL;
        IUIAutomationElement_Release(parent);
        parent = next;
    }
    if (parent) IUIAutomationElement_Release(parent);
    IUIAutomationTreeWalker_Release(walker);
}
/* Works while Loggy3 is minimized: its web UI stays in the accessibility tree. */
int loggy_snapshot(HWND window, LoggySnapshot *s) {
    IUIAutomation *automation = NULL;
    IUIAutomationElement *root = NULL, *only = NULL;
    IUIAutomationElementArray *buttons = NULL;
    IUIAutomationCondition *condition = NULL;
    VARIANT type;
    HRESULT initialized;
    int count = 0, i, ok = 0;
    memset(s, 0, sizeof(*s));
    if (!window || FAILED(initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) return 0;
    VariantInit(&type); type.vt = VT_I4; type.lVal = UIA_ButtonControlTypeId;
    if (FAILED(CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation, (void **)&automation)) ||
        FAILED(IUIAutomation_ElementFromHandle(automation, (UIA_HWND)window, &root)) ||
        FAILED(IUIAutomation_CreatePropertyCondition(automation, UIA_ControlTypePropertyId, type, &condition)) ||
        FAILED(IUIAutomationElement_FindAll(root, TreeScope_Descendants, condition, &buttons)) ||
        FAILED(IUIAutomationElementArray_get_Length(buttons, &count)) || count > 4096) goto done;
    for (i = 0; i < count; ++i) {
        IUIAutomationElement *element = NULL;
        BSTR cls = NULL;
        if (FAILED(IUIAutomationElementArray_GetElement(buttons, i, &element))) continue;
        if (SUCCEEDED(IUIAutomationElement_get_CurrentClassName(element, &cls)) && loggy_class_has(cls, L"btn-record")) {
            if (s->button_count < LOGGY_MAX_BUTTONS) {
                LoggyButton *b = &s->buttons[s->button_count];
                BSTR name = NULL;
                BOOL flag = FALSE;
                IUnknown *pattern = NULL;
                if (SUCCEEDED(IUIAutomationElement_get_CurrentName(element, &name))) copy_bstr(b->name, 64, name);
                SysFreeString(name);
                b->enabled = SUCCEEDED(IUIAutomationElement_get_CurrentIsEnabled(element, &flag)) && flag;
                flag = TRUE;
                IUIAutomationElement_get_CurrentIsOffscreen(element, &flag); b->offscreen = flag != FALSE;
                IUIAutomationElement_get_CurrentBoundingRectangle(element, &b->rect);
                if (SUCCEEDED(IUIAutomationElement_GetCurrentPattern(element, UIA_InvokePatternId, &pattern)) && pattern) {
                    b->invoke = 1; IUnknown_Release(pattern);
                }
                if (!s->button_count) { only = element; IUIAutomationElement_AddRef(only); }
            }
            ++s->button_count;
        }
        SysFreeString(cls);
        IUIAutomationElement_Release(element);
    }
    if (s->button_count == 1 && only) read_timer(automation, only, s->timer);
    ok = 1;
done:
    if (only) IUIAutomationElement_Release(only);
    if (buttons) IUIAutomationElementArray_Release(buttons);
    if (condition) IUIAutomationCondition_Release(condition);
    if (root) IUIAutomationElement_Release(root);
    if (automation) IUIAutomation_Release(automation);
    CoUninitialize();
    return ok;
}
/* The Sessions page's row for one UTC date: a button with class sessions-day. */
static int find_day(HWND window, const SYSTEMTIME *utc, LoggyDay *found) {
    IUIAutomation *automation = NULL;
    IUIAutomationElement *root = NULL;
    IUIAutomationElementArray *buttons = NULL;
    IUIAutomationCondition *condition = NULL;
    VARIANT type;
    int count = 0, i, matched = 0;
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) return 0;
    VariantInit(&type); type.vt = VT_I4; type.lVal = UIA_ButtonControlTypeId;
    if (SUCCEEDED(CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation, (void **)&automation)) &&
        SUCCEEDED(IUIAutomation_ElementFromHandle(automation, (UIA_HWND)window, &root)) &&
        SUCCEEDED(IUIAutomation_CreatePropertyCondition(automation, UIA_ControlTypePropertyId, type, &condition)) &&
        SUCCEEDED(IUIAutomationElement_FindAll(root, TreeScope_Descendants, condition, &buttons)) &&
        SUCCEEDED(IUIAutomationElementArray_get_Length(buttons, &count)) && count <= 4096)
        for (i = 0; i < count && !matched; ++i) {
            IUIAutomationElement *element = NULL;
            BSTR cls = NULL, name = NULL;
            LoggyDay day;
            if (FAILED(IUIAutomationElementArray_GetElement(buttons, i, &element))) continue;
            if (SUCCEEDED(IUIAutomationElement_get_CurrentClassName(element, &cls)) && loggy_class_has(cls, L"sessions-day") &&
                SUCCEEDED(IUIAutomationElement_get_CurrentName(element, &name)) && loggy_parse_day(name, &day) &&
                day.year == utc->wYear && day.month == utc->wMonth && day.day == utc->wDay) { *found = day; matched = 1; }
            SysFreeString(cls); SysFreeString(name);
            IUIAutomationElement_Release(element);
        }
    if (buttons) IUIAutomationElementArray_Release(buttons);
    if (condition) IUIAutomationCondition_Release(condition);
    if (root) IUIAutomationElement_Release(root);
    if (automation) IUIAutomation_Release(automation);
    CoUninitialize();
    return matched;
}
int loggy_today(HWND window, const SYSTEMTIME *utc, LoggyToday *today) {
    LoggySnapshot s;
    SYSTEMTIME now;
    memset(today, 0, sizeof(*today));
    today->timer_seconds = -1;
    if (!utc) { GetSystemTime(&now); utc = &now; }
    if (!loggy_snapshot(window, &s)) return 0;
    today->state = loggy_recording_state(&s, NULL);
    today->timer_seconds = loggy_timer_seconds(s.timer);
    today->found = find_day(window, utc, &today->day);
    return 1;
}
long loggy_today_seconds(const LoggyToday *t) {
    if (!t->found) return -1;
    return (long)t->day.seconds + (t->state == LOGGY_RECORDING && t->timer_seconds > 0 ? t->timer_seconds : 0);
}
/* Whether the element under the pointer is (inside) the recording button with this label. */
static int record_button_at(POINT point, const wchar_t *expected) {
    IUIAutomation *automation = NULL;
    IUIAutomationTreeWalker *walker = NULL;
    IUIAutomationElement *element = NULL, *next = NULL;
    unsigned depth;
    int found = 0;
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) return 0;
    if (SUCCEEDED(CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation, (void **)&automation)) &&
        SUCCEEDED(IUIAutomation_get_ControlViewWalker(automation, &walker)) &&
        SUCCEEDED(IUIAutomation_ElementFromPoint(automation, point, &element)))
        for (depth = 0; element && depth < 30 && !found; ++depth) {
            BSTR cls = NULL, name = NULL;
            found = SUCCEEDED(IUIAutomationElement_get_CurrentClassName(element, &cls)) && loggy_class_has(cls, L"btn-record") &&
                SUCCEEDED(IUIAutomationElement_get_CurrentName(element, &name)) && name && !wcscmp(name, expected);
            SysFreeString(cls); SysFreeString(name);
            if (FAILED(IUIAutomationTreeWalker_GetParentElement(walker, element, &next))) next = NULL;
            IUIAutomationElement_Release(element);
            element = next;
        }
    if (element) IUIAutomationElement_Release(element);
    if (walker) IUIAutomationTreeWalker_Release(walker);
    if (automation) IUIAutomation_Release(automation);
    CoUninitialize();
    return found;
}

/* ---------- acting on the window ---------- */

static int click_rect(Playback *p, MotionRng *rng, const RECT *r) {
    POINT point;
    if (!motion_click_point(rng, r, &point) || !mouse_move_human(p, rng, point, MOTION_AIM, r->bottom - r->top) ||
        !playback_wait(p, motion_between(rng, 80, 300))) return 0;
    return mouse_click(p, 0, 0);
}
static void log_window(Playback *p, HWND w) {
    if (!w) { playback_log(p, "Loggy3 window: not open"); return; }
    playback_log(p, "Loggy3 window: %s%s%s", !IsWindowVisible(w) ? "hidden" : IsIconic(w) ? "minimized" : IsZoomed(w) ? "maximized" : "normal",
        GetForegroundWindow() == w ? ", in front" : "", IsWindowEnabled(w) ? "" : ", a dialog is open");
}
/* The single main window; reports when several match. */
static HWND main_window(Playback *p) {
    unsigned count;
    HWND w = loggy_window(&count);
    if (count > 1) playback_log(p, "%u Loggy3 main windows matched; close the extra ones", count);
    return w;
}
static int wait_until(Playback *p, int (*ready)(HWND), HWND *w, DWORD timeout_ms) {
    DWORD waited;
    for (waited = 0; waited <= timeout_ms; waited += 150) {
        *w = loggy_window(NULL);
        if (*w && ready(*w)) return 1;
        if (!playback_wait(p, 150)) return -1;
    }
    return 0;
}
static int shown_in_front(HWND w) { return IsWindowVisible(w) && !IsIconic(w) && GetForegroundWindow() == w; }
/* A title-bar button (Maximize, Minimize, Close) by its exact accessible name. */
static int title_button(Playback *p, MotionRng *rng, const wchar_t *name) {
    RECT r;
    char label[32];
    if (!ui_find_control_rect(p, UIA_ButtonControlTypeId, name, &r)) return 2;
    WideCharToMultiByte(CP_UTF8, 0, name, -1, label, sizeof(label), NULL, NULL);
    playback_log(p, "clicking Loggy3's %s button", label);
    return click_rect(p, rng, &r) ? 1 : 0;
}
/* Bring Loggy3 to the front with its taskbar icon (the Start menu when it has none), then maximize.
   With look_around, the pointer then rests over the window for a moment; without it, the caller
   heads straight for the button it came for. */
static int loggy_show(Playback *p, MotionRng *rng, int look_around) {
    HWND w = main_window(p);
    RECT area;
    POINT rest;
    int clicked, ready;
    log_window(p, w);
    if (w && shown_in_front(w)) playback_log(p, "Loggy3 is already in front; no taskbar click");
    else {
        playback_log(p, "showing Loggy3 from its taskbar icon");
        if (!(clicked = taskbar_icon_click(p, rng, L"Loggy3", TASKBAR_LEFT))) return 0;
        if (clicked == 2) {
            playback_log(p, "Loggy3 has no taskbar icon; opening it from the Start menu");
            if ((clicked = start_menu_open(p, rng, L"Loggy3")) != 1) {
                if (clicked) playback_log(p, "the Start menu could not open Loggy3");
                return 0;
            }
        }
        if ((ready = wait_until(p, shown_in_front, &w, 8000)) < 0) return 0;
        if (!ready) {
            /* The click showed it behind something, or a preview: bring it forward directly. */
            if (!(w = loggy_window(NULL)) || !IsWindowVisible(w)) { playback_log(p, "Loggy3 did not show a main window"); return 0; }
            playback_log(p, "Loggy3 did not come to the front from the click; activating it");
            if (!window_activate_handle(p, w)) return 0;
        }
    }
    p->target = w;
    if (!IsWindowEnabled(w)) { playback_log(p, "Loggy3 has a dialog open; close it first"); return 0; }
    if (!IsZoomed(w)) {
        int result = title_button(p, rng, L"Maximize");
        DWORD waited;
        if (!result) return 0;
        for (waited = 0; result == 1 && waited < 3000 && !IsZoomed(w); waited += 150) if (!playback_wait(p, 150)) return 0;
        if (!IsZoomed(w)) {
            if (!(GetWindowLongW(w, GWL_STYLE) & WS_MAXIMIZEBOX)) { playback_log(p, "Loggy3's window cannot be maximized; leaving it as is"); }
            else {
                playback_log(p, "no Maximize button responded; maximizing through Windows");
                ShowWindow(w, SW_MAXIMIZE);
                for (waited = 0; waited < 3000 && !IsZoomed(w); waited += 150) if (!playback_wait(p, 150)) return 0;
            }
        }
    }
    if (!shown_in_front(w)) { playback_log(p, "Loggy3 is not visible and in front"); return 0; }
    playback_log(p, "Loggy3 is in front%s", IsZoomed(w) ? " and maximized" : "");
    if (!look_around) return playback_wait(p, motion_between(rng, 200, 600));
    /* Let go of the pointer somewhere over the window, like a person reading it. */
    if (GetWindowRect(w, &area)) {
        area.top += 60;
        if (motion_rest_point(rng, &area, &rest) && !mouse_move_human(p, rng, rest, MOTION_REST, 160)) return 0;
    }
    return playback_wait(p, motion_between(rng, 600, 1400));
}
/* Someone who opens Loggy3 to press a button sometimes goes straight for it, and sometimes
   looks over the window first. Half and half for now (see TRACKER.md D4). */
static int look_first(Playback *p, MotionRng *rng, const char *button) {
    int look = motion_chance(rng, 50);
    playback_log(p, look ? "looking over the window before going to \"%s\"" : "heading straight for \"%s\"", button);
    return look;
}
static int minimized(HWND w) { return IsIconic(w) || !IsWindowVisible(w); }
static int loggy_minimize(Playback *p, MotionRng *rng) {
    HWND w = main_window(p);
    int ready;
    if (!w) { playback_log(p, "Loggy3 is not open; nothing to minimize"); return 1; }
    if (minimized(w)) { playback_log(p, "Loggy3 is already minimized"); return 1; }
    if (!IsWindowEnabled(w)) { playback_log(p, "Loggy3 has a dialog open; close it first"); return 0; }
    if (GetForegroundWindow() == w) {
        int result;
        p->target = w;
        if (!(result = title_button(p, rng, L"Minimize"))) return 0;
        if (result == 1 && (ready = wait_until(p, minimized, &w, 3000))) {
            if (ready < 0) return 0;
            playback_log(p, "Loggy3 is minimized; it is still running");
            return 1;
        }
        playback_log(p, "no Minimize button responded; minimizing through Windows");
    } else playback_log(p, "Loggy3 is behind other windows; minimizing it through Windows");
    ShowWindowAsync(w, SW_MINIMIZE);
    if ((ready = wait_until(p, minimized, &w, 5000)) <= 0) { if (!ready) playback_log(p, "Loggy3 did not minimize"); return 0; }
    playback_log(p, "Loggy3 is minimized; it is still running");
    return 1;
}
static int read_state(Playback *p, HWND w, LoggySnapshot *s) {
    DWORD waited;
    /* The web UI can be mid-render; give it a few seconds to show a known label. */
    for (waited = 0; waited <= 10000; waited += 300) {
        if (!loggy_snapshot(w, s)) { playback_log(p, "Loggy3's accessibility tree could not be read"); return 0; }
        if (loggy_recording_state(s, NULL) != LOGGY_UNKNOWN) return 1;
        if (!playback_wait(p, 300)) return 0;
    }
    return 1;
}
static void log_state(Playback *p, const LoggySnapshot *s) {
    unsigned index = 0;
    LoggyState state = loggy_recording_state(s, &index);
    char label[64] = "", timer[16] = "";
    if (state != LOGGY_UNKNOWN || s->button_count == 1)
        WideCharToMultiByte(CP_UTF8, 0, s->buttons[index].name, -1, label, sizeof(label), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, s->timer, -1, timer, sizeof(timer), NULL, NULL);
    playback_log(p, "recording: %s (button \"%s\", timer %s, %u recording button(s))", loggy_state_name(state), label,
        *timer ? timer : "not found", s->button_count);
}
static void hms(unsigned long seconds, char *out, size_t capacity) {
    snprintf(out, capacity, "%luh %02lum %02lus", seconds / 3600, seconds / 60 % 60, seconds % 60);
}
static void log_today(Playback *p, HWND w) {
    LoggyToday today;
    SYSTEMTIME utc;
    char total[32], running[32], recorded[32];
    GetSystemTime(&utc);
    if (!loggy_today(w, &utc, &today)) { playback_log(p, "today: Loggy3 could not be read"); return; }
    if (!today.found) {
        playback_log(p, "today (%04u-%02u-%02u UTC): no row on screen; open Loggy3's Sessions page on the current period",
            utc.wYear, utc.wMonth, utc.wDay);
        return;
    }
    hms(today.day.seconds, total, sizeof(total));
    if (today.timer_seconds >= 0) hms((unsigned long)today.timer_seconds, running, sizeof(running));
    hms((unsigned long)loggy_today_seconds(&today), recorded, sizeof(recorded));
    playback_log(p, "today (%04u-%02u-%02u UTC): %s recorded (%s in %u finished session(s)%s%s)", utc.wYear, utc.wMonth, utc.wDay,
        recorded, total, today.day.sessions, today.timer_seconds >= 0 ? " + running " : "", today.timer_seconds >= 0 ? running : "");
}
static int loggy_close(Playback *p, MotionRng *rng) {
    HWND w = main_window(p);
    LoggySnapshot s;
    LoggyState state;
    int result, gone;
    if (!w) { playback_log(p, "Loggy3 is not open; nothing to close"); return 1; }
    if (!read_state(p, w, &s)) return 0;
    log_state(p, &s);
    state = loggy_recording_state(&s, NULL);
    if (state == LOGGY_RECORDING || state == LOGGY_BUSY) {
        playback_log(p, "Loggy3 is recording (or changing state); stop the recording before closing it"); return 0;
    }
    if (!loggy_show(p, rng, look_first(p, rng, "Close"))) return 0;
    w = p->target;
    if (!(result = title_button(p, rng, L"Close"))) return 0;
    if (result == 2) {
        WORD keys[2] = {VK_MENU, VK_F4};
        playback_log(p, "no Close button found; closing with Alt+F4");
        if (!keyboard_shortcut(p, keys, 2)) return 0;
    }
    for (gone = 0; gone < 10000 && IsWindow(w) && IsWindowVisible(w); gone += 150) if (!playback_wait(p, 150)) return 0;
    if (IsWindow(w) && IsWindowVisible(w)) { playback_log(p, "Loggy3's window did not close"); return 0; }
    playback_log(p, IsWindow(w) ? "Loggy3's window is closed; the app is still running in the tray" : "Loggy3 is closed");
    p->target = GetForegroundWindow();
    return 1;
}
/* Status, Start, or Stop. The button is pressed at most once and never retried. */
static int loggy_recording(Playback *p, MotionRng *rng, LoggyJob *job) {
    HWND w = main_window(p);
    LoggySnapshot s;
    LoggyPlan plan;
    unsigned index = 0;
    POINT point, actual;
    char expected[64];
    DWORD waited;
    if (!w) { playback_log(p, "Loggy3 is not open"); return 0; }
    log_window(p, w);
    if (!read_state(p, w, &s)) return 0;
    log_state(p, &s);
    job->result = loggy_recording_state(&s, NULL);
    if (!loggy_plan(job->action, &s, &plan)) { playback_log(p, "%s", plan.reason); return 0; }
    if (job->action == LOGGY_STATUS) {
        log_today(p, w);
        return job->result == LOGGY_STOPPED || job->result == LOGGY_RECORDING;
    }
    if (!plan.press) { playback_log(p, "%s", plan.reason); return 1; }
    WideCharToMultiByte(CP_UTF8, 0, plan.expected, -1, expected, sizeof(expected), NULL, NULL);
    if (job->dry_run) { playback_log(p, "dry run: would press \"%s\" once (%s); nothing pressed", expected, plan.reason); return 1; }
    if (!loggy_show(p, rng, look_first(p, rng, expected))) return 0;
    w = p->target;
    if (job->sessions_page) {
        /* Today's total is read from the Sessions page; open it if Loggy3 is showing another page. */
        SYSTEMTIME utc;
        LoggyDay day;
        GetSystemTime(&utc);
        if (!find_day(w, &utc, &day)) {
            int result = title_button(p, rng, L"Sessions");
            if (!result) return 0;
            if (result == 2) playback_log(p, "Loggy3's Sessions page button was not found");
            else if (!playback_wait(p, motion_between(rng, 900, 1600))) return 0;
        }
    }
    /* Read again now that it is in front: the label must be unchanged. */
    if (!read_state(p, w, &s) || loggy_recording_state(&s, &index) != plan.state || wcscmp(s.buttons[index].name, plan.expected)) {
        playback_log(p, "the recording button changed before it could be pressed; nothing pressed"); return 0;
    }
    if (!motion_click_point(rng, &s.buttons[index].rect, &point) ||
        !mouse_move_human(p, rng, point, MOTION_AIM, s.buttons[index].rect.bottom - s.buttons[index].rect.top)) return 0;
    if (!GetCursorPos(&actual) || !record_button_at(actual, plan.expected)) {
        playback_log(p, "something else is under the pointer instead of the \"%s\" button; nothing pressed", expected); return 0;
    }
    if (job->hover_only) {
        playback_log(p, "hover only: resting on \"%s\" for %lu ms without clicking", expected, job->hover_ms);
        if (!playback_wait(p, job->hover_ms) || !read_state(p, w, &s)) return 0;
        log_state(p, &s);
        job->result = loggy_recording_state(&s, NULL);
        if (job->result != plan.state) { playback_log(p, "the recording state changed during the hover"); return 0; }
        return 1;
    }
    if (!playback_wait(p, motion_between(rng, 80, 300)) || !GetCursorPos(&actual) || !record_button_at(actual, plan.expected)) {
        playback_log(p, "the \"%s\" button moved away from the pointer; nothing pressed", expected); return 0;
    }
    playback_log(p, "pressing \"%s\" once", expected);
    if (!mouse_click(p, 0, 0)) return 0;
    for (waited = 0; waited < 20000; waited += 350) {
        if (!playback_wait(p, 350) || !loggy_snapshot(w, &s)) return 0;
        if ((job->result = loggy_recording_state(&s, NULL)) == plan.desired) {
            log_state(p, &s);
            playback_log(p, "Loggy3 is now %s", loggy_state_name(plan.desired));
            return 1;
        }
    }
    log_state(p, &s);
    playback_log(p, "Loggy3 did not confirm %s within 20 s; check it before trying again (no second click was sent)",
        loggy_state_name(plan.desired));
    return 0;
}
int loggy_open_sessions(Playback *p, MotionRng *rng) {
    SYSTEMTIME utc;
    LoggyDay day;
    int result;
    p->client_coords = 0;
    if (!loggy_show(p, rng, 0)) return 0;
    GetSystemTime(&utc);
    if (find_day(p->target, &utc, &day)) return 1;
    if (!(result = title_button(p, rng, L"Sessions"))) return 0;
    if (result == 2) { playback_log(p, "Loggy3's Sessions page button was not found"); return 1; }
    return playback_wait(p, motion_between(rng, 900, 1600));
}
int loggy_job(Playback *p, LoggyJob *job) {
    MotionRng rng;
    int ok;
    motion_seed(&rng, job->seed);
    if (job->profile) {
        p->typing_speed = job->profile->typing_speed;
        p->typo_percent = profile_mistype_percent(job->profile);
        p->typo_state = job->seed ^ 0x9e3779b9u;
        motion_hand_for(&p->hand, motion_hash(job->profile->local_id), job->profile->randomness_score);
    }
    /* Screen coordinates: the title-bar buttons are outside the client area. */
    p->client_coords = 0;
    if (!window_usable(p->target)) p->target = GetForegroundWindow();
    window_fix_target(p);
    job->result = LOGGY_UNKNOWN;
    switch (job->action) {
    case LOGGY_STATUS: case LOGGY_RECORD: case LOGGY_STOP: ok = loggy_recording(p, &rng, job); break;
    case LOGGY_SHOW: ok = job->dry_run ? (log_window(p, main_window(p)), 1) : loggy_show(p, &rng, 1); break;
    case LOGGY_MINIMIZE: ok = job->dry_run ? (log_window(p, main_window(p)), 1) : loggy_minimize(p, &rng); break;
    case LOGGY_CLOSE: ok = job->dry_run ? (log_window(p, main_window(p)), 1) : loggy_close(p, &rng); break;
    case LOGGY_DEMO:
        if (job->dry_run) { log_window(p, main_window(p)); ok = 1; break; }
        ok = loggy_show(p, &rng, 1);
        if (ok) { playback_log(p, "keeping Loggy3 in front for %lu ms", job->hold_ms); ok = playback_wait(p, job->hold_ms) && loggy_minimize(p, &rng); }
        break;
    default: ok = 0;
    }
    if (ok && job->minimize_after && !job->dry_run && job->action != LOGGY_MINIMIZE && job->action != LOGGY_CLOSE &&
        job->action != LOGGY_DEMO) ok = loggy_minimize(p, &rng);
    return ok;
}
