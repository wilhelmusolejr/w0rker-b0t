#include "project_action.h"
#include "coding_steps.h"
#include "human_motion.h"
#include "side_apps.h"
#include "keyboard.h"
#include "mouse.h"
#include "presentation.h"
#include "ui_targets.h"
#include "window_manager.h"
#include <uiautomationclient.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void utf8(const wchar_t *text, char *out, size_t cap) {
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out, (int)cap, NULL, NULL);
}
static int shortcut(Playback *p, WORD a, WORD b) {
    WORD keys[2];
    keys[0] = a; keys[1] = b;
    return keyboard_shortcut(p, keys, 2);
}
static int save_history(Playback *p, CodingProject *j) {
    char error[256];
    if (project_history_save(j->history, error, sizeof(error))) return 1;
    playback_log(p, "cannot record project progress: %s", error);
    return 0;
}

/* ---------- dry run ---------- */

void coding_project_print(const CodingProject *j) {
    const ProjectPlan *plan = j->plan;
    const ProjectReference *r = j->reference;
    char text[3 * MAX_PATH];
    size_t i, typed = 0, copied = 0;
    unsigned base_char_ms = (j->config->char_ms[0] + j->config->char_ms[1]) / 2;
    double average_ms = typing_speed_delay(base_char_ms, base_char_ms, j->profile->typing_speed);
    utf8(j->profile->name, text, sizeof(text));
    printf("Profile: %s (typing speed %u, mistype level %u)\n", text, j->profile->typing_speed, j->profile->mistype_level);
    utf8(r->name, text, sizeof(text)); printf("Reference project: %s (%zu files, %zu folders)\n", text, r->file_count, r->folder_count);
    utf8(plan->destination, text, sizeof(text));
    printf("Project folder: %s%s\n", text, plan->fresh ? " (will be created)" : plan->history ? " (continuing)" : "");
    if (plan->missing_folders) printf("Subfolders to create with mkdir: %zu\n", plan->missing_folders);
    for (i = 0; i < r->file_count; ++i) {
        const ProjectStep *s = &plan->steps[i];
        utf8(r->files[i].relative, text, sizeof(text));
        if (s->kind == STEP_CREATE || s->kind == STEP_RESUME) {
            ++typed;
            printf("  %-8s %s (%zu characters%s)\n", project_step_name(s->kind), text, r->files[i].length - s->offset,
                s->kind == STEP_RESUME ? ", resuming" : "");
        } else if (s->kind == STEP_COPY) { ++copied; printf("  %-8s %s (%llu bytes, not typed)\n", "COPY", text, r->files[i].bytes); }
        else if (s->kind == STEP_CONFLICT) printf("  %-8s %s: %s\n", "CONFLICT", text, s->reason);
        else printf("  %-8s %s\n", "DONE", text);
    }
    printf("To do: type %zu file(s), %zu characters (about %.1f hours of typing before pauses); copy %zu file(s).\n",
        typed, plan->remaining_characters, plan->remaining_characters * average_ms / 3600000.0, copied);
    printf("Typing %u-%u ms/character; idle %u%% of checkpoints for %u-%u s; mouse wander %u%%; checkpoint every %u s.\n",
        j->config->char_ms[0], j->config->char_ms[1], j->config->idle_percent, j->config->idle_seconds[0],
        j->config->idle_seconds[1], j->config->wander_percent, j->config->checkpoint_seconds);
    if (plan->conflicts) printf("%zu conflict(s): nothing will be typed until they are resolved.\n", plan->conflicts);
}

/* ---------- opening the project ---------- */

static int wait_folder(Playback *p, const wchar_t *path) {
    ULONGLONG started = GetTickCount64(), paused = p->paused_ms;
    while (GetTickCount64() - started - (p->paused_ms - paused) < 10000) {
        DWORD attrs = GetFileAttributesW(path);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) return 1;
        if (!playback_wait(p, 200)) return 0;
    }
    return 0;
}
/* Ctrl+Shift+N makes a new folder in the focused Explorer view or desktop; type its name and confirm. */
static int new_folder(Playback *p, const wchar_t *name, const wchar_t *path) {
    WORD keys[] = {VK_CONTROL, VK_SHIFT, L'N'};
    char text[3 * MAX_PATH];
    utf8(path, text, sizeof(text));
    playback_log(p, "creating the project folder %s (Ctrl+Shift+N)", text);
    if (!keyboard_shortcut(p, keys, 3) || !playback_wait(p, 900) || !shortcut(p, VK_CONTROL, L'A') ||
        !keyboard_type(p, name) || !presentation_rest(p, "folder name typed") || !keyboard_press(p, VK_RETURN)) return 0;
    if (wait_folder(p, path)) return presentation_rest(p, "project folder created");
    playback_log(p, "the new folder did not appear with the expected name; stopped");
    return 0;
}
static int explorer_address(Playback *p, const wchar_t *path) {
    RECT rect;
    return ui_control_rect(p, UIA_EditControlTypeId, L"Address Bar", 0, &rect) && presentation_click(p, &rect, 0) &&
        shortcut(p, VK_CONTROL, L'L') && presentation_rest(p, "address bar selected") &&
        keyboard_type(p, path) && keyboard_press(p, VK_RETURN) && playback_wait(p, 1500);
}
static int open_project_folder(Playback *p, CodingProject *j) {
    const wchar_t *destination = j->plan->destination, *name = wcsrchr(destination, L'\\') + 1;
    wchar_t parent[MAX_PATH], desktop[MAX_PATH];
    wcscpy(parent, destination);
    parent[name - destination - 1] = 0;
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, SHGFP_TYPE_CURRENT, desktop)) && !_wcsicmp(parent, desktop)) {
        RECT icon;
        playback_log(p, "showing the desktop");
        if (!coding_step_show_desktop(p)) return 0;
        if (j->plan->fresh && !new_folder(p, name, destination)) return 0;
        playback_log(p, "opening the project folder from the desktop");
        if (!playback_wait(p, 800) || !ui_desktop_folder_rect(p, name, &icon) || !presentation_click(p, &icon, 1)) return 0;
    } else {
        const wchar_t *leaf = wcsrchr(parent, L'\\') ? wcsrchr(parent, L'\\') + 1 : parent;
        playback_log(p, "opening File Explorer (Win+E)");
        window_fix_target(p);
        if (!coding_step_win_key(p, L'E') || !coding_step_wait_window(p, L"CabinetWClass", L"explorer.exe", NULL, NULL) ||
            !presentation_rest(p, "File Explorer is open")) return 0;
        if (j->plan->fresh) {
            if (!explorer_address(p, parent) || !coding_step_wait_window(p, L"CabinetWClass", L"explorer.exe", leaf, NULL) ||
                !new_folder(p, name, destination)) return 0;
            playback_log(p, "opening the new folder (Enter)");
            if (!keyboard_press(p, VK_RETURN)) return 0;
        } else if (!explorer_address(p, destination)) return 0;
    }
    return coding_step_wait_window(p, L"CabinetWClass", L"explorer.exe", name, NULL) &&
        presentation_rest(p, "project folder is open");
}
/* Create every missing folder in Command Prompt. mkdir makes parent folders too, so only the
   deepest missing folders are typed. */
static int make_folders(Playback *p, CodingProject *j) {
    const ProjectReference *r = j->reference;
    size_t i, k;
    for (i = 0; i < r->folder_count; ++i) {
        wchar_t path[MAX_PATH], command[MAX_PATH + 16];
        size_t length = wcslen(r->folders[i]);
        int deeper = 0;
        swprintf(path, MAX_PATH, L"%ls\\%ls", j->plan->destination, r->folders[i]);
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) continue;
        for (k = 0; k < r->folder_count && !deeper; ++k)
            deeper = k != i && !_wcsnicmp(r->folders[k], r->folders[i], length) && r->folders[k][length] == L'\\';
        if (deeper) continue;
        swprintf(command, MAX_PATH + 16, wcschr(r->folders[i], L' ') ? L"mkdir \"%ls\"" : L"mkdir %ls", r->folders[i]);
        if (!keyboard_type(p, command) || !keyboard_press(p, VK_RETURN) ||
            !playback_wait(p, 500 + (DWORD)(GetTickCount64() % 900))) return 0;
    }
    for (i = 0; i < r->folder_count; ++i) {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%ls\\%ls", j->plan->destination, r->folders[i]);
        if (!wait_folder(p, path)) {
            char text[3 * MAX_PATH];
            utf8(r->folders[i], text, sizeof(text));
            playback_log(p, "folder %s was not created; stopped", text); return 0;
        }
    }
    return 1;
}
static int open_in_vscode(Playback *p, CodingProject *j, HWND *editor) {
    const wchar_t *name = wcsrchr(j->plan->destination, L'\\') + 1;
    wchar_t header[256];
    WORD explorer_keys[] = {VK_CONTROL, VK_SHIFT, L'E'};
    RECT ready;
    if (!coding_step_console_here(p)) return 0;
    if (j->plan->missing_folders) {
        playback_log(p, "creating %zu subfolder(s) with mkdir", j->plan->missing_folders);
        if (!make_folders(p, j)) return 0;
    }
    playback_log(p, "opening the project in VS Code (code .)");
    if (!keyboard_type(p, L"code .") || !presentation_rest(p, "code . entered") || !keyboard_press(p, VK_RETURN) ||
        !coding_step_wait_window(p, NULL, L"Code.exe", name, NULL) || !presentation_rest(p, "project opened in VS Code")) return 0;
    *editor = p->target;
    if (!IsZoomed(*editor)) {
        if (!ui_wait_control_rect(p, UIA_ButtonControlTypeId, L"Maximize", 0, &ready, 10000) ||
            !presentation_click(p, &ready, 0) || !presentation_rest(p, "editor maximized")) return 0;
    }
    swprintf(header, 256, L"Explorer Section: %ls", name);
    return keyboard_press(p, VK_ESCAPE) && keyboard_shortcut(p, explorer_keys, 3) &&
        presentation_rest(p, "project Explorer selected") &&
        ui_wait_control_rect(p, UIA_ButtonControlTypeId, header, 0, &ready, 30000) &&
        coding_step_workspace(p, j->plan->destination);
}

/* ---------- breaks between checkpoints ---------- */

static int wander(Playback *p, MotionRng *rng) {
    RECT client, area;
    POINT origin = {0, 0};
    unsigned moves = motion_between(rng, 1, 3), i;
    if (!GetClientRect(p->target, &client) || !ClientToScreen(p->target, &origin)) return 1;
    OffsetRect(&client, origin.x, origin.y);
    /* Over the code area: right of the side bar, below the tabs, above the status bar. */
    area.left = client.left + (client.right - client.left) * 30 / 100; area.right = client.right - 60;
    area.top = client.top + 120; area.bottom = client.bottom - 80;
    playback_log(p, "random mouse movement while reading the code");
    for (i = 0; i < moves; ++i) {
        POINT end;
        if (!motion_rest_point(rng, &area, &end)) return 1;
        if (!mouse_move_human(p, rng, end, MOTION_REST, 160) || !playback_wait(p, motion_between(rng, 300, 1500))) return 0;
    }
    return 1;
}
static int checkpoint_break(Playback *p, const ProgrammingConfig *c, MotionRng *rng) {
    if (motion_chance(rng, c->wander_percent) && !wander(p, rng)) return 0;
    /* A full work session uses its own scheduled idles to control the day's idle share. */
    if (!p->work_session && motion_chance(rng, c->idle_percent)) {
        unsigned seconds = motion_between(rng, c->idle_seconds[0], c->idle_seconds[1]);
        playback_log(p, "random idle: %u seconds with no input", seconds);
        if (!playback_wait(p, seconds * 1000)) return 0;
    }
    return 1;
}

/* ---------- files ---------- */

static int copy_file(Playback *p, CodingProject *j, size_t index) {
    const ProjectFile *f = &j->reference->files[index];
    wchar_t source[MAX_PATH], path[MAX_PATH];
    char text[3 * MAX_PATH];
    swprintf(source, MAX_PATH, L"%ls\\%ls", j->reference->root, f->relative);
    swprintf(path, MAX_PATH, L"%ls\\%ls", j->plan->destination, f->relative);
    utf8(f->relative, text, sizeof(text));
    if (!CopyFileW(source, path, TRUE)) { playback_log(p, "cannot copy %s (error %lu)", text, GetLastError()); return 0; }
    playback_log(p, "copied %s (image, lock file, or not text; not typed)", text);
    return project_history_mark(j->plan->history, f->relative, "", HISTORY_COPIED) && save_history(p, j);
}
/* Asked to stop: type to the end of the current line, save, and confirm the saved text. The file
   stays partial in the history, so the next run resumes from here. */
static int finish_line(Playback *p, CodingProject *j, const ProjectFile *f, const wchar_t *path, size_t offset) {
    size_t end = offset, verified;
    wchar_t *text = f->text, kept;
    char name[3 * MAX_PATH];
    int ok = 1;
    while (end < f->length && text[end] != L'\n') ++end;
    if (end < f->length) ++end;
    utf8(f->relative, name, sizeof(name));
    if (end > offset) {
        playback_log(p, "finishing the current line of %s before stopping (%zu characters)", name, end - offset);
        /* Type exactly up to the line end: cut the text there for a moment. */
        kept = text[end]; text[end] = 0;
        ok = keyboard_type_slice(p, text + offset, text, 0, NULL);
        text[end] = kept;
        if (!ok) return 0;
    }
    if (!coding_step_saved_offset(p, path, f->text, &end, &verified) || verified != end) {
        playback_log(p, "the saved file does not match what was typed; stopped"); return 0;
    }
    playback_log(p, "saved %s at %zu/%zu characters; it resumes here next time", name, end, f->length);
    j->ended_early = 1;
    return 1;
}
/* Delete text that went astray at the end of a file, the way a person fixes a typing mess: go to
   the end and press Backspace until the wrong part is gone. Deleting a little too much is harmless
   (the save check accepts any correct beginning); typing then resumes from there. */
static int trim_end(Playback *p, size_t count, const char *name, MotionRng *rng) {
    size_t i;
    playback_log(p, "%s ends with %zu character(s) that do not match the reference; deleting them", name, count);
    if (!shortcut(p, VK_CONTROL, VK_END) || !playback_wait(p, motion_between(rng, 300, 700))) return 0;
    for (i = 0; i < count; ++i)
        if (!keyboard_press(p, VK_BACK) || !playback_wait(p, motion_between(rng, 35, 120))) return 0;
    return playback_wait(p, motion_between(rng, 400, 900));
}
/* An open VS Code window on this project (its title has the folder name), if any. */
typedef struct { const wchar_t *name; HWND found; } EditorSearch;
static BOOL CALLBACK find_editor(HWND window, LPARAM parameter) {
    EditorSearch *search = (EditorSearch *)parameter;
    wchar_t title[512], needle[MAX_PATH + 8];
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || !GetWindowTextW(window, title, 512)) return TRUE;
    swprintf(needle, MAX_PATH + 8, L" - %ls - ", search->name);
    if ((wcsstr(title, needle) || !wcsncmp(title, search->name, wcslen(search->name))) && wcsstr(title, L"Visual Studio Code") &&
        window_process_is(window, L"Code.exe")) { search->found = window; return FALSE; }
    return TRUE;
}
static HWND open_editor(const wchar_t *destination) {
    EditorSearch search;
    search.name = wcsrchr(destination, L'\\') + 1; search.found = NULL;
    EnumWindows(find_editor, (LPARAM)&search);
    return search.found;
}
/* Back to the editor that is already open: its taskbar icon, like a person would, else activate it. */
static int return_to_editor(Playback *p, HWND editor, MotionRng *rng) {
    playback_log(p, "VS Code already has the project open; switching to it");
    if (GetForegroundWindow() != editor && !taskbar_show_window(p, rng, L"Visual Studio Code", editor)) return 0;
    if (IsIconic(editor)) { ShowWindow(editor, SW_RESTORE); if (!playback_wait(p, 600)) return 0; }
    p->target = editor;
    return playback_guard(p) && keyboard_press(p, VK_ESCAPE) && presentation_rest(p, "back in VS Code");
}
static int type_file(Playback *p, CodingProject *j, size_t index, HWND editor, MotionRng *rng) {
    const ProjectFile *f = &j->reference->files[index];
    const ProjectStep *step = &j->plan->steps[index];
    const wchar_t *name = wcsrchr(f->relative, L'\\') ? wcsrchr(f->relative, L'\\') + 1 : f->relative;
    wchar_t path[MAX_PATH];
    size_t offset, verified;
    char text[3 * MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\%ls", j->plan->destination, f->relative);
    utf8(f->relative, text, sizeof(text));
    playback_log(p, "%s %s (%zu characters)", step->kind == STEP_RESUME ? "resuming" : "typing", text, f->length - step->offset);
    if (step->kind == STEP_CREATE) {
        if (!coding_step_create_file(p, path)) return 0;
        /* Record ownership right away, so an interrupted file can be resumed next time. */
        if (!project_history_mark(j->plan->history, f->relative, f->sha256, HISTORY_PARTIAL) || !save_history(p, j)) return 0;
    } else if (!coding_step_open_file(p, path, editor)) return 0;
    if (!coding_step_select_tab(p, name) || !coding_step_plain_text(p) || !keyboard_press(p, VK_ESCAPE) ||
        !shortcut(p, VK_CONTROL, L'1')) return 0;
    if (step->kind == STEP_CREATE && (!shortcut(p, VK_CONTROL, L'A') || !keyboard_press(p, VK_BACK) ||
        !shortcut(p, VK_CONTROL, L'S') || !coding_step_wait_saved(p, path, L""))) return 0;
    if (!coding_step_lf(p) || !keyboard_press(p, VK_ESCAPE) || !shortcut(p, VK_CONTROL, L'1')) return 0;
    if (step->kind == STEP_RESUME) {
        /* An interrupted run can leave unsaved typing in the editor; start from what was saved. */
        if (!coding_step_revert(p) || !keyboard_press(p, VK_ESCAPE) || !shortcut(p, VK_CONTROL, L'1')) return 0;
        if (step->trim && !trim_end(p, step->trim, text, rng)) return 0;
    }
    if (!coding_step_saved_offset(p, path, f->text, NULL, &offset) || !shortcut(p, VK_CONTROL, VK_END)) return 0;
    while (offset < f->length) {
        size_t consumed = 0;
        if (playback_yield(p)) return finish_line(p, j, f, path, offset);
        if (!keyboard_type_slice(p, f->text + offset, f->text, j->config->checkpoint_seconds * 1000, &consumed)) {
            playback_log(p, "typing interrupted; run again to resume from the last saved checkpoint"); return 0;
        }
        offset += consumed;
        if (!coding_step_saved_offset(p, path, f->text, &offset, &verified) || verified != offset) {
            playback_log(p, "the saved file does not match what was typed; stopped"); return 0;
        }
        playback_log(p, "checkpoint saved: %zu/%zu characters of %s (%u typo(s) corrected so far)", offset, f->length, text, p->typos);
        if (offset < f->length && playback_yield(p)) return finish_line(p, j, f, path, offset);
        if (offset < f->length && !checkpoint_break(p, j->config, rng)) return 0;
        if (offset < f->length && j->apps) {
            int took;
            if (!side_apps_tick(p, j->apps, &took)) return 0;
            /* Back in VS Code: confirm the file is as it was left, then continue at its end. */
            if (took && (!coding_step_saved_offset(p, path, f->text, &offset, &verified) || verified != offset ||
                !shortcut(p, VK_CONTROL, VK_END))) return 0;
        }
    }
    if (j->ended_early) return 1;
    if (!coding_step_wait_saved(p, path, f->text)) return 0;
    if (!project_history_mark(j->plan->history, f->relative, f->sha256, HISTORY_TYPED) || !save_history(p, j)) return 0;
    playback_log(p, "finished %s: saved text matches the reference", text);
    /* Keep one editor tab open at a time. */
    return shortcut(p, VK_CONTROL, L'W') && presentation_rest(p, "finished file closed");
}

int coding_project_run(Playback *p, CodingProject *j) {
    const ProgrammingConfig *c = j->config;
    ProjectPlan *plan = j->plan;
    MotionRng rng;
    SideAppsTimer timer;
    HWND editor = NULL;
    size_t i;
    p->typing_speed = j->profile->typing_speed;
    if (plan->conflicts) { playback_log(p, "the project has conflicts; run --dry-run-code to see them"); return 0; }
    if (!plan->remaining_files) {
        playback_log(p, "every file is already complete");
    } else {
        motion_seed(&rng, j->seed * 2654435761u + 7);
        p->client_coords = 0;
        action_pace_configure(&p->action_pace, c->action_pause_ms[0], c->action_pause_ms[1], c->mouse_move_ms[0], c->mouse_move_ms[1], j->seed);
        typing_pace_configure(&p->typing_pace, c->char_ms[0], c->char_ms[1], c->newline_ms[0], c->newline_ms[1], j->seed + 1);
        typing_pace_breaks(&p->typing_pace, c->pause_every_chars[0], c->pause_every_chars[1], c->pause_ms[0], c->pause_ms[1]);
        p->typo_percent = profile_mistype_percent(j->profile);
        p->typo_state = j->seed ^ 0x9e3779b9u;
        motion_hand_for(&p->hand, motion_hash(j->profile->local_id), j->profile->randomness_score);
        /* Side-app breaks count main-task time from here. */
        if (j->side_apps && !j->apps) { side_apps_timer_init(&timer, j->side_apps, j->profile, j->seed, p); j->apps = &timer; }
        if (!p->target) p->target = GetForegroundWindow();
        playback_log(p, "project: %zu file(s) and %zu characters to go, seed %lu, typo %u%%",
            plan->remaining_files, plan->remaining_characters, (unsigned long)j->seed, p->typo_percent);
        j->ended_early = 0;
        if (!plan->fresh && (editor = open_editor(plan->destination))) {
            if (!return_to_editor(p, editor, &rng)) return 0;
        } else {
            if (!open_project_folder(p, j)) return 0;
            /* The folder now exists; remember it so the next run continues here instead of starting a new copy. */
            if (!save_history(p, j) || !open_in_vscode(p, j, &editor)) return 0;
        }
        for (i = 0; i < j->reference->file_count; ++i) {
            ProjectStepKind kind = plan->steps[i].kind;
            p->line = (unsigned)i + 1;
            if (!playback_poll(p)) return 0;
            /* Asked to stop between files: nothing is half typed, so stop here. */
            if ((kind == STEP_CREATE || kind == STEP_RESUME) && playback_yield(p)) { j->ended_early = 1; return 1; }
            if (kind == STEP_COPY ? !copy_file(p, j, i) :
                (kind == STEP_CREATE || kind == STEP_RESUME) ? !type_file(p, j, i, editor, &rng) : 0) return 0;
            if (j->ended_early) return 1;
            if (kind == STEP_CREATE || kind == STEP_RESUME || kind == STEP_COPY) plan->steps[i].kind = STEP_DONE;
            if (j->apps && (kind == STEP_CREATE || kind == STEP_RESUME)) {
                int took;
                if (!side_apps_tick(p, j->apps, &took)) return 0;
            }
        }
    }
    if (!plan->history->completed) {
        SYSTEMTIME now;
        GetLocalTime(&now);
        plan->history->completed = 1;
        swprintf(plan->history->finished, 32, L"%04u-%02u-%02u %02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute);
        if (!save_history(p, j)) return 0;
    }
    playback_log(p, "project complete: every file matches the reference; %u typo(s) were made and corrected", p->typos);
    return 1;
}
