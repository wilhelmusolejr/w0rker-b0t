/* Opt-in Win32 receiver. It never injects input itself. */
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
static unsigned lefts, rights, doubles, wheels, characters, shortcuts;
static wchar_t text[2048];
static LRESULT CALLBACK receive(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_LBUTTONDOWN: ++lefts; break;
        case WM_LBUTTONDBLCLK: ++doubles; break;
        case WM_RBUTTONDOWN: ++rights; break;
        case WM_MOUSEWHEEL: ++wheels; break;
        case WM_KEYDOWN:
            if (wparam == L'K' && (GetKeyState(VK_CONTROL) & 0x8000)) ++shortcuts;
            break;
        case WM_CHAR:
            if (characters + 1 < sizeof(text)/sizeof(text[0])) {
                text[characters++] = (wchar_t)wparam; text[characters] = 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT paint; wchar_t report[4096];
            HDC dc = BeginPaint(window, &paint);
            RECT rect = {20,20,760,520};
            swprintf(report, 4096, L"Tutorial Automation Input Probe\n\n"
                L"Left presses: %u   Right presses: %u   Double clicks: %u\n"
                L"Wheel messages: %u   Ctrl+K shortcuts: %u\n"
                L"UTF-16 characters: %u\n\nReceived text:\n%ls\n\n"
                L"Run scripts/input_probe.txt in a second console.\n"
                L"F8 resumes the scripted pause; F12 stops playback.\n"
                L"Close this window when finished.", lefts,rights,doubles,wheels,shortcuts,characters,text);
            DrawTextW(dc, report, -1, &rect, DT_LEFT | DT_WORDBREAK);
            EndPaint(window, &paint); return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(window,message,wparam,lparam);
    }
    InvalidateRect(window, NULL, TRUE);
    return 0;
}
int main(void) {
    WNDCLASSW wc = {0}; MSG message;
    HWND window;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    wc.style = CS_DBLCLKS; wc.lpfnWndProc = receive;
    wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"TutorialInputProbe";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    if (!RegisterClassW(&wc)) return 1;
    window = CreateWindowW(wc.lpszClassName, L"Tutorial Automation Input Probe", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,800,600,NULL,NULL,wc.hInstance,NULL);
    if (!window) return 1;
    ShowWindow(window, SW_SHOW); UpdateWindow(window);
    puts("Input probe running. Close the window to exit.");
    while (GetMessageW(&message,NULL,0,0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
