#include "window_manager.h"
#include <wtsapi32.h>
#include <wchar.h>
#include <wctype.h>

void window_enable_dpi(void) {
    /* Windows 10 1703+: use physical pixel coordinates across monitors. */
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();
}

int window_desktop_ready(void) {
    HDESK desktop;
    wchar_t name[256];
    DWORD needed, session, bytes;
    WTS_CONNECTSTATE_CLASS *state = NULL;
    int active = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return 0;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session,
            WTSConnectState, (LPWSTR *)&state, &bytes)) {
        active = bytes >= sizeof(*state) && *state == WTSActive;
        WTSFreeMemory(state);
    }
    if (!active) return 0;
    desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop) return 0;
    active = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &needed) &&
             !_wcsicmp(name, L"Default");
    CloseDesktop(desktop);
    return active && GetSystemMetrics(SM_CXVIRTUALSCREEN) > 0 &&
                     GetSystemMetrics(SM_CYVIRTUALSCREEN) > 0;
}

int window_target_foreground(HWND target) {
    HWND foreground = GetForegroundWindow(), owned;
    DWORD target_pid = 0, foreground_pid = 0;
    unsigned depth = 0;
    if (foreground == target) return 1;
    if (!foreground || !target) return 0;
    GetWindowThreadProcessId(target, &target_pid);
    GetWindowThreadProcessId(foreground, &foreground_pid);
    if (!target_pid || target_pid != foreground_pid) return 0;
    /* Excel's Go To dialog, for example, is an owned top-level window. */
    for (owned = GetWindow(foreground, GW_OWNER); owned && depth < 128;
         owned = GetWindow(owned, GW_OWNER), ++depth)
        if (owned == target) return 1;
    return 0;
}

typedef struct { const wchar_t *title; HWND found; int matches; } Search;
static int contains(const wchar_t *text, const wchar_t *needle) {
    size_t length = wcslen(needle);
    for (; *text; ++text) if (!_wcsnicmp(text, needle, length)) return 1;
    return 0;
}
static BOOL CALLBACK find_window(HWND hwnd, LPARAM param) {
    Search *search = (Search *)param;
    wchar_t title[4096];
    if (IsWindowVisible(hwnd) && GetWindowTextW(hwnd, title, 4096) && contains(title, search->title)) {
        search->found = hwnd; ++search->matches;
    }
    return TRUE;
}

int window_process_is(HWND window, const wchar_t *expected) {
    DWORD pid = 0, length = MAX_PATH;
    wchar_t path[MAX_PATH], *name;
    HANDLE process;
    int result = 0;
    GetWindowThreadProcessId(window, &pid);
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return 0;
    if (QueryFullProcessImageNameW(process, 0, path, &length)) {
        name = wcsrchr(path, L'\\');
        result = !_wcsicmp(name ? name + 1 : path, expected);
    }
    CloseHandle(process);
    return result;
}

int window_activate_handle(Playback *p, HWND window) {
    if (!IsWindow(window) || !IsWindowVisible(window) || !IsWindowEnabled(window)) {
        playback_log(p, "requested window is unavailable or has a modal dialog"); return 0;
    }
    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
    SetForegroundWindow(window);
    /* Foreground restrictions are respected; never attach to another input thread. */
    if (!playback_wait(p, 250)) return 0;
    if (GetForegroundWindow() != window) {
        playback_log(p, "Windows denied foreground activation; focus the target and retry"); return 0;
    }
    p->target = window;
    return playback_guard(p);
}

int window_activate(Playback *p, const wchar_t *title) {
    Search search = {title, NULL, 0};
    EnumWindows(find_window, (LPARAM)&search);
    if (search.matches != 1) {
        playback_log(p, "window title must match exactly one visible window; found %d", search.matches);
        return 0;
    }
    return window_activate_handle(p, search.found);
}

int window_point_visible(POINT point) {
    return MonitorFromPoint(point, MONITOR_DEFAULTTONULL) != NULL;
}

int window_point(Playback *p, int x, int y, POINT *point) {
    RECT rect;
    point->x = x; point->y = y;
    if (!playback_guard(p)) return 0;
    if (p->client_coords) {
        if (!GetClientRect(p->target, &rect) || x < 0 || y < 0 ||
            x >= rect.right || y >= rect.bottom || !ClientToScreen(p->target, point)) {
            playback_log(p, "coordinate is outside the target client area"); return 0;
        }
    }
    if (!window_point_visible(*point)) {
        playback_log(p, "coordinate is outside all connected monitors"); return 0;
    }
    return 1;
}
