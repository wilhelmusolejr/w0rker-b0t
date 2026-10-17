#include "coding_action.h"
#include "keyboard.h"
#include "window_manager.h"
#include "browser_action.h"
#include "presentation.h"
#include "ui_targets.h"
#include "reference_config.h"
#include "json_config.h"
#include "saved_text_wait.h"
#include "vscode_editor.h"
#include <shlobj.h>
#include <uiautomationclient.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>

static int shortcut(Playback *p, WORD first, WORD last) {
    WORD keys[] = {first, last};
    return keyboard_shortcut(p, keys, 2);
}

typedef struct {
    const wchar_t *class_name, *process, *title;
    HWND exact, found;
    unsigned matches;
} WindowSearch;

static BOOL CALLBACK matching_window(HWND window, LPARAM parameter) {
    WindowSearch *search = (WindowSearch *)parameter;
    wchar_t cls[128], name[512];
    if (IsWindowVisible(window) && IsWindowEnabled(window) && (!search->exact || window == search->exact) &&
        (!search->class_name || (GetClassNameW(window, cls, 128) && !wcscmp(cls, search->class_name))) &&
        (!search->process || window_process_is(window, search->process)) &&
        (!search->title || (GetWindowTextW(window, name, 512) && wcsstr(name, search->title)))) {
        search->found = window;
        ++search->matches;
    }
    return TRUE;
}

static int wait_window(Playback *p, const wchar_t *class_name, const wchar_t *process,
                       const wchar_t *title, HWND exact) {
    ULONGLONG started = GetTickCount64(), paused_before = p->paused_ms;
    HWND activation_attempted = NULL;
    for (;;) {
        HWND window;
        wchar_t cls[128], name[512];
        if (!playback_poll(p)) return 0;
        window = GetForegroundWindow();
        if (window && IsWindowVisible(window) && IsWindowEnabled(window) && !IsIconic(window) &&
            (!exact || window == exact) &&
            (!class_name || (GetClassNameW(window, cls, 128) && !wcscmp(cls, class_name))) &&
            (!process || window_process_is(window, process)) &&
            (!title || (GetWindowTextW(window, name, 512) && wcsstr(name, title)))) {
            p->target = window;
            return 1;
        }
        /* An existing VS Code process can initially open the requested window behind another window. */
        if (title || exact) {
            WindowSearch search = {class_name, process, title, exact, NULL, 0};
            EnumWindows(matching_window, (LPARAM)&search);
            if (search.matches == 1 && search.found != activation_attempted) {
                activation_attempted = search.found;
                if (IsIconic(search.found)) ShowWindow(search.found, SW_RESTORE);
                SetForegroundWindow(search.found);
            }
        }
        if (GetTickCount64() - started - (p->paused_ms - paused_before) > 30000) {
            char observed[1536], expected_class[512];
            GetWindowTextW(GetForegroundWindow(), name, 512);
            WideCharToMultiByte(CP_UTF8, 0, name, -1, observed, sizeof(observed), NULL, NULL);
            playback_log(p, "observed foreground: %s", observed);
            if (class_name) {
                WideCharToMultiByte(CP_UTF8, 0, class_name, -1, expected_class, sizeof(expected_class), NULL, NULL);
                playback_log(p, "waiting for window class: %s", expected_class);
            }
            playback_log(p, "expected application/dialog did not reach foreground within 30 seconds");
            return 0;
        }
        if (!playback_wait(p, 50)) return 0;
    }
}

static int desktop_shortcut(Playback *p, WORD key) {
    if (!playback_poll(p)) return 0;
    p->target = GetForegroundWindow();
    return shortcut(p, VK_LWIN, key);
}

static BOOL CALLBACK find_demo_save_dialog(HWND window, LPARAM parameter) {
    HWND owner;
    wchar_t cls[128], title[512];
    if (!IsWindowVisible(window) || !GetClassNameW(window, cls, 128) ||
        wcscmp(cls, L"#32770") || !window_process_is(window, L"Code.exe")) return TRUE;
    for (owner = GetWindow(window, GW_OWNER); owner; owner = GetWindow(owner, GW_OWNER)) {
        if (GetWindowTextW(owner, title, 512) && wcsstr(title, L"Vehicle Coding Demo ")) {
            *(HWND *)parameter = window;
            return FALSE;
        }
    }
    return TRUE;
}

static int dismiss_demo_save_dialogs(Playback *p) {
    unsigned attempts;
    for (attempts = 0; attempts < 8; ++attempts) {
        HWND dialog = NULL;
        EnumWindows(find_demo_save_dialog, (LPARAM)&dialog);
        if (!dialog) return 1;
        playback_log(p, "dismissing an unfinished Save As dialog from an earlier demo attempt");
        SetForegroundWindow(dialog);
        if (!wait_window(p, L"#32770", L"Code.exe", NULL, dialog) ||
            !keyboard_press(p, VK_ESCAPE) || !playback_wait(p, 500)) return 0;
    }
    playback_log(p, "could not dismiss earlier demo Save As dialogs");
    return 0;
}

static int address(Playback *p, const wchar_t *path) {
    return shortcut(p, VK_CONTROL, L'L') && playback_wait(p, 200) &&
           keyboard_type(p, path) && keyboard_press(p, VK_RETURN) && playback_wait(p, 1200);
}

static int open_folder(Playback *p, const wchar_t *directory) {
    DWORD attrs = GetFileAttributesW(directory);
    wchar_t parent[MAX_PATH], *leaf;
    int create = attrs == INVALID_FILE_ATTRIBUTES;
    if (!desktop_shortcut(p, L'E') ||
        !wait_window(p, L"CabinetWClass", L"explorer.exe", NULL, NULL)) return 0;
    if (create) {
        WORD keys[] = {VK_CONTROL, VK_SHIFT, L'N'};
        wcscpy(parent, directory);
        leaf = wcsrchr(parent, L'\\');
        if (!leaf || !leaf[1]) return 0;
        *leaf++ = 0;
        if (!address(p, parent) || !keyboard_shortcut(p, keys, 3) ||
            !playback_wait(p, 600) || !keyboard_type(p, leaf) ||
            !keyboard_press(p, VK_RETURN) || !playback_wait(p, 1000)) return 0;
        if (GetFileAttributesW(directory) == INVALID_FILE_ATTRIBUTES) {
            playback_log(p, "Explorer did not create the destination folder"); return 0;
        }
    }
    return address(p, directory);
}

static int write_workspace(Playback *p, const wchar_t *directory, wchar_t *path,
                           const wchar_t *marker) {
    wchar_t relative[MAX_PATH], normalized[MAX_PATH], settings[4096];
    char *bytes;
    int count;
    DWORD written = 0;
    HANDLE file;
    size_t i;
    wcscpy(normalized, directory);
    for (i = 0; normalized[i]; ++i) if (normalized[i] == L'\\') normalized[i] = L'/';
    CreateDirectoryW(L"logs", NULL);
    swprintf(relative, MAX_PATH, L"logs\\v%lu-%llu.code-workspace",
             GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    { DWORD length = GetFullPathNameW(relative, MAX_PATH, path, NULL);
      if (!length || length >= MAX_PATH) return 0; }
    swprintf(settings, 4096,
        L"{\n  \"folders\": [{\"path\": \"%ls\"}],\n  \"settings\": {\n"
        L"    \"window.title\": \"%ls - ${activeEditorShort}${dirty} - ${appName}\",\n"
        L"    \"workbench.startupEditor\": \"none\",\n"
        L"    \"workbench.editor.languageDetection\": false,\n"
        L"    \"editor.autoIndent\": \"none\",\n"
        L"    \"editor.autoClosingBrackets\": \"never\",\n"
        L"    \"editor.autoClosingQuotes\": \"never\",\n"
        L"    \"editor.autoSurround\": \"never\",\n"
        L"    \"editor.quickSuggestions\": {\"other\": \"off\", \"comments\": \"off\", \"strings\": \"off\"},\n"
        L"    \"editor.suggestOnTriggerCharacters\": false,\n"
        L"    \"editor.acceptSuggestionOnEnter\": \"off\",\n"
        L"    \"editor.formatOnType\": false,\n"
        L"    \"editor.formatOnSave\": false,\n"
        L"    \"files.autoSave\": \"off\",\n"
        L"    \"files.eol\": \"\\n\",\n"
        L"    \"files.encoding\": \"utf8\",\n"
        L"    \"files.trimTrailingWhitespace\": false,\n"
        L"    \"files.insertFinalNewline\": false,\n"
        L"    \"files.trimFinalNewlines\": false\n"
        L"  }\n}\n", normalized, marker);
    count = WideCharToMultiByte(CP_UTF8, 0, settings, -1, NULL, 0, NULL, NULL);
    if (!count) return 0;
    bytes = malloc((size_t)count);
    if (!bytes) return 0;
    WideCharToMultiByte(CP_UTF8, 0, settings, -1, bytes, count, NULL, NULL);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) { free(bytes); return 0; }
    if (!WriteFile(file, bytes, (DWORD)count - 1, &written, NULL) || written != (DWORD)count - 1) {
        CloseHandle(file); free(bytes); return 0;
    }
    CloseHandle(file); free(bytes);
    playback_log(p, "created isolated workspace configuration; code will be entered through the editor");
    return 1;
}

static int select_plain_text(Playback *p) {
    return vscode_select_plain_text(p);
}

int coding_same_text(const wchar_t *expected, const wchar_t *actual) {
    for (;;) {
        wchar_t a = *expected, b = *actual;
        if (a == L'\r') { if (expected[1] == L'\n') ++expected; a = L'\n'; }
        if (b == L'\r') { if (actual[1] == L'\n') ++actual; b = L'\n'; }
        if (a != b) return 0;
        if (!a) return 1;
        ++expected; ++actual;
    }
}

static int wait_saved_text(Playback *p, const wchar_t *path, const wchar_t *expected) {
    ULONGLONG started = GetTickCount64(), paused_before = p->paused_ms;
    for (;;) {
        wchar_t *actual = NULL;
        char error[256];
        if (!playback_poll(p)) return 0;
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES &&
            read_utf8(path, &actual, error, sizeof(error))) {
            int matches = coding_same_text(expected, actual);
            free(actual);
            if (matches) return 1;
            if (!*expected) {
                playback_log(p, "new file is not empty; stopped to protect existing content"); return 0;
            }
        }
        if (GetTickCount64() - started - (p->paused_ms - paused_before) > 30000) {
            playback_log(p, "saved file did not reach the expected content within 30 seconds"); return 0;
        }
        if (!playback_wait(p, 100)) return 0;
    }
}

static int desktop_is(HWND window) {
    wchar_t cls[128];
    return window && GetClassNameW(window, cls, 128) &&
        (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW")) && window_process_is(window, L"explorer.exe");
}

static int show_desktop(Playback *p) {
    ULONGLONG started, paused;
    if (!desktop_is(GetForegroundWindow()) && !desktop_shortcut(p, L'D')) return 0;
    started = GetTickCount64(); paused = p->paused_ms;
    while (GetTickCount64() - started - (p->paused_ms - paused) < 10000) {
        HWND foreground;
        if (!playback_poll(p)) return 0;
        foreground = GetForegroundWindow();
        if (desktop_is(foreground)) {
            p->target = foreground;
            return presentation_rest(p, "desktop view is visible");
        }
        if (!playback_wait(p, 50)) return 0;
    }
    playback_log(p, "desktop view did not become foreground"); return 0;
}

static int desktop_directory_valid(const wchar_t *directory) {
    wchar_t desktop[MAX_PATH];
    size_t length;
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, desktop))) return 0;
    length = wcslen(desktop);
    return !_wcsnicmp(desktop, directory, length) && directory[length] == L'\\' &&
        directory[length + 1] && !wcschr(directory + length + 1, L'\\') &&
        GetFileAttributesW(directory) != INVALID_FILE_ATTRIBUTES;
}

static int console_host_is(HWND window) {
    wchar_t cls[128];
    return GetClassNameW(window, cls, 128) &&
        (!wcscmp(cls, L"ConsoleWindowClass") || !wcscmp(cls, L"CASCADIA_HOSTING_WINDOW_CLASS")) &&
        (window_process_is(window, L"cmd.exe") || window_process_is(window, L"conhost.exe") ||
         window_process_is(window, L"WindowsTerminal.exe") || window_process_is(window, L"OpenConsole.exe"));
}

typedef struct { HWND old[64], found; unsigned count, matches; int finding, overflow; } ConsoleWindows;
static BOOL CALLBACK find_console(HWND window, LPARAM parameter) {
    ConsoleWindows *search = (ConsoleWindows *)parameter;
    unsigned i;
    if (!IsWindowVisible(window) || !console_host_is(window)) return TRUE;
    if (!search->finding) {
        if (search->count == 64) { search->overflow = 1; return FALSE; }
        search->old[search->count++] = window;
    } else {
        for (i = 0; i < search->count; ++i) if (search->old[i] == window) return TRUE;
        if (IsWindowEnabled(window)) { search->found = window; ++search->matches; }
    }
    return TRUE;
}

static int wait_console(Playback *p, ConsoleWindows *search) {
    ULONGLONG started, paused;
    /* Windows Terminal can briefly give focus back to Explorer during launch. */
    if (!playback_wait(p, 2500)) return 0;
    started = GetTickCount64(); paused = p->paused_ms; search->finding = 1;
    while (GetTickCount64() - started - (p->paused_ms - paused) < 30000) {
        if (!playback_poll(p)) return 0;
        search->found = NULL; search->matches = 0;
        EnumWindows(find_console, (LPARAM)search);
        if (search->matches == 1) return window_activate_handle(p, search->found);
        if (!playback_wait(p, 100)) return 0;
    }
    playback_log(p, "expected one new Command Prompt window; found %u", search->matches); return 0;
}

static int desktop_launch(Playback *p, const wchar_t *directory) {
    RECT rect;
    ConsoleWindows consoles = {0};
    const wchar_t *leaf = wcsrchr(directory, L'\\') + 1;
    playback_log(p, "step 1/5: show the desktop");
    if (!show_desktop(p)) return 0;
    playback_log(p, "step 2/5: locate and double-click the target folder");
    if (!ui_desktop_folder_rect(p, leaf, &rect) || !presentation_click(p, &rect, 1) ||
        !wait_window(p, L"CabinetWClass", L"explorer.exe", leaf, NULL) ||
        !presentation_rest(p, "target folder is open")) return 0;
    playback_log(p, "step 3/5: click Explorer's address bar and open Command Prompt here");
    EnumWindows(find_console, (LPARAM)&consoles);
    if (consoles.overflow) { playback_log(p, "too many console windows to identify a new one"); return 0; }
    if (!ui_control_rect(p, UIA_EditControlTypeId, L"Address Bar", 0, &rect) ||
        !presentation_click(p, &rect, 0) || !shortcut(p, VK_CONTROL, L'L') ||
        !presentation_rest(p, "address bar selected") || !keyboard_type(p, L"cmd") ||
        !presentation_rest(p, "Command Prompt command entered") || !keyboard_press(p, VK_RETURN) ||
        !wait_console(p, &consoles)) return 0;
    if (!presentation_rest(p, "Command Prompt is open in the project folder")) return 0;
    playback_log(p, "step 4/5: type code . in Command Prompt, then create the file in VS Code");
    return keyboard_type(p, L"code .") && presentation_rest(p, "code . entered") &&
        keyboard_press(p, VK_RETURN);
}

static int create_editor_file(Playback *p, const wchar_t *directory, const wchar_t *output) {
    WORD explorer[] = {VK_CONTROL, VK_SHIFT, L'E'};
    RECT button;
    wchar_t header[512];
    const wchar_t *name = wcsrchr(output, L'\\') + 1;
    swprintf(header, 512, L"Explorer Section: %ls", wcsrchr(directory, L'\\') + 1);
    if (GetFileAttributesW(output) != INVALID_FILE_ATTRIBUTES) {
        playback_log(p, "output filename exists; refusing to overwrite"); return 0;
    }
    playback_log(p, "creating the new file with VS Code's New File button");
    return keyboard_shortcut(p, explorer, 3) && presentation_rest(p, "Explorer panel selected") &&
        ui_control_rect(p, UIA_ButtonControlTypeId, header, 0, &button) &&
        presentation_hover(p, &button) && presentation_rest(p, "folder actions revealed") &&
        ui_control_rect(p, UIA_ButtonControlTypeId, L"New File...", 0, &button) &&
        presentation_click(p, &button, 0) && presentation_rest(p, "new-file name field ready") &&
        keyboard_type(p, name) && presentation_rest(p, "new filename entered") &&
        keyboard_press(p, VK_RETURN) &&
        wait_window(p, NULL, L"Code.exe", name, p->target) &&
        shortcut(p, VK_CONTROL, L'S') && wait_saved_text(p, output, L"");
}

static const struct { const wchar_t *name, *value; WORD choice; } literal_settings[] = {
    {L"editor.autoIndent", L"none", VK_HOME},
    {L"editor.acceptSuggestionOnEnter", L"off", VK_END},
    {L"editor.autoClosingBrackets", L"never", VK_END},
    {L"editor.autoClosingQuotes", L"never", VK_END},
    {L"editor.autoSurround", L"never", VK_END}
};
static int prepare_workspace_indentation(Playback *p) {
    WORD palette[] = {VK_CONTROL, VK_SHIFT, L'P'};
    RECT control;
    size_t i;
    playback_log(p, "configuring literal Plain Text entry through workspace Settings");
    if (!(keyboard_press(p, VK_ESCAPE) && keyboard_press(p, VK_ESCAPE) &&
        keyboard_shortcut(p, palette, 3) && presentation_rest(p, "command palette ready") &&
        keyboard_type(p, L"Preferences: Open Workspace Settings") &&
        presentation_rest(p, "workspace settings command entered") && keyboard_press(p, VK_RETURN) &&
        presentation_rest(p, "workspace settings opened") &&
        ui_wait_control_rect(p, UIA_TabItemControlTypeId, L"Workspace", 0, &control, 15000) &&
        presentation_click(p, &control, 0) && presentation_rest(p, "Workspace scope selected"))) return 0;
    for (i = 0; i < sizeof(literal_settings)/sizeof(*literal_settings); ++i) {
        wchar_t query[160];
        char name[100];
        swprintf(query,160,L"@lang:plaintext @id:%ls",literal_settings[i].name);
        WideCharToMultiByte(CP_UTF8,0,literal_settings[i].name,-1,name,sizeof(name),NULL,NULL);
        playback_log(p,"configuring workspace Plain Text %s",name);
        if (!shortcut(p,VK_CONTROL,L'F') || !shortcut(p,VK_CONTROL,L'A') || !keyboard_type(p,query) ||
            !presentation_rest(p,"Plain Text setting located") ||
            !ui_wait_control_rect(p,UIA_ComboBoxControlTypeId,literal_settings[i].name,0,&control,10000) ||
            !presentation_click(p,&control,0) || !presentation_rest(p,"setting options opened") ||
            !keyboard_press(p,literal_settings[i].choice) || !keyboard_press(p,VK_RETURN) ||
            !presentation_rest(p,"Plain Text setting configured")) return 0;
    }
    return keyboard_press(p,VK_ESCAPE) && presentation_rest(p,"workspace settings overlay dismissed");
}

static int type_code(Playback *p, const wchar_t *source, const wchar_t *output) {
    wchar_t *copy = _wcsdup(source), *block, *end;
    unsigned lines = 0;
    if (!copy) return 0;
    block = copy;
    while (*block) {
        wchar_t saved;
        unsigned chunk_lines = 0;
        end = block;
        while (*end && chunk_lines < 20) { if (*end++ == L'\n') ++chunk_lines; }
        saved = *end; *end = 0;
        if (!keyboard_type_code(p, block, copy) || !shortcut(p, VK_CONTROL, L'S')) { free(copy); return 0; }
        lines += chunk_lines;
        playback_log(p, "typed and saved %u line(s)", lines);
        if (!presentation_rest(p, "saved code checkpoint")) { free(copy); return 0; }
        if (browser_checkpoint_due(p, lines, saved != 0) &&
            (!wait_saved_text(p, output, copy) || !browser_checkpoint(p, lines))) {
            free(copy); return 0;
        }
        *end = saved;
        block = end;
    }
    free(copy);
    return 1;
}

static int coding_action(Playback *p, const wchar_t *directory, const wchar_t *source, int desktop) {
    wchar_t editor[MAX_PATH], workspace[MAX_PATH], marker[80], output[MAX_PATH], command[1024];
    wchar_t parent[MAX_PATH], *last;
    DWORD attrs, length, char_ms = p->char_ms, line_ms = p->line_ms;
    int ok = 0, pacing_enabled = p->typing_pace.enabled;
    char printable[1024];
    if (wcslen(directory) > MAX_PATH - 90) {
        playback_log(p, "destination path is too long for this desktop action"); return 0;
    }
    wcscpy(parent, directory);
    last = wcsrchr(parent, L'\\');
    if (!last || last == parent + 2 || !last[1]) {
        playback_log(p, "choose a named project folder below an existing parent"); return 0;
    }
    *last = 0;
    attrs = GetFileAttributesW(parent);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        playback_log(p, "destination parent directory must already exist"); return 0;
    }
    attrs = GetFileAttributesW(directory);
    if (attrs != INVALID_FILE_ATTRIBUTES &&
        (!(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT))) {
        playback_log(p, "destination must be a normal directory, not a file or junction"); return 0;
    }
    if (desktop && !desktop_directory_valid(directory)) {
        playback_log(p, "desktop workflow needs an existing folder directly on the current user's desktop"); return 0;
    }
    length = GetEnvironmentVariableW(L"LOCALAPPDATA", editor, MAX_PATH);
    if (!length || length + wcslen(L"\\Programs\\Microsoft VS Code\\Code.exe") >= MAX_PATH) return 0;
    wcscat(editor, L"\\Programs\\Microsoft VS Code\\Code.exe");
    if (GetFileAttributesW(editor) == INVALID_FILE_ATTRIBUTES) {
        playback_log(p, "VS Code was not found under LOCALAPPDATA/Programs/Microsoft VS Code"); return 0;
    }
    swprintf(output, MAX_PATH, L"%ls\\vehicle_lookup_%lu_%llu.py", directory,
        GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    swprintf(marker, 80, L"Vehicle Coding Demo %lu", GetCurrentProcessId());
    if (!desktop) {
        if (!write_workspace(p, directory, workspace, marker)) {
            playback_log(p, "cannot create workspace configuration in logs"); return 0;
        }
        swprintf(command, 1024, L"\"%ls\" -n --disable-extensions \"%ls\" \"%ls\"", editor, workspace, output);
        if (wcslen(command) >= 259) {
            playback_log(p, "VS Code launch text is too long for the Run dialog; use a shorter working directory"); return 0;
        }
    }
    WideCharToMultiByte(CP_UTF8, 0, output, -1, printable, sizeof(printable), NULL, NULL);
    playback_log(p, "new file: %s", printable);
    if (!dismiss_demo_save_dialogs(p)) goto done;
    if (GetFileAttributesW(output) != INVALID_FILE_ATTRIBUTES) {
        playback_log(p, "output filename exists; refusing to overwrite"); goto done;
    }
    if (desktop) {
        const wchar_t *folder = wcsrchr(directory, L'\\') + 1;
        if (!desktop_launch(p, directory) ||
            !wait_window(p, NULL, L"Code.exe", folder, NULL) || !presentation_rest(p, "project is open in VS Code") ||
            !create_editor_file(p, directory, output)) goto done;
    } else {
        playback_log(p, "opening the project in File Explorer");
        if (!open_folder(p, directory)) goto done;
        playback_log(p, "opening VS Code through Windows Run");
        if (!desktop_shortcut(p, L'R') || !wait_window(p, L"#32770", L"explorer.exe", NULL, NULL) ||
            !shortcut(p, VK_CONTROL, L'A') || !keyboard_type(p, command) || !keyboard_press(p, VK_RETURN)) goto done;
    }
    if (!desktop && (!wait_window(p, NULL, L"Code.exe", marker, NULL) || !playback_wait(p, 3000))) goto done;
    playback_log(p, "VS Code workspace is ready; waiting for the new file editor");
    last = wcsrchr(output, L'\\');
    if (!wait_window(p, NULL, L"Code.exe", last + 1, p->target)) goto done;
    playback_log(p, "saving the new named file and waiting for disk verification");
    if ((!IsZoomed(p->target) && (!shortcut(p, VK_MENU, VK_SPACE) ||
        !presentation_rest(p, "window menu opened") || !keyboard_press(p, L'X') ||
        !presentation_rest(p, "editor maximized"))) ||
        !shortcut(p, VK_CONTROL, L'S') || !wait_saved_text(p, output, L"")) goto done;
    if (desktop) {
        RECT tab;
        if (!ui_control_rect(p, UIA_TabItemControlTypeId, last + 1, 1, &tab) ||
            !presentation_click(p, &tab, 0) || !presentation_rest(p, "new file tab selected")) goto done;
    }
    /* Re-establish the fresh editor's empty state after the language picker closes. */
    if (!select_plain_text(p) || !keyboard_press(p, VK_ESCAPE) ||
        !shortcut(p, VK_CONTROL, L'1') || !shortcut(p, VK_CONTROL, L'A') ||
        !keyboard_press(p, VK_BACK) || !shortcut(p, VK_CONTROL, L'S') ||
        !wait_saved_text(p, output, L"")) goto done;
    if (desktop) {
        RECT tab;
        if (!prepare_workspace_indentation(p) ||
            !ui_control_rect(p, UIA_TabItemControlTypeId, last + 1, 1, &tab) ||
            !presentation_click(p, &tab, 0) || !presentation_rest(p, "new file ready for code entry") ||
            !shortcut(p, VK_CONTROL, L'1')) goto done;
    }
    p->char_ms = char_ms; p->line_ms = line_ms;
    p->typing_pace.enabled = pacing_enabled;
    playback_log(p, "typing the reference code; F8 pauses and F12 stops");
    if (desktop) playback_log(p, "step 5/5: type the reference code in the new editor");
    if (p->browser.every_lines) p->browser.next_line = p->browser.every_lines;
    if (!type_code(p, source, output) || !wait_saved_text(p, output, source)) goto done;
    playback_log(p, "verified: saved code matches the reference (line endings normalized)");
    ok = 1;
done:
    p->char_ms = char_ms; p->line_ms = line_ms;
    p->typing_pace.enabled = pacing_enabled;
    return ok;
}

int coding_reference(Playback *p, const wchar_t *directory, const wchar_t *source) {
    return coding_action(p, directory, source, 0);
}
int coding_desktop_reference(Playback *p, const wchar_t *directory, const wchar_t *source) {
    return coding_action(p, directory, source, 1);
}

static int select_file_tab(Playback *p, const wchar_t *name) {
    RECT tab;
    return ui_control_rect(p,UIA_TabItemControlTypeId,name,1,&tab) && presentation_click(p,&tab,0) &&
        presentation_rest(p,"reference file selected") && shortcut(p,VK_CONTROL,L'1');
}
int coding_workspace_pacing_ready(const wchar_t *text) {
    JsonValue *settings = NULL;
    const JsonValue *language;
    char error[256]; int ok = 1; size_t i;
    if (!json_parse(text,&settings,error,sizeof(error))) return 0;
    language = json_get(settings,L"[plaintext]");
    for (i = 0; i < sizeof(literal_settings)/sizeof(*literal_settings); ++i) {
        const JsonValue *v = json_get(language,literal_settings[i].name);
        if (!v || v->kind != JSON_STRING || wcscmp(v->string,literal_settings[i].value)) { ok = 0; break; }
    }
    json_free(settings); return ok;
}
static int workspace_plaintext_ready(const wchar_t *directory) {
    wchar_t path[MAX_PATH], *text = NULL;
    char error[256]; int ok;
    swprintf(path,MAX_PATH,L"%ls\\.vscode\\settings.json",directory);
    if (!read_utf8(path,&text,error,sizeof(error))) return 0;
    ok = coding_workspace_pacing_ready(text); free(text); return ok;
}
/* In the Windows Save As dialog the "File name" box is the only Edit control inside a ComboBox
   ("Save as type" is a ComboBox without one; the search box is a different class). */
static BOOL CALLBACK find_name_box(HWND window, LPARAM result) {
    wchar_t cls[32];
    HWND parent = GetParent(window);
    if (IsWindowVisible(window) && GetClassNameW(window,cls,32) && !wcscmp(cls,L"Edit") &&
        parent && GetClassNameW(parent,cls,32) && !wcscmp(cls,L"ComboBox")) {
        *(HWND *)result = window; return FALSE;
    }
    return TRUE;
}
/* Reads the Save As "File name" box. Returns 1 when it holds exactly `expected`, 0 when it holds
   something else, -1 when it cannot be read. */
static int save_as_name_is(Playback *p, HWND dialog, const wchar_t *expected) {
    HWND box = NULL;
    wchar_t text[2*MAX_PATH];
    DWORD_PTR length = 0;
    char shown[3*2*MAX_PATH];
    EnumChildWindows(dialog,find_name_box,(LPARAM)&box);
    if (!box || !SendMessageTimeoutW(box,WM_GETTEXT,2*MAX_PATH,(LPARAM)text,SMTO_ABORTIFHUNG,2000,&length))
        return -1;
    text[length < 2*MAX_PATH ? length : 2*MAX_PATH-1] = 0;
    if (!wcscmp(text,expected)) return 1;
    WideCharToMultiByte(CP_UTF8,0,text,-1,shown,sizeof(shown),NULL,NULL);
    playback_log(p,"Save As filename reads \"%s\", not the destination path",shown);
    return 0;
}
static int create_reference_file(Playback *p, const wchar_t *output) {
    WORD save_as[] = {VK_CONTROL,VK_SHIFT,L'S'};
    HWND editor = p->target, dialog;
    const wchar_t *name = wcsrchr(output,L'\\')+1;
    int attempt, matches = 0;
    if (GetFileAttributesW(output) != INVALID_FILE_ATTRIBUTES) {
        playback_log(p,"output filename exists; refusing to overwrite"); return 0;
    }
    playback_log(p,"creating an untitled file and saving it to the exact configured path");
    if (!keyboard_press(p,VK_ESCAPE) || !shortcut(p,VK_CONTROL,L'N') ||
        !presentation_rest(p,"new untitled editor ready") || !keyboard_shortcut(p,save_as,3) ||
        !wait_window(p,L"#32770",L"Code.exe",L"Save As",NULL)) return 0;
    dialog = p->target;
    if (GetAncestor(dialog,GA_ROOTOWNER) != editor) {
        playback_log(p,"Save As belongs to a different editor window"); return 0;
    }
    if (!shortcut(p,VK_MENU,L'N') || !presentation_rest(p,"Save As filename field selected")) return 0;
    /* A character lost while typing (once, the ':' after the drive letter) makes Windows report
       "Path does not exist" and leaves modal dialogs that block VS Code. Check before pressing Enter. */
    for (attempt = 0; attempt < 2 && matches != 1; ++attempt) {
        if (!shortcut(p,VK_CONTROL,L'A') || !keyboard_type(p,output) ||
            !presentation_rest(p,"exact destination and original filename entered")) return 0;
        matches = save_as_name_is(p,dialog,output);
        if (matches < 0) { playback_log(p,"Save As filename could not be read back; not verified"); break; }
    }
    if (!matches) {
        playback_log(p,"Save As filename is still wrong; closing Save As");
        keyboard_press(p,VK_ESCAPE);
        return 0;
    }
    if (GetFileAttributesW(output) != INVALID_FILE_ATTRIBUTES) {
        playback_log(p,"output appeared while Save As was open; refusing to overwrite"); return 0;
    }
    return keyboard_press(p,VK_RETURN) && wait_window(p,NULL,L"Code.exe",name,editor) &&
        wait_saved_text(p,output,L"");
}
static int select_lf(Playback *p) {
    return vscode_select_lf(p);
}
static int open_existing_reference(Playback *p, const wchar_t *path, HWND editor) {
    const wchar_t *name = wcsrchr(path,L'\\')+1;
    return shortcut(p,VK_CONTROL,L'P') && presentation_rest(p,"file picker ready") &&
        keyboard_type(p,path) && presentation_rest(p,"exact reference path entered") &&
        keyboard_press(p,VK_RETURN) && wait_window(p,NULL,L"Code.exe",name,editor) &&
        select_file_tab(p,name);
}
typedef struct { Playback *p; const wchar_t *path; int noticed; char error[256]; } SaveContext;
static int saved_reference_read(void *context, wchar_t **snapshot) {
    SaveContext *save = context;
    wchar_t *text = NULL, title[512];
    *snapshot = NULL;
    /* The standard VS Code title keeps its dirty marker until the save completes. */
    if (GetWindowTextW(save->p->target,title,512) && wcschr(title,0x25cf)) {
        snprintf(save->error,sizeof(save->error),"VS Code still has unsaved changes"); return 0;
    }
    if (!read_utf8(save->path,&text,save->error,sizeof(save->error))) return 0;
    *snapshot = reference_normalize(text); free(text);
    return *snapshot != NULL;
}
static int saved_reference_poll(void *context) {
    SaveContext *save = context;
    return playback_poll(save->p) && playback_guard(save->p);
}
static int saved_reference_wait(void *context, unsigned milliseconds) {
    SaveContext *save = context;
    if (!save->noticed) {
        playback_log(save->p,"save pending; waiting for VS Code and the saved checkpoint to finish"); save->noticed = 1;
    }
    return playback_wait(save->p,(DWORD)milliseconds);
}
static uint64_t saved_reference_now(void *context) {
    SaveContext *save = context;
    return GetTickCount64()-save->p->paused_ms;
}
static int reference_saved_offset(Playback *p, const wchar_t *path, const wchar_t *expected,
                                   const size_t *required, size_t *offset) {
    SaveContext context = {p,path,0,{0}};
    SavedTextIO io = {&context,saved_reference_read,saved_reference_poll,saved_reference_wait,saved_reference_now};
    SavedTextResult result;
    SavedTextStatus status;
    if (!shortcut(p,VK_CONTROL,L'S')) return 0;
    status = saved_text_wait(expected,required,&io,30000,&result);
    if (status == SAVED_TEXT_READY) { *offset = result.length; return 1; }
    if (status == SAVED_TEXT_DIFFERENT) {
        playback_log(p,"saved checkpoint differs (saved %zu characters%s); no source was replaced",
            result.length,required ? ", expected the typed checkpoint" : "");
        if (result.saved_character) playback_log(p,"first difference at character %zu: expected U+%04x, saved U+%04x",
            result.mismatch,(unsigned)result.expected_character,(unsigned)result.saved_character);
    } else if (status == SAVED_TEXT_TIMEOUT) {
        playback_log(p,"save did not finish within 30 active seconds (last saved length %zu, required %zu): %s",
            result.length,required ? *required : result.length,context.error);
    }
    return 0;
}
static int reference_visit(Playback *p, ReferenceConfig *c, CodingSchedule *schedule,
                            HWND *browser, size_t *previous_topic) {
    size_t topic = reference_pick_topic(c,schedule,*previous_topic);
    DWORD duration = coding_schedule_visit(schedule);
    char url[900];
    if (topic == SIZE_MAX) { playback_log(p,"no browser choice is available"); return 0; }
    *previous_topic = topic;
    playback_log(p,"coding interval complete after %.1f seconds; browser choice %zu, visit %.1f seconds",
        schedule->coding_ms/1000.0,topic+1,duration/1000.0);
    WideCharToMultiByte(CP_UTF8,0,c->topics[topic].url,-1,url,sizeof(url),NULL,NULL);
    playback_log(p,"browser category: %s; URL: %s",reference_category_name(c->topics[topic].category),url);
    if (c->topics[topic].category == REFERENCE_RESEARCH) {
        char query[600];
        WideCharToMultiByte(CP_UTF8,0,c->topics[topic].query,-1,query,sizeof(query),NULL,NULL);
        unsigned results = coding_schedule_pick(schedule,c->results_to_open[0] ? c->results_to_open[0] : 1,
            c->results_to_open[1] ? c->results_to_open[1] : 1);
        playback_log(p,"research query: %s (%u result%s)",query,results,results == 1 ? "" : "s");
        if (!browser_research_visit(p,c->topics[topic].query,duration,schedule->state,browser,results)) return 0;
    } else if (!browser_topic_visit(p,c->topics[topic].query,c->topics[topic].url,duration,schedule->state,browser)) return 0;
    coding_schedule_reset(schedule);
    playback_log(p,"next browser visit after %.1f coding seconds",schedule->next_interval_ms/1000.0);
    return 1;
}
static int type_reference_file(Playback *p, ReferenceConfig *c, ReferenceFile *f, const wchar_t *path,
                                CodingSchedule *schedule, HWND *browser, size_t *previous_topic) {
    size_t length = wcslen(f->normalized), offset = f->offset;
    while (offset < length) {
        size_t consumed = 0, verified;
        ULONGLONG started = GetTickCount64(), paused = p->paused_ms;
        DWORD budget = 45000;
        if (c->browser_enabled) {
            uint64_t remaining = schedule->coding_ms >= schedule->next_interval_ms ? 1 :
                schedule->next_interval_ms-schedule->coding_ms;
            if (remaining < budget) budget = (DWORD)remaining;
        }
        if (!keyboard_type_slice(p,f->normalized+offset,f->normalized,budget,&consumed)) {
            /* Preserve an interrupted prefix through the editor if focus and the desktop are still usable. */
            playback_log(p,"reference interrupted; restart the launcher to resume its last saved prefix"); return 0;
        }
        offset += consumed;
        coding_schedule_add(schedule,GetTickCount64()-started-(p->paused_ms-paused));
        started = GetTickCount64(); paused = p->paused_ms;
        if (!reference_saved_offset(p,path,f->normalized,&offset,&verified)) return 0;
        if (verified != offset) {
            playback_log(p,"saved checkpoint length %zu did not match %zu typed characters",verified,offset); return 0;
        }
        f->offset = offset;
        playback_log(p,"verified reference checkpoint: %zu/%zu characters",offset,length);
        if (!presentation_rest(p,"saved reference checkpoint")) return 0;
        coding_schedule_add(schedule,GetTickCount64()-started-(p->paused_ms-paused));
        if (c->browser_enabled) {
            uint64_t remaining = coding_schedule_due(schedule) ? 0 : schedule->next_interval_ms-schedule->coding_ms;
            playback_log(p,"next browser visit in %.1f coding minute(s)",remaining/60000.0);
        }
        if (c->browser_enabled && coding_schedule_due(schedule) && offset < length) {
            if (!reference_visit(p,c,schedule,browser,previous_topic) ||
                !reference_saved_offset(p,path,f->normalized,&offset,&verified) || verified != offset ||
                !shortcut(p,VK_CONTROL,VK_END)) return 0;
        }
    }
    f->done = 1; return wait_saved_text(p,path,f->normalized);
}
int coding_reference_batch(Playback *p) {
    ReferenceConfig *c = p->reference_config;
    CodingSchedule schedule;
    HWND browser = NULL, editor;
    RECT ready; wchar_t header[256];
    size_t i, remaining = 0;
    size_t previous_topic = SIZE_MAX;
    char error[512] = {0}; uint32_t seed;
    if (!c || !reference_config_preflight(c,error,sizeof(error))) {
        playback_log(p,"reference batch preflight failed: %s",c ? error : "config missing"); return 0;
    }
    for (i = 0; i < c->file_count; ++i) remaining += c->files[i].enabled && !c->files[i].done;
    if (!remaining) { playback_log(p,"all enabled references are already complete; no input needed"); return 1; }
    if (!desktop_directory_valid(c->destination)) {
        playback_log(p,"reference destination must be a folder directly on the current user's Desktop"); return 0;
    }
    seed = c->seed ? c->seed : action_recording_seed();
    playback_log(p,"reference batch: %zu files remaining, presentation seed %lu",remaining,(unsigned long)seed);
    action_pace_configure(&p->action_pace,c->action_pause_ms[0],c->action_pause_ms[1],c->mouse_move_ms[0],c->mouse_move_ms[1],seed);
    typing_pace_configure(&p->typing_pace,c->char_ms[0],c->char_ms[1],c->newline_ms[0],c->newline_ms[1],seed+1);
    typing_pace_breaks(&p->typing_pace,c->pause_every_chars[0],c->pause_every_chars[1],c->typing_pause_ms[0],c->typing_pause_ms[1]);
    coding_schedule_init(&schedule,c->interval_seconds[0]*1000,c->interval_seconds[1]*1000,
        c->visit_seconds[0]*1000,c->visit_seconds[1]*1000,seed+2);
    if (!desktop_launch(p,c->destination) ||
        !wait_window(p,NULL,L"Code.exe",wcsrchr(c->destination,L'\\')+1,NULL) ||
        !presentation_rest(p,"reference project opened")) return 0;
    editor = p->target;
    swprintf(header,256,L"Explorer Section: %ls",wcsrchr(c->destination,L'\\')+1);
    { WORD explorer_keys[] = {VK_CONTROL,VK_SHIFT,L'E'};
      if (!keyboard_press(p,VK_ESCAPE) || !keyboard_shortcut(p,explorer_keys,3) ||
          !presentation_rest(p,"project Explorer selected") ||
          !ui_wait_control_rect(p,UIA_ButtonControlTypeId,header,0,&ready,30000)) return 0;
    }
    if (!IsZoomed(editor)) {
        if (!ui_wait_control_rect(p,UIA_ButtonControlTypeId,L"Maximize",0,&ready,10000) ||
            !presentation_click(p,&ready,0) || !presentation_rest(p,"editor maximized")) return 0;
    }
    if (workspace_plaintext_ready(c->destination)) playback_log(p,"verified existing Plain Text workspace settings");
    else if (!prepare_workspace_indentation(p)) return 0;
    if (!workspace_plaintext_ready(c->destination)) {
        playback_log(p,"workspace settings did not disable automatic text insertion; stopped before entering source code"); return 0;
    }
    playback_log(p,"next browser visit after %.1f coding seconds",schedule.next_interval_ms/1000.0);
    for (i = 0; i < c->file_count; ++i) {
        ReferenceFile *f = c->files+i;
        wchar_t path[MAX_PATH]; char name[200];
        DWORD attrs; size_t verified;
        int fresh;
        ULONGLONG started, paused;
        p->line = (unsigned)i+1;
        if (!f->enabled || f->done) continue;
        if (!playback_poll(p)) return 0;
        if (c->browser_enabled && coding_schedule_due(&schedule) &&
            !reference_visit(p,c,&schedule,&browser,&previous_topic)) return 0;
        WideCharToMultiByte(CP_UTF8,0,f->name,-1,name,sizeof(name),NULL,NULL);
        playback_log(p,"reference %zu/%zu: %s",i+1,c->file_count,name);
        swprintf(path,MAX_PATH,L"%ls\\%ls",c->destination,f->name);
        attrs = GetFileAttributesW(path);
        fresh = attrs == INVALID_FILE_ATTRIBUTES;
        started = GetTickCount64(); paused = p->paused_ms;
        error[0] = 0;
        if (fresh) {
            if (!create_reference_file(p,path) || !reference_config_claim(c,i,error,sizeof(error))) {
                playback_log(p,"cannot create/record reference ownership: %s",error); return 0;
            }
            f->offset = 0;
        } else if (!f->owned || !open_existing_reference(p,path,editor)) {
            playback_log(p,"output changed after preflight, or the owned file could not be opened"); return 0;
        }
        if (!select_file_tab(p,f->name) || !select_plain_text(p) || !keyboard_press(p,VK_ESCAPE) ||
            !shortcut(p,VK_CONTROL,L'1')) return 0;
        if (fresh && (!shortcut(p,VK_CONTROL,L'A') || !keyboard_press(p,VK_BACK) ||
            !shortcut(p,VK_CONTROL,L'S') || !wait_saved_text(p,path,L""))) return 0;
        if (!select_lf(p) || !keyboard_press(p,VK_ESCAPE) || !shortcut(p,VK_CONTROL,L'1') ||
            !reference_saved_offset(p,path,f->normalized,NULL,&verified) || !shortcut(p,VK_CONTROL,VK_END)) return 0;
        f->offset = verified;
        coding_schedule_add(&schedule,GetTickCount64()-started-(p->paused_ms-paused));
        if (!type_reference_file(p,c,f,path,&schedule,&browser,&previous_topic)) return 0;
        playback_log(p,"completed %s: original filename and code text verified (LF line endings)",name);
        /* Keep only the current reference tab, reducing accessible-name ambiguity in a long run. */
        if (!shortcut(p,VK_CONTROL,L'W') || !presentation_rest(p,"completed reference tab closed")) return 0;
    }
    playback_log(p,"all enabled reference files completed and verified"); return 1;
}
