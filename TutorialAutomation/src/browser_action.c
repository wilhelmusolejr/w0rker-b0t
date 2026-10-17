#include "browser_action.h"
#include "browser_scroll_plan.h"
#include "keyboard.h"
#include "mouse.h"
#include "window_manager.h"
#include "presentation.h"
#include "ui_targets.h"
#include "reference_config.h"
#include <uiautomationclient.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct {
    HWND old[64], found;
    unsigned count, matches;
    const wchar_t *process;
    int find_new, overflow;
} BrowserWindows;

static int browser_window_is(HWND window, const wchar_t *process) {
    wchar_t name[128];
    return IsWindow(window) && IsWindowVisible(window) &&
        GetClassNameW(window, name, 128) && !wcscmp(name, L"Chrome_WidgetWin_1") &&
        window_process_is(window, process);
}

static BOOL CALLBACK collect_browser(HWND window, LPARAM parameter) {
    BrowserWindows *windows = (BrowserWindows *)parameter;
    unsigned i;
    if (!browser_window_is(window, windows->process)) return TRUE;
    if (!windows->find_new) {
        if (windows->count == 64) { windows->overflow = 1; return FALSE; }
        windows->old[windows->count++] = window;
    } else {
        for (i = 0; i < windows->count; ++i) if (windows->old[i] == window) return TRUE;
        if (IsWindowEnabled(window)) { windows->found = window; ++windows->matches; }
    }
    return TRUE;
}

static int find_browser_program(wchar_t *path, const wchar_t **process) {
    static const struct { const wchar_t *variable, *suffix, *name; } choices[] = {
        {L"ProgramFiles", L"\\Google\\Chrome\\Application\\chrome.exe", L"chrome.exe"},
        {L"ProgramFiles(x86)", L"\\Google\\Chrome\\Application\\chrome.exe", L"chrome.exe"},
        {L"LOCALAPPDATA", L"\\Google\\Chrome\\Application\\chrome.exe", L"chrome.exe"},
        {L"ProgramFiles(x86)", L"\\Microsoft\\Edge\\Application\\msedge.exe", L"msedge.exe"},
        {L"ProgramFiles", L"\\Microsoft\\Edge\\Application\\msedge.exe", L"msedge.exe"}
    };
    unsigned i;
    for (i = 0; i < sizeof(choices)/sizeof(choices[0]); ++i) {
        DWORD length = GetEnvironmentVariableW(choices[i].variable, path, MAX_PATH), attrs;
        if (!length || length + wcslen(choices[i].suffix) >= MAX_PATH) continue;
        wcscat(path, choices[i].suffix);
        attrs = GetFileAttributesW(path);
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            *process = choices[i].name; return 1;
        }
    }
    return 0;
}

static int shortcut(Playback *p, WORD modifier, WORD key) {
    WORD keys[] = {modifier, key};
    return keyboard_shortcut(p, keys, 2);
}

static int wait_run_dialog(Playback *p) {
    ULONGLONG start = GetTickCount64(), paused = p->paused_ms;
    while (GetTickCount64() - start - (p->paused_ms - paused) < 10000) {
        HWND window;
        wchar_t cls[128];
        if (!playback_poll(p)) return 0;
        window = GetForegroundWindow();
        if (window && IsWindowVisible(window) && IsWindowEnabled(window) &&
            GetClassNameW(window, cls, 128) && !wcscmp(cls, L"#32770") &&
            window_process_is(window, L"explorer.exe")) {
            if (!playback_wait(p, 200)) return 0;
            if (GetForegroundWindow() != window || !IsWindowVisible(window) || !IsWindowEnabled(window)) continue;
            p->target = window;
            return playback_guard(p);
        }
        if (!playback_wait(p, 50)) return 0;
    }
    playback_log(p, "Windows Run dialog did not open"); return 0;
}

static int navigate_browser(Playback *p, const wchar_t *url);

static int taskbar_is(HWND window) {
    wchar_t cls[128];
    return window && GetClassNameW(window, cls, 128) && !wcscmp(cls, L"Shell_TrayWnd") &&
        window_process_is(window, L"explorer.exe");
}

/* Win+T gives the taskbar keyboard focus, so it becomes the guarded target for the click. */
static int focus_taskbar(Playback *p) {
    ULONGLONG started, paused;
    if (!shortcut(p, VK_LWIN, L'T')) return 0;
    started = GetTickCount64(); paused = p->paused_ms;
    while (GetTickCount64() - started - (p->paused_ms - paused) < 5000) {
        HWND foreground;
        if (!playback_poll(p)) return 0;
        foreground = GetForegroundWindow();
        if (taskbar_is(foreground)) { p->target = foreground; return playback_guard(p); }
        if (!playback_wait(p, 50)) return 0;
    }
    playback_log(p, "taskbar did not take focus after Win+T"); return 0;
}

/* Left-click starts the browser; middle-click opens a new window when one is already running. */
static int launch_from_taskbar(Playback *p, const wchar_t *process, int running) {
    const wchar_t *label = !_wcsicmp(process, L"msedge.exe") ? L"Microsoft Edge" : L"Google Chrome";
    RECT button;
    int coordinates = p->client_coords, ok;
    playback_log(p, "opening the browser from its taskbar icon (%s)", running ? "middle-click, new window" : "click");
    if (!focus_taskbar(p) || !ui_control_rect(p, UIA_ButtonControlTypeId, label, 1, &button) ||
        !presentation_hover(p, &button)) return 0;
    p->client_coords = 0;
    ok = mouse_click(p, running ? 2 : 0, 0);
    p->client_coords = coordinates;
    return ok;
}

static int launch_from_run(Playback *p, const wchar_t *url, const wchar_t *program) {
    wchar_t command[4096];
    swprintf(command, 4096, L"\"%ls\" --new-window \"%ls\"", program, url);
    if (wcslen(command) >= 259) {
        playback_log(p, "browser launch text exceeds the Run dialog limit; choose a shorter URL"); return 0;
    }
    playback_log(p, "opening a dedicated browser window through Windows Run");
    return shortcut(p, VK_LWIN, L'R') && wait_run_dialog(p) &&
        shortcut(p, VK_CONTROL, L'A') && keyboard_type(p, command) && keyboard_press(p, VK_RETURN);
}

static int taskbar_launch_selected(const Playback *p) {
    const ReferenceConfig *config = p->reference_config;
    return config && config->browser_launch == REFERENCE_LAUNCH_TASKBAR;
}

static int open_browser(Playback *p, const wchar_t *url, const wchar_t *program,
                        const wchar_t *process, HWND *window) {
    BrowserWindows windows = {0};
    HWND previous = p->target;
    ULONGLONG started, paused;
    int via_taskbar = 0;
    windows.process = process;
    EnumWindows(collect_browser, (LPARAM)&windows);
    if (windows.overflow) { playback_log(p, "too many browser windows to identify a new one safely"); return 0; }
    if (taskbar_launch_selected(p)) {
        via_taskbar = launch_from_taskbar(p, process, windows.count != 0);
        if (!via_taskbar) {
            if (InterlockedCompareExchange(&p->stopped, 0, 0)) return 0;
            /* Leave taskbar focus and fall back to Windows Run, which works from any foreground window. */
            playback_log(p, "taskbar icon unavailable; falling back to Windows Run");
            if (taskbar_is(GetForegroundWindow()) && !keyboard_press(p, VK_ESCAPE)) return 0;
            if (!playback_wait(p, 400)) return 0;
            p->target = GetForegroundWindow();
            if (!p->target) p->target = previous;
        }
    }
    if (!via_taskbar && !launch_from_run(p, url, program)) return 0;
    windows.find_new = 1;
    started = GetTickCount64(); paused = p->paused_ms;
    while (GetTickCount64() - started - (p->paused_ms - paused) < 30000) {
        if (!playback_poll(p)) return 0;
        windows.found = NULL; windows.matches = 0;
        EnumWindows(collect_browser, (LPARAM)&windows);
        if (windows.matches == 1) {
            if (!window_activate_handle(p, windows.found)) return 0;
            *window = windows.found;
            if (!playback_wait(p, 3500)) return 0;
            /* New windows can open small; maximize with Win+Up so results and pages have room. */
            if (!IsZoomed(windows.found)) {
                playback_log(p, "maximizing the browser window");
                if (!shortcut(p, VK_LWIN, VK_UP) || !playback_wait(p, 900) || !playback_guard(p)) return 0;
            }
            /* A taskbar launch opens the start page; type the destination like a person would. */
            return !via_taskbar || navigate_browser(p, url);
        }
        if (windows.matches > 1) {
            playback_log(p, "more than one new browser window appeared; stopped for inspection"); return 0;
        }
        if (!playback_wait(p, 100)) return 0;
    }
    playback_log(p, "dedicated browser window did not appear within 30 seconds"); return 0;
}

static DWORD time_left(const Playback *p, ULONGLONG start, ULONGLONG paused, DWORD duration) {
    ULONGLONG elapsed = GetTickCount64() - start - (p->paused_ms - paused);
    return elapsed >= duration ? 0 : (DWORD)(duration - elapsed);
}

static int scroll_page(Playback *p, DWORD duration, uint32_t seed) {
    BrowserScrollPlan plan;
    ULONGLONG started, paused;
    RECT client;
    if (!GetClientRect(p->target, &client) || client.right < 400 || client.bottom < 300) {
        playback_log(p, "browser client area is too small for scrolling"); return 0;
    }
    /* Position over page content rather than the address bar or another application. */
    if (p->action_pace.enabled) {
        RECT content = {client.right / 2, client.bottom / 2,
                        client.right * 3 / 4, client.bottom * 3 / 4};
        POINT point;
        if (!action_point_next(&p->action_pace, &content, &point) ||
            !mouse_move_arc(p, point.x, point.y, action_move_next(&p->action_pace),
                action_curve_next(&p->action_pace))) return 0;
    } else if (!mouse_move(p, client.right * 2 / 3, client.bottom * 2 / 3, 600, 0)) return 0;
    browser_scroll_plan_init(&plan, seed);
    started = GetTickCount64(); paused = p->paused_ms;
    while (time_left(p, started, paused, duration)) {
        BrowserScrollStep step = browser_scroll_plan_next(&plan);
        unsigned notches = (unsigned)(step.notches < 0 ? -step.notches : step.notches), i;
        DWORD remaining;
        playback_log(p, "browser scroll %s %u notch(es), then reading pause",
            step.notches < 0 ? "down" : "up", notches);
        for (i = 0; i < notches && time_left(p, started, paused, duration); ++i) {
            if (!playback_poll(p) || !mouse_scroll(p, step.notches < 0 ? -1 : 1)) return 0;
            remaining = time_left(p, started, paused, duration);
            if (!playback_wait(p, remaining > 120 ? 120 : remaining)) return 0;
        }
        remaining = time_left(p, started, paused, duration);
        if (!playback_wait(p, remaining > step.pause_ms ? step.pause_ms : remaining)) return 0;
    }
    return playback_guard(p);
}

static int browser_visit(Playback *p, const wchar_t *query, const wchar_t *url,
                         DWORD duration_ms, uint32_t seed, HWND *browser_window, int navigate_reused, unsigned research);
int browser_reading_visit(Playback *p, const wchar_t *url, DWORD duration_ms,
                          uint32_t seed, HWND *browser_window) {
    return browser_visit(p,NULL,url,duration_ms,seed,browser_window,0,0);
}

int browser_checkpoint_due(const Playback *p, unsigned completed_lines, int more_code) {
    return p->browser.every_lines && more_code && completed_lines >= p->browser.next_line;
}

int browser_checkpoint(Playback *p, unsigned completed_lines) {
    playback_log(p, "saved checkpoint at %u lines; starting browser visit", completed_lines);
    if (!browser_reading_visit(p, p->browser.url, p->browser.duration_ms,
        p->browser.seed + p->browser.visits, &p->browser.window)) return 0;
    ++p->browser.visits;
    p->browser.next_line = (completed_lines / p->browser.every_lines + 1) * p->browser.every_lines;
    return 1;
}

static int navigate_browser(Playback *p, const wchar_t *url) {
    return shortcut(p,VK_CONTROL,L'L') && presentation_rest(p,"browser address field selected") &&
        keyboard_type(p,url) && keyboard_press(p,VK_RETURN) && playback_wait(p,3500) && playback_guard(p);
}
static int search_google(Playback *p, const wchar_t *query) {
    RECT field;
    ULONGLONG started = GetTickCount64(), paused = p->paused_ms;
    while (GetTickCount64()-started-(p->paused_ms-paused) < 20000) {
        if (!playback_poll(p) || !playback_guard(p)) return 0;
        if (ui_find_control_rect(p,UIA_EditControlTypeId,L"Search",&field) ||
            ui_find_control_rect(p,UIA_ComboBoxControlTypeId,L"Search",&field)) {
            return presentation_click(p,&field,0) && presentation_rest(p,"Google search field selected") &&
                shortcut(p,VK_CONTROL,L'A') && keyboard_type(p,query) &&
                presentation_rest(p,"documentation search query entered") && keyboard_press(p,VK_RETURN) &&
                playback_wait(p,3500) && playback_guard(p);
        }
        if (!playback_wait(p,250)) return 0;
    }
    playback_log(p,"Google's Search field was unavailable; check page loading or a consent/network dialog"); return 0;
}
/* Click one visible organic result; returns 2 when none is suitable so the visit keeps reading results. */
static int open_search_result(Playback *p, uint32_t seed, wchar_t opened[220]) {
    UiWebLink links[64];
    unsigned count = 0, attempt, i, kept;
    char url[660];
    for (attempt = 0; attempt < 2; ++attempt) {
        if (!ui_web_links(p, links, 64, &count)) return 0;
        /* Do not reopen the result read earlier in this visit. */
        for (i = kept = 0; i < count; ++i) if (wcscmp(links[i].url, opened)) links[kept++] = links[i];
        count = kept;
        if (count) break;
        /* Results may have scrolled past; wheel back toward the top of the page and look again. */
        { unsigned notch;
          playback_log(p, "no result in view; scrolling back up");
          for (notch = 0; notch < 12; ++notch) if (!mouse_scroll(p, 1) || !playback_wait(p, 90)) return 0;
          if (!playback_wait(p, 1200)) return 0; }
    }
    if (!count) { playback_log(p, "no organic search result was visible; reading the results page instead"); return 2; }
    seed = seed * 1103515245u + 12345u;
    count = (seed >> 8) % (count < 6 ? count : 6);
    wcscpy(opened, links[count].url);
    WideCharToMultiByte(CP_UTF8, 0, links[count].name, -1, url, sizeof(url), NULL, NULL);
    playback_log(p, "opening search result %u: %s", count + 1, url);
    return presentation_click(p, &links[count].rect, 0) && playback_wait(p, 3500) && playback_guard(p);
}
static int browser_visit(Playback *p, const wchar_t *query, const wchar_t *url,
                         DWORD duration_ms, uint32_t seed, HWND *browser_window, int navigate_reused, unsigned research) {
    wchar_t program[MAX_PATH], cls[128];
    const wchar_t *process = NULL;
    HWND editor = p->target ? p->target : GetForegroundWindow();
    const wchar_t *initial_url = query && *query ? L"https://www.google.com/?hl=en" : url;
    int coordinates = p->client_coords, ok = 0;
    ULONGLONG started, paused;
    DWORD remaining;
    if (!url || !browser_window || !browser_url_valid(url)) {
        playback_log(p,"browser URL must use HTTP or HTTPS"); return 0;
    }
    if (GetClassNameW(editor,cls,128) && !wcscmp(cls,L"#32770") && window_process_is(editor,L"explorer.exe")) {
        playback_log(p,"close Windows Run and focus the application to return to before starting"); return 0;
    }
    if (!find_browser_program(program,&process)) {
        playback_log(p,"Chrome or Edge was not found in a supported installation directory"); return 0;
    }
    p->target = editor;
    if (!playback_guard(p)) return 0;
    p->client_coords = 1;
    if (browser_window_is(*browser_window,process)) {
        playback_log(p,"switching to the dedicated browser with Alt+Tab");
        if (!shortcut(p,VK_MENU,VK_TAB) || !playback_wait(p,250)) goto done;
        if (GetForegroundWindow() != *browser_window) {
            if (!window_activate_handle(p,*browser_window)) goto done;
        } else p->target = *browser_window;
        if (navigate_reused && !navigate_browser(p,initial_url)) goto done;
    } else if (!open_browser(p,initial_url,program,process,browser_window)) goto done;
    started = GetTickCount64(); paused = p->paused_ms;
    if (query && *query) {
        playback_log(p,"%s visit budget: %.1f seconds; searching Google",research ? "research" : "documentation",duration_ms/1000.0);
        if (!search_google(p,query)) goto done;
        remaining = time_left(p,started,paused,duration_ms);
        { DWORD results = research ? 5000 : 14000;
          if (remaining && !scroll_page(p,remaining > results ? results : remaining,seed)) goto done; }
        if (research) {
            /* research is the number of results to read; the remaining budget is shared between them. */
            wchar_t opened[220] = L"";
            unsigned index;
            for (index = 0; index < research; ++index) {
                int result;
                if (index) {
                    playback_log(p,"returning to the search results for result %u of %u",index+1,research);
                    if (!shortcut(p,VK_MENU,VK_LEFT) || !playback_wait(p,2500) || !playback_guard(p)) goto done;
                }
                if (!presentation_rest(p,"choosing a search result") ||
                    !(result = open_search_result(p,seed+index*7919u,opened))) goto done;
                remaining = time_left(p,started,paused,duration_ms);
                if (result == 2) { if (remaining && !scroll_page(p,remaining,seed+1)) goto done; break; }
                remaining /= research-index;
                if (remaining && !scroll_page(p,remaining,seed+1+index)) goto done;
            }
            goto finished;
        }
        /* Navigate to the topic's known official page, rather than guessing a result's screen position. */
        playback_log(p,"opening the selected official documentation page");
        if (!navigate_browser(p,url)) goto done;
    } else playback_log(p,"direct page visit: %.1f seconds of scrolling and reading pauses",duration_ms/1000.0);
    remaining = time_left(p,started,paused,duration_ms);
    if (remaining && !scroll_page(p,remaining,seed+1)) goto done;
finished:
    playback_log(p,"browser visit finished; returning to the previous application");
    if (!shortcut(p,VK_MENU,VK_TAB) || !playback_wait(p,250)) goto done;
    if (GetForegroundWindow() != editor) {
        if (!window_activate_handle(p,editor)) goto done;
    } else p->target = editor;
    if (!playback_guard(p)) goto done;
    playback_log(p,"previous application restored; browser visit completed");
    ok = 1;
done:
    p->client_coords = coordinates;
    return ok;
}
int browser_topic_visit(Playback *p, const wchar_t *query, const wchar_t *url,
                        DWORD duration_ms, uint32_t seed, HWND *browser_window) {
    return browser_visit(p,query,url,duration_ms,seed,browser_window,1,0);
}
int browser_research_visit(Playback *p, const wchar_t *query, DWORD duration_ms,
                           uint32_t seed, HWND *browser_window, unsigned results) {
    return query && *query &&
        browser_visit(p,query,L"https://www.google.com/?hl=en",duration_ms,seed,browser_window,1,results ? results : 1);
}
int browser_docs_visit(Playback *p, const wchar_t *query, const wchar_t *url,
                       DWORD duration_ms, uint32_t seed, HWND *browser_window) {
    return query && *query && browser_topic_visit(p,query,url,duration_ms,seed,browser_window);
}
