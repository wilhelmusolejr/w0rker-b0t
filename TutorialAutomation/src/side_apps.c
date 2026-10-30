#include "side_apps.h"
#include "coding_steps.h"
#include "json_config.h"
#include "keyboard.h"
#include "mouse.h"
#include "presentation.h"
#include "script_engine.h"
#include "ui_targets.h"
#include "window_manager.h"
#include <uiautomationclient.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const wchar_t *const app_keys[APP_COUNT] = {L"notepad", L"explorer", L"calculator", L"images"};

/* ---------- settings ---------- */

static int fail(char *error, size_t cap, const char *message) { snprintf(error, cap, "%s", message); return 0; }
static int range(const JsonValue *parent, const wchar_t *key, unsigned lo, unsigned hi, unsigned out[2]) {
    const JsonValue *v = json_get(parent, key), *a;
    if (!v || v->kind != JSON_ARRAY || !(a = v->child) || !a->next || a->next->next ||
        a->kind != JSON_NUMBER || a->next->kind != JSON_NUMBER) return 0;
    out[0] = a->number; out[1] = a->next->number;
    return out[0] >= lo && out[1] <= hi && out[0] <= out[1];
}
static int percent(const JsonValue *parent, const wchar_t *key, unsigned *out) {
    const JsonValue *v = json_get(parent, key);
    if (!v || v->kind != JSON_NUMBER || v->number > 100) return 0;
    *out = v->number; return 1;
}
static int strings(const JsonValue *v, wchar_t **out, size_t *count, size_t max_length) {
    const JsonValue *item;
    if (!v || v->kind != JSON_ARRAY || !v->child) return 0;
    for (item = v->child; item; item = item->next) {
        if (*count == SIDE_APPS_MAX_ITEMS || item->kind != JSON_STRING || !*item->string || wcslen(item->string) > max_length) return 0;
        if (!(out[(*count)++] = _wcsdup(item->string))) return 0;
    }
    return 1;
}
int side_apps_parse(const wchar_t *text, SideAppsConfig *c, char *error, size_t cap) {
    static const wchar_t *const keys[] = {L"version", L"chance_percent", L"check_every_minutes", L"minutes",
        L"leave_open_percent", L"apps", L"notes_folder", L"folders", L"image_folders", L"notes"};
    JsonValue *root = NULL;
    const JsonValue *v, *apps;
    unsigned total = 0, i;
    memset(c, 0, sizeof(*c));
    if (!cap) return 0;
    *error = 0;
    if (!json_parse(text, &root, error, cap)) return 0;
    v = json_get(root, L"version"); apps = json_get(root, L"apps");
    if (!json_keys(root, keys, sizeof(keys)/sizeof(*keys)) || !v || v->kind != JSON_NUMBER || v->number != 1) {
        fail(error, cap, "apps settings need \"version\": 1 and only documented fields"); goto fail;
    }
    if (!percent(root, L"chance_percent", &c->chance_percent) || !range(root, L"check_every_minutes", 1, 600, c->check_minutes) ||
        !range(root, L"minutes", 1, 240, c->minutes) || !percent(root, L"leave_open_percent", &c->leave_open_percent)) {
        fail(error, cap, "apps settings: chance_percent and leave_open_percent 0-100, check_every_minutes and minutes [min, max]"); goto fail;
    }
    if (!json_keys(apps, app_keys, APP_COUNT)) { fail(error, cap, "apps are notepad, explorer, calculator and images"); goto fail; }
    for (i = 0; i < APP_COUNT; ++i) {
        const JsonValue *w = json_get(apps, app_keys[i]);
        if (w && (w->kind != JSON_NUMBER || w->number > 100)) { fail(error, cap, "app weights must be 0-100"); goto fail; }
        total += c->weights[i] = w ? w->number : 0;
    }
    if (!total) { fail(error, cap, "at least one app needs a weight above 0"); goto fail; }
    v = json_get(root, L"notes_folder");
    if (!v || v->kind != JSON_STRING || !*v->string || wcslen(v->string) >= MAX_PATH - 60) { fail(error, cap, "notes_folder is required"); goto fail; }
    wcscpy(c->notes_folder, v->string);
    if (!strings(json_get(root, L"folders"), c->folders, &c->folder_count, 200) ||
        !strings(json_get(root, L"image_folders"), c->image_folders, &c->image_folder_count, 200) ||
        !strings(json_get(root, L"notes"), c->notes, &c->note_count, 2000)) {
        fail(error, cap, "folders, image_folders and notes must be non-empty lists (up to 32)"); goto fail;
    }
    json_free(root); return 1;
fail:
    json_free(root); side_apps_free(c); return 0;
}
int side_apps_load(const wchar_t *path, SideAppsConfig *c, char *error, size_t cap) {
    wchar_t *text = NULL;
    int ok;
    memset(c, 0, sizeof(*c));
    if (!read_utf8(path, &text, error, cap)) return 0;
    ok = side_apps_parse(text, c, error, cap);
    free(text); return ok;
}
void side_apps_free(SideAppsConfig *c) {
    size_t i;
    for (i = 0; i < c->note_count; ++i) free(c->notes[i]);
    for (i = 0; i < c->folder_count; ++i) free(c->folders[i]);
    for (i = 0; i < c->image_folder_count; ++i) free(c->image_folders[i]);
    c->note_count = c->folder_count = c->image_folder_count = 0;
}
const char *side_app_name(SideApp app) {
    static const char *const names[APP_COUNT] = {"Notepad", "File Explorer", "Calculator", "image viewing"};
    return (unsigned)app < APP_COUNT ? names[app] : "unknown";
}
int side_app_from_name(const wchar_t *name, SideApp *app) {
    unsigned i;
    for (i = 0; i < APP_COUNT; ++i) if (!_wcsicmp(name, app_keys[i])) { *app = (SideApp)i; return 1; }
    return 0;
}
SideApp side_apps_pick(const SideAppsConfig *c, MotionRng *rng) {
    unsigned total = 0, roll, i;
    for (i = 0; i < APP_COUNT; ++i) total += c->weights[i];
    roll = motion_between(rng, 0, total - 1);
    for (i = 0; i < APP_COUNT - 1 && roll >= c->weights[i]; ++i) roll -= c->weights[i];
    return (SideApp)i;
}
static const wchar_t *pick_text(wchar_t *const *list, size_t count, MotionRng *rng, const wchar_t *fallback) {
    return count ? list[motion_between(rng, 0, (unsigned)count - 1)] : fallback;
}
int side_apps_note(const SideAppsConfig *c, const Profile *profile, MotionRng *rng, wchar_t *out, size_t cap) {
    const wchar_t *at = c->notes[motion_between(rng, 0, (unsigned)c->note_count - 1)];
    size_t used = 0;
    while (*at) {
        const wchar_t *insert = NULL;
        size_t skip = 1, n;
        if (!wcsncmp(at, L"{interest}", 10)) { insert = pick_text(profile->interests, profile->interest_count, rng, L"programming"); skip = 10; }
        else if (!wcsncmp(at, L"{hobby}", 7)) { insert = pick_text(profile->hobbies, profile->hobby_count, rng, L"reading"); skip = 7; }
        if (insert) {
            n = wcslen(insert);
            if (used + n >= cap) return 0;
            wmemcpy(out + used, insert, n); used += n;
        } else {
            if (used + 1 >= cap) return 0;
            out[used++] = *at;
        }
        at += skip;
    }
    out[used] = 0;
    return used > 0;
}
int side_apps_folder(const wchar_t *token, const Profile *profile, wchar_t out[MAX_PATH]) {
    static const struct { const wchar_t *name; int csidl; const wchar_t *suffix; } known[] = {
        {L"Desktop", CSIDL_DESKTOPDIRECTORY, L""}, {L"Documents", CSIDL_PERSONAL, L""}, {L"Pictures", CSIDL_MYPICTURES, L""},
        {L"Music", CSIDL_MYMUSIC, L""}, {L"Videos", CSIDL_MYVIDEO, L""}, {L"Home", CSIDL_PROFILE, L""},
        {L"Downloads", CSIDL_PROFILE, L"\\Downloads"}
    };
    wchar_t base[MAX_PATH];
    size_t i, n = wcscspn(token, L"\\");
    const wchar_t *rest = token + n;
    if (wcsstr(token, L"..")) return 0;
    if (n == 8 && !_wcsnicmp(token, L"Projects", 8)) {
        if (!profile || !profile_project_root(profile, base, MAX_PATH)) return 0;
        return swprintf(out, MAX_PATH, L"%ls%ls", base, rest) > 0;
    }
    for (i = 0; i < sizeof(known)/sizeof(*known); ++i)
        if (wcslen(known[i].name) == n && !_wcsnicmp(token, known[i].name, n)) {
            if (FAILED(SHGetFolderPathW(NULL, known[i].csidl, NULL, SHGFP_TYPE_CURRENT, base))) return 0;
            return swprintf(out, MAX_PATH, L"%ls%ls%ls", base, known[i].suffix, rest) > 0;
        }
    if (((token[0] >= L'A' && token[0] <= L'Z') || (token[0] >= L'a' && token[0] <= L'z')) && token[1] == L':' && token[2] == L'\\') {
        wcsncpy(out, token, MAX_PATH - 1); out[MAX_PATH - 1] = 0; return 1;
    }
    return 0;
}
static int is_folder(const wchar_t *path) {
    DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

/* ---------- small actions ---------- */

static int shortcut(Playback *p, WORD a, WORD b) {
    WORD keys[2];
    keys[0] = a; keys[1] = b;
    return keyboard_shortcut(p, keys, 2);
}
static int move_to(Playback *p, MotionRng *rng, POINT target, int purpose, double width) {
    return mouse_move_human(p, rng, target, purpose, width);
}
static int click_rect(Playback *p, MotionRng *rng, const RECT *r, int twice) {
    POINT point;
    if (!motion_click_point(rng, r, &point) || !move_to(p, rng, point, MOTION_AIM, r->bottom - r->top) ||
        !playback_wait(p, motion_between(rng, 80, 300))) return 0;
    return mouse_click(p, 0, twice);
}
static int window_area(Playback *p, RECT *area) {
    POINT origin = {0, 0};
    if (!GetClientRect(p->target, area) || !ClientToScreen(p->target, &origin)) return 0;
    OffsetRect(area, origin.x, origin.y);
    return area->right - area->left > 200 && area->bottom - area->top > 150;
}
/* Pause with the hands off, sometimes drifting the pointer over the window. */
static int linger(Playback *p, MotionRng *rng, unsigned low_ms, unsigned high_ms) {
    RECT area;
    if (motion_chance(rng, 30) && window_area(p, &area)) {
        POINT point;
        area.top += 60;
        if (motion_rest_point(rng, &area, &point) && !move_to(p, rng, point, MOTION_REST, 160)) return 0;
    }
    return playback_wait(p, motion_between(rng, low_ms, high_ms));
}
static DWORD left_ms(const Playback *p, ULONGLONG start, ULONGLONG paused, DWORD duration) {
    ULONGLONG used = GetTickCount64() - start - (p->paused_ms - paused);
    /* A work session is winding down, reached its target, or lost the recording: cut the break
       short. (A slot's own deadline does not: a break may run past the end of its slot.) */
    if (InterlockedCompareExchange((volatile LONG *)&p->wind_down, 0, 0) ||
        InterlockedCompareExchange((volatile LONG *)&p->yield, 0, 0)) return 0;
    return used >= duration ? 0 : (DWORD)(duration - used);
}
/* Windows Run (Win+R): the way the coding launcher opens programs too. */
static int run_command(Playback *p, const wchar_t *command) {
    return coding_step_win_key(p, L'R') && coding_step_wait_window(p, L"#32770", L"explorer.exe", L"Run", NULL) &&
        shortcut(p, VK_CONTROL, L'A') && keyboard_type(p, command) &&
        playback_wait(p, 300) && keyboard_press(p, VK_RETURN);
}
static int scroll_list(Playback *p, MotionRng *rng, int direction, unsigned notches);
/* Double-click a folder in File Explorer's main view, scrolling down to find it if needed.
   Returns 1 opened, 2 not found, 0 stop. */
static int double_click_item(Playback *p, MotionRng *rng, const wchar_t *name) {
    RECT item;
    unsigned scrolls;
    char shown[3 * MAX_PATH];
    for (scrolls = 0; !ui_find_control_rect(p, UIA_ListItemControlTypeId, name, &item); ++scrolls)
        if (scrolls == 4 || !scroll_list(p, rng, -1, motion_between(rng, 3, 6)) || !playback_wait(p, 500)) return scrolls == 4 ? 2 : 0;
    WideCharToMultiByte(CP_UTF8, 0, name, -1, shown, sizeof(shown), NULL, NULL);
    playback_log(p, "File Explorer: opening folder %s", shown);
    return click_rect(p, rng, &item, 1) && playback_wait(p, 1300) ? 1 : 0;
}
/* Go to a folder with the mouse only, the way a person browses: click its nearest entry in the
   sidebar (Desktop, Documents, Downloads, Pictures, Music, Videos), then double-click down through
   the subfolders. `current` becomes the folder reached. Returns 1 reached (or as far as the folders
   were visible), 2 when no sidebar entry leads there, 0 stop. */
static int browse_to(Playback *p, MotionRng *rng, const wchar_t *target, wchar_t current[MAX_PATH]) {
    static const wchar_t *const sidebar[] = {L"Desktop", L"Documents", L"Downloads", L"Pictures", L"Music", L"Videos"};
    wchar_t root[MAX_PATH], best_path[MAX_PATH] = L"", segment[MAX_PATH];
    const wchar_t *best = NULL, *rest;
    size_t i, length = 0;
    RECT entry;
    for (i = 0; i < sizeof(sidebar)/sizeof(*sidebar); ++i) {
        size_t n;
        if (!side_apps_folder(sidebar[i], NULL, root)) continue;
        n = wcslen(root);
        if (n > length && !_wcsnicmp(target, root, n) && (!target[n] || target[n] == L'\\')) { best = sidebar[i]; length = n; wcscpy(best_path, root); }
    }
    if (!best || !ui_explorer_sidebar_rect(p, best, &entry)) return 2;
    { char shown[64];
      WideCharToMultiByte(CP_UTF8, 0, best, -1, shown, sizeof(shown), NULL, NULL);
      playback_log(p, "File Explorer: clicking %s in the sidebar", shown); }
    if (!click_rect(p, rng, &entry, 0) || !playback_wait(p, 1500)) return 0;
    wcscpy(current, best_path);
    for (rest = target + length; *rest; ) {
        size_t n;
        int opened;
        while (*rest == L'\\') ++rest;
        if (!*rest) break;
        n = wcscspn(rest, L"\\");
        wmemcpy(segment, rest, n); segment[n] = 0;
        rest += n;
        if (!(opened = double_click_item(p, rng, segment))) return 0;
        if (opened == 2) { playback_log(p, "File Explorer: the next folder is not visible; staying here"); return 1; }
        if (wcslen(current) + n + 2 < MAX_PATH) { wcscat(current, L"\\"); wcscat(current, segment); }
    }
    return 1;
}
/* Back with the toolbar button, or Alt+Left; people use both. */
static int go_back(Playback *p, MotionRng *rng) {
    RECT button;
    if (motion_chance(rng, 55) && ui_window_control_rect(p, p->target, UIA_ButtonControlTypeId, L"Back", &button)) {
        playback_log(p, "File Explorer: Back button");
        return click_rect(p, rng, &button, 0) && playback_wait(p, 1000);
    }
    playback_log(p, "File Explorer: back (Alt+Left)");
    return shortcut(p, VK_MENU, VK_LEFT) && playback_wait(p, 1000);
}
/* Click a pinned taskbar icon with the mouse. The taskbar is not the window in front, so instead of
   the usual foreground check this confirms the taskbar is under the pointer before pressing.
   When the app already has windows open (its icon reads "<app> - N running windows"), a middle
   click opens a new window instead of showing thumbnails of the existing ones.
   Returns 1 clicked, 2 no such icon (or something covers it), 0 stop. */
int taskbar_icon_click(Playback *p, MotionRng *rng, const wchar_t *button, int mode) {
    HWND tray = FindWindowW(L"Shell_TrayWnd", NULL), hit;
    RECT r;
    POINT point, actual;
    INPUT input = {0};
    wchar_t running[128];
    int middle, right = mode == TASKBAR_MENU;
    swprintf(running, 128, L"%ls - ", button);
    if (!tray) return 2;
    /* The pointer moves under the guard of the window in front; it must be a real one. */
    if (!window_fix_target(p)) return 0;
    middle = mode == TASKBAR_NEW_WINDOW && ui_window_control_rect(p, tray, UIA_ButtonControlTypeId, running, &r);
    if (!middle && !ui_window_control_rect(p, tray, UIA_ButtonControlTypeId, button, &r)) return 2;
    if (middle) playback_log(p, "windows are already open; middle-clicking the icon for a new one");
    if (!motion_click_point(rng, &r, &point) || !move_to(p, rng, point, MOTION_AIM, r.bottom - r.top) ||
        !playback_wait(p, motion_between(rng, 120, 350)) || !playback_poll(p)) return 0;
    if (!GetCursorPos(&actual) || !(hit = WindowFromPoint(actual)) || GetAncestor(hit, GA_ROOT) != tray) return 2;
    if (mode == TASKBAR_NEXT_WINDOW) {
        INPUT ctrl;
        memset(&ctrl, 0, sizeof(ctrl));
        ctrl.type = INPUT_KEYBOARD; ctrl.ki.wVk = VK_CONTROL;
        if (!playback_send(p, &ctrl, 1)) return 0;
        p->held[VK_CONTROL] = 1;
        if (!playback_wait(p, motion_between(rng, 40, 110))) { playback_release(p); return 0; }
    }
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = middle ? MOUSEEVENTF_MIDDLEDOWN : right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
    if (!playback_send(p, &input, 1)) { playback_release(p); return 0; }
    p->mouse_held |= middle ? 4u : right ? 2u : 1u;
    if (!playback_wait(p, motion_between(rng, 50, 110))) { playback_release(p); return 0; }
    input.mi.dwFlags = middle ? MOUSEEVENTF_MIDDLEUP : right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
    if (!playback_send(p, &input, 1)) { playback_release(p); return 0; }
    p->mouse_held &= middle ? ~4u : right ? ~2u : ~1u;
    if (mode == TASKBAR_NEXT_WINDOW) {
        INPUT ctrl;
        memset(&ctrl, 0, sizeof(ctrl));
        ctrl.type = INPUT_KEYBOARD; ctrl.ki.wVk = VK_CONTROL; ctrl.ki.dwFlags = KEYEVENTF_KEYUP;
        Sleep(motion_between(rng, 30, 90));
        if (!playback_send(p, &ctrl, 1)) return 0;
        p->held[VK_CONTROL] = 0;
    }
    return 1;
}
/* Bring one exact window forward from its app's taskbar icon. A plain click switches to an app's
   only window; with several it shows thumbnails, which Windows does not expose to accessibility and
   which block switching. So the icon is Ctrl+clicked instead: each Ctrl+click brings the app's next
   window forward, until it is the one wanted (checked after every click). */
int taskbar_show_window(Playback *p, MotionRng *rng, const wchar_t *icon, HWND window) {
    unsigned tries;
    if (!window || !IsWindow(window)) return 0;
    if (GetForegroundWindow() == window && !IsIconic(window)) { p->target = window; return 1; }
    for (tries = 0; tries < 6 && GetForegroundWindow() != window; ++tries) {
        int clicked = taskbar_icon_click(p, rng, icon, TASKBAR_NEXT_WINDOW);
        if (!clicked) return 0;
        if (clicked == 2) break;
        if (tries == 1) playback_log(p, "several %ls windows: Ctrl+clicking until the right one is in front", icon);
        if (!playback_wait(p, motion_between(rng, 700, 1100))) return 0;
        /* Each click hands the guard to whatever came forward, so the next one can move the pointer. */
        if (window_usable(GetForegroundWindow())) p->target = GetForegroundWindow();
    }
    if (GetForegroundWindow() != window) { playback_log(p, "could not bring the window forward"); return 0; }
    p->target = window;
    if (!playback_guard(p)) return 0;
    /* Off the taskbar and into the window: a pointer left on the icon opens its hover preview,
       which can take focus away a few seconds later. */
    {
        RECT area;
        POINT rest;
        if (GetWindowRect(window, &area)) {
            area.top += 120; area.bottom -= 80;
            if (motion_rest_point(rng, &area, &rest) && window_point_visible(rest) &&
                !mouse_move_human(p, rng, rest, MOTION_REST, 160)) return 0;
        }
    }
    return 1;
}
/* The Start menu or its search panel, once it is in front. */
static HWND start_menu_front(Playback *p, DWORD timeout_ms) {
    DWORD waited;
    for (waited = 0; waited <= timeout_ms; waited += 100) {
        HWND front = GetForegroundWindow();
        if (front && (window_process_is(front, L"StartMenuExperienceHost.exe") || window_process_is(front, L"SearchHost.exe")))
            return front;
        if (!playback_wait(p, 100)) return NULL;
    }
    return NULL;
}
/* Open an app from the Start menu with the mouse: click Start, then its pinned tile, or type its
   name in the search and click the result. Returns 1 when the app was launched, 2 when the Start
   menu could not be used (the caller falls back to Win+R), 0 to stop. */
int start_menu_open(Playback *p, MotionRng *rng, const wchar_t *app) {
    HWND start;
    RECT tile;
    int clicked;
    char shown[64];
    WideCharToMultiByte(CP_UTF8, 0, app, -1, shown, sizeof(shown), NULL, NULL);
    playback_log(p, "opening %s from the Start menu", shown);
    if (!(clicked = taskbar_icon_click(p, rng, L"Start", TASKBAR_LEFT))) return 0;
    if (clicked == 2 || !(start = start_menu_front(p, 3000))) {
        playback_log(p, "the Start menu did not open"); return 2;
    }
    p->target = start;
    if (!playback_wait(p, motion_between(rng, 500, 1100))) return 0;
    /* A pinned tile, about half the time when there is one. */
    if (motion_chance(rng, 55) &&
        (ui_find_control_rect(p, UIA_ListItemControlTypeId, app, &tile) || ui_find_control_rect(p, UIA_ButtonControlTypeId, app, &tile))) {
        playback_log(p, "clicking the pinned %s tile", shown);
        return click_rect(p, rng, &tile, 0) ? 1 : 0;
    }
    /* Otherwise search: typing goes straight into the Start search box. */
    playback_log(p, "searching for %s in the Start menu", shown);
    if (!keyboard_type(p, app) || !playback_wait(p, motion_between(rng, 1200, 2000))) return 0;
    if ((start = start_menu_front(p, 1000))) p->target = start;
    if (ui_window_control_rect(p, p->target, UIA_ListItemControlTypeId, app, &tile)) {
        playback_log(p, "clicking the %s search result", shown);
        return click_rect(p, rng, &tile, 0) ? 1 : 0;
    }
    /* Several results mention the name: Enter opens the best match, which Start highlights. */
    playback_log(p, "opening the best match (Enter)");
    return keyboard_press(p, VK_RETURN) ? 1 : 0;
}
/* Start menu first; if it does not respond, close it and use Win+R. */
static int open_app(Playback *p, MotionRng *rng, const wchar_t *app, const wchar_t *command) {
    int result = start_menu_open(p, rng, app);
    if (result != 2) return result;
    p->target = GetForegroundWindow();
    if (p->target && start_menu_front(p, 0) && !keyboard_press(p, VK_ESCAPE)) return 0;
    if (!playback_wait(p, 500)) return 0;
    p->target = GetForegroundWindow();
    playback_log(p, "falling back to Win+R");
    return run_command(p, command);
}
static int explorer_in_front(Playback *p, DWORD timeout_ms) {
    DWORD waited;
    for (waited = 0; waited < timeout_ms; waited += 100) {
        HWND front = GetForegroundWindow();
        wchar_t cls[64];
        if (front && GetClassNameW(front, cls, 64) && !wcscmp(cls, L"CabinetWClass") && window_process_is(front, L"explorer.exe")) {
            p->target = front;
            return playback_wait(p, 1200) ? 1 : -1;
        }
        if (!playback_wait(p, 100)) return -1;
    }
    return 0;
}
/* File Explorer from its taskbar icon, like a person would; Win+E when the icon is not pinned. */
static int open_explorer(Playback *p, MotionRng *rng) {
    int clicked;
    playback_log(p, "opening File Explorer from its taskbar icon");
    if (!(clicked = taskbar_icon_click(p, rng, L"File Explorer", TASKBAR_NEW_WINDOW))) return 0;
    if (clicked == 1) {
        int front = explorer_in_front(p, 4000);
        if (front) return front > 0;
        /* With several Explorer windows open, the click shows thumbnails instead; close them. */
        playback_log(p, "no File Explorer window came to the front; closing the preview");
        p->target = GetForegroundWindow();
        if (p->target && !keyboard_press(p, VK_ESCAPE)) return 0;
    } else playback_log(p, "File Explorer is not pinned to the taskbar; using Win+E");
    if (!p->target) p->target = GetForegroundWindow();
    return coding_step_win_key(p, L'E') && coding_step_wait_window(p, L"CabinetWClass", L"explorer.exe", NULL, NULL) &&
        playback_wait(p, 1200);
}
/* Close the app (Alt+F4), or now and then minimize it and leave it open. */
static int put_away(Playback *p, const SideAppsConfig *c, MotionRng *rng) {
    HWND app = p->target;
    unsigned i;
    if (motion_chance(rng, c->leave_open_percent)) {
        playback_log(p, "leaving the app open, minimized");
        for (i = 0; i < 3 && !IsIconic(app) && GetForegroundWindow() == app; ++i)
            if (!shortcut(p, VK_LWIN, VK_DOWN) || !playback_wait(p, 500)) return 0;
        return 1;
    }
    playback_log(p, "closing the app (Alt+F4)");
    if (!shortcut(p, VK_MENU, VK_F4)) return 0;
    for (i = 0; i < 25 && IsWindow(app) && IsWindowVisible(app); ++i) if (!playback_wait(p, 200)) return 0;
    return 1;
}
/* Back to the window that was in front before the break. */
static int return_to(Playback *p, HWND back) {
    HWND foreground;
    if (!back || !IsWindow(back)) { p->target = GetForegroundWindow(); return p->target != NULL; }
    if (IsIconic(back)) ShowWindow(back, SW_RESTORE);
    Sleep(400);
    if (GetForegroundWindow() != back) { SetForegroundWindow(back); Sleep(300); }
    foreground = GetForegroundWindow();
    if (foreground != back && foreground) {
        playback_log(p, "returning with Alt+Tab");
        p->target = foreground;
        if (!shortcut(p, VK_MENU, VK_TAB) || !playback_wait(p, 500)) return 0;
    }
    if (GetForegroundWindow() != back) { playback_log(p, "could not return to the previous window"); return 0; }
    p->target = back;
    return playback_guard(p);
}

/* ---------- apps ---------- */

static int use_notepad(Playback *p, const SideAppsConfig *c, const Profile *profile, DWORD duration, MotionRng *rng) {
    ULONGLONG start = GetTickCount64(), paused = p->paused_ms;
    wchar_t title[256], folder[MAX_PATH], path[MAX_PATH], quoted[MAX_PATH + 3], note[2100], *stem;
    SYSTEMTIME now;
    HWND app;
    unsigned notes = 0;
    if (!open_app(p, rng, L"Notepad", L"notepad") || !coding_step_wait_window(p, NULL, L"Notepad.exe", NULL, NULL) || !playback_wait(p, 1500)) return 0;
    app = p->target;
    /* Notepad can reopen earlier tabs; write in a fresh one. */
    if (!GetWindowTextW(app, title, 256) || !wcsstr(title, L"Untitled")) {
        if (!shortcut(p, VK_CONTROL, L'N') || !playback_wait(p, 1000)) return 0;
    }
    while (left_ms(p, start, paused, duration) > 60000 || !notes) {
        if (!side_apps_note(c, profile, rng, note, 2100)) return 0;
        playback_log(p, "typing a note in Notepad");
        if (notes && !keyboard_type(p, L"\n\n")) return 0;
        if (!keyboard_type(p, note)) return 0;
        ++notes;
        if (!linger(p, rng, 4000, 20000)) return 0;
        if (notes >= 4) break;
    }
    /* Save the note in the notes folder. */
    if (!side_apps_folder(c->notes_folder, profile, folder)) { playback_log(p, "notes_folder could not be resolved"); return 0; }
    CreateDirectoryW(folder, NULL);
    GetLocalTime(&now);
    swprintf(path, MAX_PATH, L"%ls\\note-%04u-%02u-%02u-%02u%02u.txt", folder, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute);
    swprintf(quoted, MAX_PATH + 3, L"\"%ls\"", path);
    stem = wcsrchr(path, L'\\') + 1;
    playback_log(p, "saving the note (Ctrl+S)");
    if (!shortcut(p, VK_CONTROL, L'S') || !coding_step_wait_window(p, L"#32770", L"Notepad.exe", NULL, NULL) ||
        !playback_wait(p, 800) || !shortcut(p, VK_MENU, L'N') || !shortcut(p, VK_CONTROL, L'A') ||
        !keyboard_type(p, quoted) || !playback_wait(p, 500) || !keyboard_press(p, VK_RETURN) ||
        !coding_step_wait_window(p, NULL, L"Notepad.exe", stem, app)) return 0;
    while (left_ms(p, start, paused, duration)) if (!linger(p, rng, 3000, 15000)) return 0;
    return put_away(p, c, rng);
}
/* Subfolders of a folder, skipping hidden and system ones. */
static size_t subfolders(const wchar_t *folder, wchar_t (*names)[MAX_PATH], size_t cap) {
    wchar_t query[MAX_PATH];
    WIN32_FIND_DATAW d;
    HANDLE h;
    size_t count = 0;
    swprintf(query, MAX_PATH, L"%ls\\*", folder);
    if ((h = FindFirstFileW(query, &d)) == INVALID_HANDLE_VALUE) return 0;
    do {
        if ((d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(d.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT)) &&
            d.cFileName[0] != L'.' && count < cap) wcscpy(names[count++], d.cFileName);
    } while (FindNextFileW(h, &d));
    FindClose(h);
    return count;
}
/* Double-click one visible subfolder. Returns 1 opened, 2 none visible, 0 stop. */
static int open_subfolder(Playback *p, MotionRng *rng, wchar_t *current) {
    wchar_t names[64][MAX_PATH];
    size_t count = subfolders(current, names, 64), tries;
    for (tries = 0; tries < count && tries < 6; ++tries) {
        size_t pick = motion_between(rng, 0, (unsigned)count - 1);
        RECT item;
        char shown[3 * MAX_PATH];
        if (!ui_find_control_rect(p, UIA_ListItemControlTypeId, names[pick], &item)) continue;
        WideCharToMultiByte(CP_UTF8, 0, names[pick], -1, shown, sizeof(shown), NULL, NULL);
        playback_log(p, "File Explorer: opening folder %s", shown);
        if (!click_rect(p, rng, &item, 1) || !playback_wait(p, 1300)) return 0;
        if (wcslen(current) + wcslen(names[pick]) + 2 < MAX_PATH) { wcscat(current, L"\\"); wcscat(current, names[pick]); }
        return 1;
    }
    return 2;
}
/* Wheel over the main file view (right of the sidebar, below the toolbar). */
static int scroll_list(Playback *p, MotionRng *rng, int direction, unsigned notches) {
    RECT area;
    POINT point;
    unsigned i;
    if (!window_area(p, &area)) return 1;
    area.left += (area.right - area.left) / 4; area.top += 120;
    if (!motion_rest_point(rng, &area, &point) || !move_to(p, rng, point, MOTION_REST, 160)) return 0;
    for (i = 0; i < notches; ++i) if (!mouse_wheel(p, direction) || !playback_wait(p, motion_between(rng, 90, 260))) return 0;
    return 1;
}
static int scroll_view(Playback *p, MotionRng *rng) {
    return scroll_list(p, rng, motion_chance(rng, 70) ? -1 : 1, motion_between(rng, 2, 6));
}
static int use_explorer(Playback *p, const SideAppsConfig *c, const Profile *profile, DWORD duration, MotionRng *rng) {
    ULONGLONG start = GetTickCount64(), paused = p->paused_ms;
    wchar_t current[MAX_PATH] = L"", root[MAX_PATH];
    unsigned depth = 0, steps = 0;
    if (!open_explorer(p, rng)) return 0;
    while (left_ms(p, start, paused, duration)) {
        unsigned roll = motion_between(rng, 0, 99);
        int result;
        if (!*current || steps >= motion_between(rng, 6, 12)) {
            /* Start somewhere familiar from the sidebar: Documents, Downloads, the project folder... */
            if (!side_apps_folder(c->folders[motion_between(rng, 0, (unsigned)c->folder_count - 1)], profile, root) || !is_folder(root)) continue;
            if (!(result = browse_to(p, rng, root, current))) return 0;
            if (result == 2) { if (++steps > 40) break; continue; }
            depth = 0; steps = 0;
        } else if (roll < 50) {
            if (!(result = open_subfolder(p, rng, current))) return 0;
            if (result == 1) ++depth;
            else if (!scroll_view(p, rng)) return 0;
        } else if (roll < 65 && depth) {
            wchar_t *slash = wcsrchr(current, L'\\');
            if (!go_back(p, rng)) return 0;
            if (slash) *slash = 0;
            --depth;
        } else if (roll < 85) {
            if (!scroll_view(p, rng)) return 0;
        }
        ++steps;
        if (!linger(p, rng, 1500, 7000)) return 0;
    }
    return put_away(p, c, rng);
}
static int calculator_key(Playback *p, MotionRng *rng, const wchar_t *button, wchar_t key) {
    RECT r;
    wchar_t text[2];
    if (ui_find_control_rect(p, UIA_ButtonControlTypeId, button, &r))
        return click_rect(p, rng, &r, 0) && playback_wait(p, motion_between(rng, 150, 600));
    /* Buttons named differently (another display language): type the key instead. */
    if (key == 0x1b) return keyboard_press(p, VK_ESCAPE);
    text[0] = key; text[1] = 0;
    return keyboard_type(p, text);
}
static int use_calculator(Playback *p, const SideAppsConfig *c, DWORD duration, MotionRng *rng) {
    static const wchar_t *const digits[10] = {L"Zero", L"One", L"Two", L"Three", L"Four", L"Five", L"Six", L"Seven", L"Eight", L"Nine"};
    static const struct { const wchar_t *button; wchar_t key; } ops[] = {{L"Plus", L'+'}, {L"Minus", L'-'}, {L"Multiply by", L'*'}, {L"Divide by", L'/'}};
    ULONGLONG start = GetTickCount64(), paused = p->paused_ms;
    if (!open_app(p, rng, L"Calculator", L"calc") || !coding_step_wait_window(p, NULL, NULL, L"Calculator", NULL) || !playback_wait(p, 1500)) return 0;
    while (left_ms(p, start, paused, duration)) {
        unsigned a = motion_between(rng, 2, 9999), b = motion_between(rng, 2, 999), op = motion_between(rng, 0, 3), k;
        wchar_t number[16];
        playback_log(p, "Calculator: %u %c %u", a, (char)ops[op].key, b);
        if (!calculator_key(p, rng, L"Clear", 0x1b)) return 0;
        swprintf(number, 16, L"%u", a);
        for (k = 0; number[k]; ++k) if (!calculator_key(p, rng, digits[number[k] - L'0'], number[k])) return 0;
        if (!calculator_key(p, rng, ops[op].button, ops[op].key)) return 0;
        swprintf(number, 16, L"%u", b);
        for (k = 0; number[k]; ++k) if (!calculator_key(p, rng, digits[number[k] - L'0'], number[k])) return 0;
        if (!calculator_key(p, rng, L"Equals", L'=') || !linger(p, rng, 3000, 15000)) return 0;
    }
    return put_away(p, c, rng);
}
static int is_image(const wchar_t *name) {
    static const wchar_t *const kinds[] = {L".png", L".jpg", L".jpeg", L".gif", L".bmp", L".webp"};
    const wchar_t *dot = wcsrchr(name, L'.');
    size_t i;
    for (i = 0; dot && i < sizeof(kinds)/sizeof(*kinds); ++i) if (!_wcsicmp(dot, kinds[i])) return 1;
    return 0;
}
/* Folders (up to three levels down) that hold at least two pictures. */
static void image_folders(const wchar_t *folder, unsigned depth, wchar_t (*found)[MAX_PATH], size_t *count, size_t cap) {
    wchar_t query[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW d;
    HANDLE h;
    unsigned images = 0;
    swprintf(query, MAX_PATH, L"%ls\\*", folder);
    if (*count >= cap || (h = FindFirstFileW(query, &d)) == INVALID_HANDLE_VALUE) return;
    do {
        if (d.cFileName[0] == L'.' || (d.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT))) continue;
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth < 3 && _wcsicmp(d.cFileName, L"node_modules") && swprintf(path, MAX_PATH, L"%ls\\%ls", folder, d.cFileName) > 0)
                image_folders(path, depth + 1, found, count, cap);
        } else images += is_image(d.cFileName);
    } while (FindNextFileW(h, &d));
    FindClose(h);
    if (images >= 2 && *count < cap) wcscpy(found[(*count)++], folder);
}
static int use_images(Playback *p, const SideAppsConfig *c, const Profile *profile, DWORD duration, MotionRng *rng) {
    ULONGLONG start = GetTickCount64(), paused = p->paused_ms;
    wchar_t folders[24][MAX_PATH], root[MAX_PATH], query[MAX_PATH], files[64][MAX_PATH];
    size_t count = 0, file_count = 0, i;
    WIN32_FIND_DATAW d;
    HANDLE h;
    HWND explorer, viewer = NULL;
    for (i = 0; i < c->image_folder_count; ++i)
        if (side_apps_folder(c->image_folders[i], profile, root) && is_folder(root)) image_folders(root, 0, folders, &count, 24);
    if (!count) {
        playback_log(p, "no pictures found in the image folders; browsing folders instead");
        return use_explorer(p, c, profile, duration, rng);
    }
    wcscpy(root, folders[motion_between(rng, 0, (unsigned)count - 1)]);
    swprintf(query, MAX_PATH, L"%ls\\*", root);
    if ((h = FindFirstFileW(query, &d)) != INVALID_HANDLE_VALUE) {
        do if (is_image(d.cFileName) && file_count < 64) wcscpy(files[file_count++], d.cFileName); while (FindNextFileW(h, &d));
        FindClose(h);
    }
    playback_log(p, "looking at pictures");
    if (!open_explorer(p, rng)) return 0;
    { wchar_t reached[MAX_PATH] = L"";
      int result = browse_to(p, rng, root, reached);
      if (!result) return 0;
      if (result == 2 || _wcsicmp(reached, root)) {
          playback_log(p, "could not browse to the picture folder; closing File Explorer");
          return put_away(p, c, rng);
      } }
    explorer = p->target;
    /* Explorer may hide extensions; try the name both ways. */
    for (i = 0; i < 6 && !viewer; ++i) {
        wchar_t name[MAX_PATH], *dot;
        RECT item;
        ULONGLONG waited;
        wcscpy(name, files[motion_between(rng, 0, (unsigned)file_count - 1)]);
        if (!ui_find_control_rect(p, UIA_ListItemControlTypeId, name, &item)) {
            if ((dot = wcsrchr(name, L'.'))) *dot = 0;
            if (!ui_find_control_rect(p, UIA_ListItemControlTypeId, name, &item)) continue;
        }
        playback_log(p, "opening a picture");
        if (!click_rect(p, rng, &item, 1)) return 0;
        for (waited = 0; waited < 15000; waited += 250) {
            HWND foreground = GetForegroundWindow();
            if (!playback_wait(p, 250)) return 0;
            if (foreground && foreground != explorer && IsWindowVisible(foreground)) { viewer = foreground; break; }
        }
    }
    if (!viewer) { playback_log(p, "no picture opened; browsing folders instead"); p->target = explorer; return put_away(p, c, rng); }
    p->target = viewer;
    if (!playback_wait(p, 2000)) return 0;
    while (left_ms(p, start, paused, duration)) {
        WORD key = motion_chance(rng, 80) ? VK_RIGHT : VK_LEFT;
        if (!linger(p, rng, 3000, 12000) || !keyboard_press(p, key)) return 0;
    }
    playback_log(p, "closing the picture viewer (Alt+F4)");
    if (!shortcut(p, VK_MENU, VK_F4) || !playback_wait(p, 1200)) return 0;
    if (GetForegroundWindow() != explorer && !window_activate_handle(p, explorer)) return 0;
    p->target = explorer;
    return put_away(p, c, rng);
}

int side_apps_run(Playback *p, const SideAppsConfig *c, const Profile *profile, SideApp app, DWORD duration, MotionRng *rng) {
    HWND back = p->target ? p->target : GetForegroundWindow();
    int coordinates = p->client_coords, ok;
    if (!p->target) p->target = back;
    p->client_coords = 0;
    playback_log(p, "side app break: %s for %.1f minutes", side_app_name(app), duration / 60000.0);
    switch (app) {
        case APP_NOTEPAD: ok = use_notepad(p, c, profile, duration, rng); break;
        case APP_EXPLORER: ok = use_explorer(p, c, profile, duration, rng); break;
        case APP_CALCULATOR: ok = use_calculator(p, c, duration, rng); break;
        default: ok = use_images(p, c, profile, duration, rng); break;
    }
    if (!ok) {
        /* A step failed (a window that did not open, a moved control). Unless the user stopped
           playback, close whatever the break left in front and go back to the main task. */
        if (InterlockedCompareExchange(&p->user_stop, 0, 0) || !window_desktop_ready()) { p->client_coords = coordinates; return 0; }
        InterlockedExchange(&p->stopped, 0);
        playback_log(p, "side app step failed; closing it and returning to the main task");
        { HWND front = GetForegroundWindow();
          if (front && front != back) PostMessageW(front, WM_CLOSE, 0, 0); }
        Sleep(1000);
    }
    ok = return_to(p, back);
    if (ok) playback_log(p, "side app break finished; back to the main task");
    p->client_coords = coordinates;
    return ok;
}

int side_apps_open(Playback *p, MotionRng *rng, SideApp app) {
    int coordinates = p->client_coords, ok;
    if (!p->target) p->target = GetForegroundWindow();
    p->client_coords = 0;
    switch (app) {
        case APP_NOTEPAD:
            ok = open_app(p, rng, L"Notepad", L"notepad") && coding_step_wait_window(p, NULL, L"Notepad.exe", NULL, NULL);
            break;
        case APP_CALCULATOR:
            ok = open_app(p, rng, L"Calculator", L"calc") && coding_step_wait_window(p, NULL, NULL, L"Calculator", NULL);
            break;
        default: ok = open_explorer(p, rng); break;
    }
    p->client_coords = coordinates;
    return ok && playback_wait(p, motion_between(rng, 800, 1600));
}
int side_apps_job(Playback *p, const SideAppJob *job) {
    MotionRng rng;
    motion_seed(&rng, job->seed);
    p->typing_speed = job->profile->typing_speed;
    p->typo_percent = profile_mistype_percent(job->profile);
    p->typo_state = job->seed ^ 0x9e3779b9u;
    motion_hand_for(&p->hand, motion_hash(job->profile->local_id), job->profile->randomness_score);
    if (!p->target) p->target = GetForegroundWindow();
    return side_apps_run(p, job->config, job->profile, job->app, job->duration_ms, &rng);
}

/* ---------- timer ---------- */

void side_apps_timer_init(SideAppsTimer *t, const SideAppsConfig *c, const Profile *profile, uint32_t seed, const Playback *p) {
    memset(t, 0, sizeof(*t));
    t->config = c; t->profile = profile;
    motion_seed(&t->rng, seed ^ 0x6a09e667u);
    t->last = GetTickCount64(); t->paused = p->paused_ms;
    t->next_check_ms = motion_between(&t->rng, c->check_minutes[0], c->check_minutes[1]) * 60000;
}
int side_apps_tick(Playback *p, SideAppsTimer *t, int *took) {
    ULONGLONG now = GetTickCount64();
    *took = 0;
    /* A work session's planned idles happen at the same safe points as breaks. */
    if (p->safe_point && !playback_yield(p) && !p->safe_point(p)) return 0;
    if (!t || !t->config) return 1;
    /* No new break once the task has been asked to stop. */
    if (playback_yield(p)) return 1;
    t->elapsed_ms += (DWORD)(now - t->last - (p->paused_ms - t->paused));
    t->last = now; t->paused = p->paused_ms;
    if (t->elapsed_ms < t->next_check_ms) return 1;
    t->elapsed_ms = 0;
    t->next_check_ms = motion_between(&t->rng, t->config->check_minutes[0], t->config->check_minutes[1]) * 60000;
    if (!motion_chance(&t->rng, t->config->chance_percent)) {
        playback_log(p, "side app check: staying on the main task (next check in %u minutes)", t->next_check_ms / 60000);
        return 1;
    }
    {
        SideApp app = side_apps_pick(t->config, &t->rng);
        DWORD duration = motion_between(&t->rng, t->config->minutes[0], t->config->minutes[1]) * 60000;
        int ok = side_apps_run(p, t->config, t->profile, app, duration, &t->rng);
        ++t->breaks; *took = 1;
        /* Break time does not count toward the next check. */
        t->last = GetTickCount64(); t->paused = p->paused_ms;
        return ok;
    }
}
