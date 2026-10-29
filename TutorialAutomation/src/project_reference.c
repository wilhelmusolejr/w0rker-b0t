#include "project_reference.h"
#include "json_config.h"
#include "reference_config.h"
#include "script_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

static int fail(char *error, size_t cap, const char *message) { snprintf(error, cap, "%s", message); return 0; }

/* ---------- settings ---------- */

static int range(const JsonValue *parent, const wchar_t *key, unsigned lo, unsigned hi, unsigned out[2]) {
    const JsonValue *v = json_get(parent, key), *a;
    if (!v || v->kind != JSON_ARRAY || !(a = v->child) || !a->next || a->next->next ||
        a->kind != JSON_NUMBER || a->next->kind != JSON_NUMBER) return 0;
    out[0] = a->number; out[1] = a->next->number;
    return out[0] >= lo && out[1] <= hi && out[0] <= out[1];
}
static int number(const JsonValue *parent, const wchar_t *key, unsigned lo, unsigned hi, unsigned *out) {
    const JsonValue *v = json_get(parent, key);
    if (!v || v->kind != JSON_NUMBER || v->number < lo || v->number > hi) return 0;
    *out = v->number; return 1;
}
static int patterns(const JsonValue *v, wchar_t **out, size_t *count) {
    const JsonValue *item;
    if (!v) return 1;
    if (v->kind != JSON_ARRAY) return 0;
    for (item = v->child; item; item = item->next) {
        if (*count == PROJECT_MAX_PATTERNS || item->kind != JSON_STRING || !*item->string ||
            wcslen(item->string) > 100 || wcspbrk(item->string, L"\\/")) return 0;
        if (!(out[(*count)++] = _wcsdup(item->string))) return 0;
    }
    return 1;
}
static int resolve(const wchar_t *base, const wchar_t *text, wchar_t out[MAX_PATH]) {
    wchar_t joined[2 * MAX_PATH];
    DWORD n;
    int absolute = ((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z')) && text[1] == L':';
    if (!*text || wcslen(text) >= MAX_PATH) return 0;
    if (absolute) wcscpy(joined, text);
    else swprintf(joined, 2 * MAX_PATH, L"%ls\\%ls", base, text);
    n = GetFullPathNameW(joined, MAX_PATH, out, NULL);
    if (!n || n >= MAX_PATH) return 0;
    for (n = 0; out[n]; ++n) if (out[n] == L'/') out[n] = L'\\';
    n = (DWORD)wcslen(out);
    if (n > 3 && out[n-1] == L'\\') out[n-1] = 0;
    return 1;
}
int programming_config_load(const wchar_t *path, ProgrammingConfig *c, char *error, size_t cap) {
    static const wchar_t *const keys[] = {L"version", L"reference_projects", L"progress_folder", L"project_list",
        L"copy_instead_of_typing", L"ignore", L"max_typed_bytes", L"max_project_characters", L"typing", L"actions",
        L"checkpoint_seconds", L"idle_percent", L"idle_seconds", L"mouse_wander_percent"};
    static const wchar_t *const typing_keys[] = {L"character_ms", L"newline_ms", L"pause_every_chars", L"pause_ms"};
    static const wchar_t *const action_keys[] = {L"pause_ms", L"mouse_move_ms"};
    wchar_t *text = NULL, base[MAX_PATH], *slash;
    JsonValue *root = NULL;
    const JsonValue *v, *typing, *actions;
    DWORD attrs;
    memset(c, 0, sizeof(*c));
    if (!cap) return 0;
    *error = 0;
    if (!GetFullPathNameW(path, MAX_PATH, base, NULL) || !(slash = wcsrchr(base, L'\\'))) return fail(error, cap, "invalid settings path");
    *slash = 0;
    if (!read_utf8(path, &text, error, cap)) return 0;
    if (!json_parse(text, &root, error, cap)) { free(text); return 0; }
    free(text);
    v = json_get(root, L"version");
    if (!json_keys(root, keys, sizeof(keys)/sizeof(*keys)) || !v || v->kind != JSON_NUMBER || v->number != 1) {
        fail(error, cap, "programming settings need \"version\": 1 and only documented fields"); goto fail;
    }
    v = json_get(root, L"reference_projects");
    if (!v || v->kind != JSON_STRING || !resolve(base, v->string, c->references) ||
        (attrs = GetFileAttributesW(c->references)) == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        fail(error, cap, "reference_projects must name an existing folder (relative to programming.json)"); goto fail;
    }
    v = json_get(root, L"progress_folder");
    if (!v || v->kind != JSON_STRING || !resolve(base, v->string, c->progress_folder)) {
        fail(error, cap, "progress_folder must be a folder path (relative to programming.json)"); goto fail;
    }
    v = json_get(root, L"project_list");
    if (!v || v->kind != JSON_STRING || !resolve(base, v->string, c->project_list) ||
        GetFileAttributesW(c->project_list) == INVALID_FILE_ATTRIBUTES) {
        fail(error, cap, "project_list must name an existing projects file (relative to programming.json)"); goto fail;
    }
    if (!patterns(json_get(root, L"copy_instead_of_typing"), c->copy_patterns, &c->copy_count) ||
        !patterns(json_get(root, L"ignore"), c->ignore_patterns, &c->ignore_count)) {
        fail(error, cap, "copy_instead_of_typing and ignore are lists of file or folder name patterns (up to 32)"); goto fail;
    }
    typing = json_get(root, L"typing"); actions = json_get(root, L"actions");
    if (!number(root, L"max_typed_bytes", 1, 2000000, &c->max_typed_bytes) ||
        !number(root, L"max_project_characters", 1, 50000000, &c->max_project_characters) ||
        !json_keys(typing, typing_keys, 4) || !json_keys(actions, action_keys, 2) ||
        !range(typing, L"character_ms", 1, 5000, c->char_ms) || !range(typing, L"newline_ms", 1, 20000, c->newline_ms) ||
        !range(typing, L"pause_every_chars", 1, 100000, c->pause_every_chars) || !range(typing, L"pause_ms", 0, 600000, c->pause_ms) ||
        !range(actions, L"pause_ms", 0, 60000, c->action_pause_ms) || !range(actions, L"mouse_move_ms", 50, 10000, c->mouse_move_ms) ||
        !number(root, L"checkpoint_seconds", 10, 600, &c->checkpoint_seconds) ||
        !number(root, L"idle_percent", 0, 100, &c->idle_percent) || !range(root, L"idle_seconds", 1, 3600, c->idle_seconds) ||
        !number(root, L"mouse_wander_percent", 0, 100, &c->wander_percent)) {
        fail(error, cap, "programming settings: check max_typed_bytes, max_project_characters, typing and actions [min, max] ranges, "
            "checkpoint_seconds 10-600, percents 0-100, and idle_seconds"); goto fail;
    }
    json_free(root); return 1;
fail:
    json_free(root); programming_config_free(c); return 0;
}
void programming_config_free(ProgrammingConfig *c) {
    size_t i;
    for (i = 0; i < c->copy_count; ++i) free(c->copy_patterns[i]);
    for (i = 0; i < c->ignore_count; ++i) free(c->ignore_patterns[i]);
    c->copy_count = c->ignore_count = 0;
}
int project_pattern_match(const wchar_t *p, const wchar_t *s) {
    if (!*p) return !*s;
    if (*p == L'*') return project_pattern_match(p + 1, s) || (*s && project_pattern_match(p, s + 1));
    return *s && (*p == L'?' || towlower(*p) == towlower(*s)) && project_pattern_match(p + 1, s + 1);
}
static int matches_any(wchar_t *const *list, size_t count, const wchar_t *name) {
    size_t i;
    for (i = 0; i < count; ++i) if (project_pattern_match(list[i], name)) return 1;
    return 0;
}

/* ---------- reference projects ---------- */

typedef struct { wchar_t name[MAX_PATH]; int folder; } Entry;
static int compare_entries(const void *a, const void *b) {
    const Entry *x = a, *y = b;
    if (x->folder != y->folder) return x->folder - y->folder; /* files before subfolders */
    return _wcsicmp(x->name, y->name);
}
static int add_file(const ProgrammingConfig *c, ProjectReference *r, const wchar_t *relative, char *error, size_t cap) {
    ProjectFile *grown, *f;
    wchar_t full[MAX_PATH], *text = NULL;
    WIN32_FILE_ATTRIBUTE_DATA data;
    const wchar_t *name = wcsrchr(relative, L'\\') ? wcsrchr(relative, L'\\') + 1 : relative;
    char ignored[128];
    if (swprintf(full, MAX_PATH, L"%ls\\%ls", r->root, relative) < 0) return fail(error, cap, "reference path too long");
    if (!(grown = realloc(r->files, (r->file_count + 1) * sizeof(*grown)))) return fail(error, cap, "out of memory");
    r->files = grown; f = &r->files[r->file_count];
    memset(f, 0, sizeof(*f));
    wcscpy(f->relative, relative);
    if (GetFileAttributesExW(full, GetFileExInfoStandard, &data))
        f->bytes = ((unsigned long long)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    f->kind = PROJECT_COPY;
    /* Lock files, images, and anything too large or not UTF-8 text are copied, never typed. */
    if (!matches_any(c->copy_patterns, c->copy_count, name) && f->bytes <= c->max_typed_bytes &&
        read_utf8(full, &text, ignored, sizeof(ignored))) {
        f->text = reference_normalize(text);
        free(text);
        if (!f->text) return fail(error, cap, "out of memory");
        f->length = wcslen(f->text);
        if (!reference_sha256(f->text, f->sha256)) return fail(error, cap, "cannot hash a reference file");
        f->kind = PROJECT_TYPE;
    }
    ++r->file_count;
    return 1;
}
static int scan(const ProgrammingConfig *c, ProjectReference *r, const wchar_t *relative, unsigned depth,
                char *error, size_t cap) {
    wchar_t query[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    Entry *entries = NULL;
    size_t count = 0, i;
    int ok = 1;
    if (depth > 12) return fail(error, cap, "reference project folders are nested too deeply");
    swprintf(query, MAX_PATH, *relative ? L"%ls\\%ls\\*" : L"%ls\\*", r->root, relative);
    find = FindFirstFileW(query, &data);
    if (find == INVALID_HANDLE_VALUE) return fail(error, cap, "cannot list a reference folder");
    do {
        Entry *grown;
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..") ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            matches_any(c->ignore_patterns, c->ignore_count, data.cFileName)) continue;
        if (!(grown = realloc(entries, (count + 1) * sizeof(*grown)))) { ok = fail(error, cap, "out of memory"); break; }
        entries = grown;
        if (swprintf(entries[count].name, MAX_PATH, *relative ? L"%ls\\%ls" : L"%ls%ls", relative, data.cFileName) < 0 ||
            wcslen(r->root) + wcslen(entries[count].name) > MAX_PATH - 40) { ok = fail(error, cap, "a reference path is too long"); break; }
        entries[count++].folder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    } while (FindNextFileW(find, &data));
    FindClose(find);
    if (ok) qsort(entries, count, sizeof(*entries), compare_entries);
    for (i = 0; ok && i < count; ++i) {
        if (!entries[i].folder) { ok = add_file(c, r, entries[i].name, error, cap); continue; }
        { wchar_t (*grown)[MAX_PATH] = realloc(r->folders, (r->folder_count + 1) * sizeof(*grown));
          if (!grown) { ok = fail(error, cap, "out of memory"); break; }
          r->folders = grown; wcscpy(r->folders[r->folder_count++], entries[i].name); }
        ok = scan(c, r, entries[i].name, depth + 1, error, cap);
    }
    free(entries);
    return ok;
}
static int name_valid(const wchar_t *name) {
    return *name && wcslen(name) < 100 && !wcspbrk(name, L"\\/:*?\"<>|") && wcscmp(name, L".") && wcscmp(name, L"..");
}
int project_reference_load(const ProgrammingConfig *c, const wchar_t *name, ProjectReference *r, char *error, size_t cap) {
    memset(r, 0, sizeof(*r));
    if (!name_valid(name)) return fail(error, cap, "invalid reference project name");
    wcscpy(r->name, name);
    if (swprintf(r->root, MAX_PATH, L"%ls\\%ls", c->references, name) < 0) return fail(error, cap, "reference path too long");
    if (!scan(c, r, L"", 0, error, cap)) { project_reference_free(r); return 0; }
    if (!r->file_count) { project_reference_free(r); return fail(error, cap, "the reference project has no files"); }
    return 1;
}
void project_reference_free(ProjectReference *r) {
    size_t i;
    for (i = 0; i < r->file_count; ++i) free(r->files[i].text);
    free(r->files); free(r->folders);
    memset(r, 0, sizeof(*r));
}
size_t project_reference_characters(const ProjectReference *r) {
    size_t i, total = 0;
    for (i = 0; i < r->file_count; ++i) total += r->files[i].length;
    return total;
}

/* ---------- history ---------- */

static const char *const status_names[] = {"partial", "typed", "copied"};
static int copy_string(const JsonValue *v, wchar_t *out, size_t cap) {
    if (!v || v->kind != JSON_STRING || wcslen(v->string) >= cap) return 0;
    wcscpy(out, v->string); return 1;
}
int project_history_load(const ProgrammingConfig *c, const Profile *profile, ProjectHistory *h, char *error, size_t cap) {
    wchar_t *text = NULL;
    JsonValue *root = NULL;
    const JsonValue *projects, *v, *f;
    memset(h, 0, sizeof(*h));
    wcsncpy(h->profile, profile->local_id, 63);
    if (swprintf(h->path, MAX_PATH, L"%ls\\%ls.json", c->progress_folder, profile->local_id) < 0)
        return fail(error, cap, "history path too long");
    if (GetFileAttributesW(h->path) == INVALID_FILE_ATTRIBUTES) return 1;   /* nothing created yet */
    if (!read_utf8(h->path, &text, error, cap) || !json_parse(text, &root, error, cap)) { free(text); return 0; }
    free(text);
    projects = json_get(root, L"projects");
    if (!projects || projects->kind != JSON_ARRAY) { json_free(root); return fail(error, cap, "project history has no projects list"); }
    for (v = projects->child; v; v = v->next) {
        const JsonValue *status = json_get(v, L"status"), *files = json_get(v, L"files");
        HistoryProject *p;
        wchar_t reference[128], destination[MAX_PATH];
        if (!copy_string(json_get(v, L"reference"), reference, 128) || !copy_string(json_get(v, L"destination"), destination, MAX_PATH) ||
            !status || status->kind != JSON_STRING || !files || files->kind != JSON_ARRAY ||
            !(p = project_history_add(h, reference, destination))) {
            json_free(root); project_history_free(h); return fail(error, cap, "invalid project in history");
        }
        p->completed = !wcscmp(status->string, L"completed");
        p->skipped = !wcscmp(status->string, L"skipped");
        { const JsonValue *commit = json_get(v, L"commit");
          if (commit && commit->kind == JSON_STRING && wcslen(commit->string) == 40)
              WideCharToMultiByte(CP_UTF8, 0, commit->string, -1, p->commit, 41, NULL, NULL); }
        copy_string(json_get(v, L"started"), p->started, 32);
        copy_string(json_get(v, L"finished"), p->finished, 32);
        for (f = files->child; f; f = f->next) {
            const JsonValue *path = json_get(f, L"path"), *sha = json_get(f, L"sha256"), *st = json_get(f, L"status");
            char hash[65] = "";
            int k, kind = -1;
            if (sha && sha->kind == JSON_STRING && wcslen(sha->string) == 64) WideCharToMultiByte(CP_UTF8, 0, sha->string, -1, hash, 65, NULL, NULL);
            for (k = 0; st && st->kind == JSON_STRING && k < 3; ++k) {
                wchar_t name[16];
                MultiByteToWideChar(CP_UTF8, 0, status_names[k], -1, name, 16);
                if (!wcscmp(st->string, name)) kind = k;
            }
            if (!path || path->kind != JSON_STRING || kind < 0 || !project_history_mark(p, path->string, hash, (HistoryFileStatus)kind)) {
                json_free(root); project_history_free(h); return fail(error, cap, "invalid file in project history");
            }
        }
    }
    json_free(root);
    return 1;
}
static void json_text(FILE *out, const wchar_t *text) {
    char utf8[3 * MAX_PATH + 8];
    const char *s;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
    fputc('"', out);
    for (s = utf8; *s; ++s) {
        if (*s == '"' || *s == '\\') { fputc('\\', out); fputc(*s, out); }
        else if ((unsigned char)*s < 32) fprintf(out, "\\u%04x", (unsigned char)*s);
        else fputc(*s, out);
    }
    fputc('"', out);
}
int project_history_save(const ProjectHistory *h, char *error, size_t cap) {
    wchar_t temporary[MAX_PATH + 8], folder[MAX_PATH], *slash;
    FILE *out;
    size_t i, j;
    int failed;
    wcscpy(folder, h->path);
    if ((slash = wcsrchr(folder, L'\\'))) { *slash = 0; CreateDirectoryW(folder, NULL); }
    swprintf(temporary, MAX_PATH + 8, L"%ls.tmp", h->path);
    if (!(out = _wfopen(temporary, L"wb"))) return fail(error, cap, "cannot write the project history file");
    fputs("{\n    \"version\": 1,\n    \"profile\": ", out); json_text(out, h->profile);
    fputs(",\n    \"projects\": [", out);
    for (i = 0; i < h->project_count; ++i) {
        const HistoryProject *p = &h->projects[i];
        fputs(i ? ",\n        {\n" : "\n        {\n", out);
        fputs("            \"reference\": ", out); json_text(out, p->reference);
        fputs(",\n            \"destination\": ", out); json_text(out, p->destination);
        fprintf(out, ",\n            \"commit\": \"%s\",\n            \"status\": \"%s\",\n            \"started\": ", p->commit,
            p->completed ? "completed" : p->skipped ? "skipped" : "in_progress");
        json_text(out, p->started);
        fputs(",\n            \"finished\": ", out); json_text(out, p->finished);
        fputs(",\n            \"files\": [", out);
        for (j = 0; j < p->file_count; ++j) {
            fputs(j ? ",\n                { \"path\": " : "\n                { \"path\": ", out);
            json_text(out, p->files[j].relative);
            fprintf(out, ", \"sha256\": \"%s\", \"status\": \"%s\" }", p->files[j].sha256, status_names[p->files[j].status]);
        }
        fputs(p->file_count ? "\n            ]\n        }" : "]\n        }", out);
    }
    fputs(h->project_count ? "\n    ]\n}\n" : "]\n}\n", out);
    failed = ferror(out) != 0;
    if (fclose(out) || failed) { DeleteFileW(temporary); return fail(error, cap, "project history write failed"); }
    if (!MoveFileExW(temporary, h->path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary); return fail(error, cap, "cannot commit the project history file");
    }
    return 1;
}
void project_history_free(ProjectHistory *h) {
    size_t i;
    for (i = 0; i < h->project_count; ++i) free(h->projects[i].files);
    free(h->projects);
    h->projects = NULL; h->project_count = 0;
}
HistoryProject *project_history_add(ProjectHistory *h, const wchar_t *reference, const wchar_t *destination) {
    HistoryProject *grown = realloc(h->projects, (h->project_count + 1) * sizeof(*grown)), *p;
    SYSTEMTIME now;
    if (!grown) return NULL;
    h->projects = grown; p = &h->projects[h->project_count++];
    memset(p, 0, sizeof(*p));
    wcsncpy(p->reference, reference, 127); wcsncpy(p->destination, destination, MAX_PATH - 1);
    GetLocalTime(&now);
    swprintf(p->started, 32, L"%04u-%02u-%02u %02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute);
    return p;
}
HistoryFile *project_history_file(HistoryProject *p, const wchar_t *relative) {
    size_t i;
    for (i = 0; p && i < p->file_count; ++i) if (!_wcsicmp(p->files[i].relative, relative)) return &p->files[i];
    return NULL;
}
int project_history_mark(HistoryProject *p, const wchar_t *relative, const char *sha256, HistoryFileStatus status) {
    HistoryFile *f = project_history_file(p, relative);
    if (wcslen(relative) >= MAX_PATH || strlen(sha256) > 64) return 0;
    if (!f) {
        if (p->file_count == p->file_capacity) {
            size_t capacity = p->file_capacity ? p->file_capacity * 2 : 32;
            HistoryFile *grown = realloc(p->files, capacity * sizeof(*grown));
            if (!grown) return 0;
            p->files = grown; p->file_capacity = capacity;
        }
        f = &p->files[p->file_count++];
        wcscpy(f->relative, relative);
    }
    strcpy(f->sha256, sha256); f->status = status;
    return 1;
}

/* ---------- choosing and planning ---------- */

int project_destination(const Profile *profile, const wchar_t *name, wchar_t destination[MAX_PATH], char *error, size_t cap) {
    wchar_t root[MAX_PATH];
    unsigned suffix;
    DWORD attrs;
    if (!profile_project_root(profile, root, MAX_PATH)) return fail(error, cap, "the profile has no usable project_path");
    attrs = GetFileAttributesW(root);
    if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return fail(error, cap, "the profile's project_path folder does not exist");
    /* Never reuse a folder this profile did not create: kruzpayr, then kruzpayr-2, ... */
    for (suffix = 1; suffix < 10; ++suffix) {
        if (suffix == 1) swprintf(destination, MAX_PATH, L"%ls\\%ls", root, name);
        else swprintf(destination, MAX_PATH, L"%ls\\%ls-%u", root, name, suffix);
        if (GetFileAttributesW(destination) == INVALID_FILE_ATTRIBUTES) return 1;
    }
    return fail(error, cap, "every candidate project folder name is already taken");
}
static int same_bytes(const wchar_t *a, const wchar_t *b) {
    HANDLE x = CreateFileW(a, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE y = CreateFileW(b, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    char bx[65536], by[65536];
    DWORD nx, ny;
    int same = x != INVALID_HANDLE_VALUE && y != INVALID_HANDLE_VALUE;
    while (same && ReadFile(x, bx, sizeof(bx), &nx, NULL) && ReadFile(y, by, sizeof(by), &ny, NULL)) {
        if (nx != ny || memcmp(bx, by, nx)) same = 0;
        if (!nx) break;
    }
    if (x != INVALID_HANDLE_VALUE) CloseHandle(x);
    if (y != INVALID_HANDLE_VALUE) CloseHandle(y);
    return same;
}
int project_plan_build(const ProjectReference *r, HistoryProject *history, const wchar_t *destination,
                       ProjectPlan *plan, char *error, size_t cap) {
    size_t i;
    memset(plan, 0, sizeof(*plan));
    plan->reference = r; plan->history = history;
    wcsncpy(plan->destination, destination, MAX_PATH - 1);
    plan->fresh = GetFileAttributesW(destination) == INVALID_FILE_ATTRIBUTES;
    if (!(plan->steps = calloc(r->file_count, sizeof(*plan->steps)))) return fail(error, cap, "out of memory");
    for (i = 0; i < r->folder_count; ++i) {
        wchar_t path[MAX_PATH];
        swprintf(path, MAX_PATH, L"%ls\\%ls", destination, r->folders[i]);
        plan->missing_folders += GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES;
    }
    for (i = 0; i < r->file_count; ++i) {
        const ProjectFile *f = &r->files[i];
        ProjectStep *s = &plan->steps[i];
        HistoryFile *owned = project_history_file(history, f->relative);
        wchar_t path[MAX_PATH], source[MAX_PATH], *saved = NULL, *normalized;
        char ignored[128];
        swprintf(path, MAX_PATH, L"%ls\\%ls", destination, f->relative);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            s->kind = f->kind == PROJECT_COPY ? STEP_COPY : STEP_CREATE;
        } else if (f->kind == PROJECT_COPY) {
            swprintf(source, MAX_PATH, L"%ls\\%ls", r->root, f->relative);
            if (same_bytes(source, path)) s->kind = STEP_DONE;
            else { s->kind = STEP_CONFLICT; s->reason = "a different file already exists there"; }
        } else if (!read_utf8(path, &saved, ignored, sizeof(ignored)) || !(normalized = reference_normalize(saved))) {
            s->kind = STEP_CONFLICT; s->reason = "the existing file is not readable UTF-8 text";
        } else {
            int mine = owned && owned->status == HISTORY_PARTIAL && !strcmp(owned->sha256, f->sha256);
            if (!wcscmp(normalized, f->text)) s->kind = STEP_DONE;
            else if (reference_prefix(f->text, normalized, &s->offset)) {
                if (mine) s->kind = STEP_RESUME;
                else { s->kind = STEP_CONFLICT; s->reason = "a partial file exists that this profile did not start"; }
            } else if (mine) {
                /* This profile's own half-typed file, with text that went astray at the end: keep the
                   part that matches, delete the rest, and resume there. */
                size_t same = 0, length = wcslen(normalized);
                while (same < length && f->text[same] && f->text[same] == normalized[same]) ++same;
                if (same && normalized[same - 1] >= 0xd800 && normalized[same - 1] <= 0xdbff) --same;
                s->kind = STEP_RESUME; s->offset = same; s->trim = length - same;
            } else { s->kind = STEP_CONFLICT; s->reason = "the existing file has different content"; }
            free(normalized);
        }
        free(saved);
        if (s->kind == STEP_CONFLICT) ++plan->conflicts;
        if (s->kind != STEP_DONE && s->kind != STEP_CONFLICT) ++plan->remaining_files;
        if (s->kind == STEP_CREATE || s->kind == STEP_RESUME) plan->remaining_characters += f->length - s->offset;
    }
    return 1;
}
void project_plan_free(ProjectPlan *plan) { free(plan->steps); plan->steps = NULL; }
const char *project_step_name(ProjectStepKind kind) {
    static const char *const names[] = {"TYPE", "RESUME", "DONE", "COPY", "CONFLICT"};
    return (unsigned)kind < 5 ? names[kind] : "?";
}
