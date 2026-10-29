#include "project_manifest.h"
#include "human_motion.h"
#include "json_config.h"
#include "script_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *error, size_t cap, const char *message) { snprintf(error, cap, "%s", message); return 0; }
static int name_valid(const wchar_t *s) {
    if (!*s || wcslen(s) >= 100 || s[0] == L'.') return 0;
    for (; *s; ++s)
        if (!((*s >= L'A' && *s <= L'Z') || (*s >= L'a' && *s <= L'z') || (*s >= L'0' && *s <= L'9') ||
              *s == L'-' || *s == L'_' || *s == L'.')) return 0;
    return 1;
}
static int commit_valid(const wchar_t *s) {
    size_t n = 0;
    for (; *s; ++s, ++n) if (!((*s >= L'0' && *s <= L'9') || (*s >= L'a' && *s <= L'f'))) return 0;
    return n == 40;
}
static int url_valid(const wchar_t *s) {
    return !wcsncmp(s, L"https://", 8) && wcslen(s) < 300 && !wcspbrk(s, L" \"'`<>|^&%\r\n\t\\");
}
int project_manifest_parse(const wchar_t *text, ProjectManifest *m, char *error, size_t cap) {
    static const wchar_t *const keys[] = {L"version", L"projects"};
    static const wchar_t *const item_keys[] = {L"name", L"url", L"commit"};
    JsonValue *root = NULL;
    const JsonValue *version, *list, *v;
    size_t count = 0, i, j;
    memset(m, 0, sizeof(*m));
    if (!cap) return 0;
    *error = 0;
    if (!json_parse(text, &root, error, cap)) return 0;
    version = json_get(root, L"version"); list = json_get(root, L"projects");
    if (!json_keys(root, keys, 2) || !version || version->kind != JSON_NUMBER || version->number != 1 ||
        !list || list->kind != JSON_ARRAY || !list->child) {
        json_free(root); return fail(error, cap, "projects file needs \"version\": 1 and a non-empty \"projects\" list");
    }
    for (v = list->child; v; v = v->next) ++count;
    if (count > 1000 || !(m->items = calloc(count, sizeof(*m->items)))) { json_free(root); return fail(error, cap, "too many projects"); }
    for (v = list->child, i = 0; v; v = v->next, ++i) {
        const JsonValue *name = json_get(v, L"name"), *url = json_get(v, L"url"), *commit = json_get(v, L"commit");
        if (!json_keys(v, item_keys, 3) || !name || name->kind != JSON_STRING || !name_valid(name->string) ||
            !url || url->kind != JSON_STRING || !url_valid(url->string) ||
            !commit || commit->kind != JSON_STRING || !commit_valid(commit->string)) {
            snprintf(error, cap, "project %zu: needs a name (letters, digits, - _ .), an https:// git url, "
                "and the full 40-character lowercase commit id", i + 1);
            json_free(root); project_manifest_free(m); return 0;
        }
        wcscpy(m->items[i].name, name->string); wcscpy(m->items[i].url, url->string);
        WideCharToMultiByte(CP_UTF8, 0, commit->string, -1, m->items[i].commit, 41, NULL, NULL);
        for (j = 0; j < i; ++j) if (!_wcsicmp(m->items[j].name, m->items[i].name)) {
            snprintf(error, cap, "project %zu: the name is already used by project %zu", i + 1, j + 1);
            json_free(root); project_manifest_free(m); return 0;
        }
        m->count = i + 1;
    }
    json_free(root);
    return 1;
}
int project_manifest_load(const wchar_t *path, ProjectManifest *m, char *error, size_t cap) {
    wchar_t *text = NULL;
    int ok;
    memset(m, 0, sizeof(*m));
    if (!read_utf8(path, &text, error, cap)) return 0;
    ok = project_manifest_parse(text, m, error, cap);
    free(text); return ok;
}
void project_manifest_free(ProjectManifest *m) { free(m->items); m->items = NULL; m->count = 0; }
const ManifestProject *project_manifest_find(const ProjectManifest *m, const wchar_t *name) {
    size_t i;
    for (i = 0; i < m->count; ++i) if (!_wcsicmp(m->items[i].name, name)) return &m->items[i];
    return NULL;
}
void project_manifest_order(const ProjectManifest *m, const Profile *profile, size_t *order) {
    MotionRng rng;
    uint32_t seed = 2166136261u;
    const wchar_t *s;
    size_t i;
    for (s = profile->local_id; *s; ++s) { seed ^= (uint32_t)*s; seed *= 16777619u; }
    motion_seed(&rng, seed);
    for (i = 0; i < m->count; ++i) order[i] = i;
    for (i = m->count; i > 1; --i) {
        size_t j = motion_between(&rng, 0, (unsigned)(i - 1)), t = order[i - 1];
        order[i - 1] = order[j]; order[j] = t;
    }
}

/* ---------- git ---------- */

static int run_git(const wchar_t *directory, const wchar_t *arguments, char *error, size_t cap) {
    wchar_t git[MAX_PATH], command[2048];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    DWORD code = 1;
    if (!SearchPathW(NULL, L"git.exe", NULL, MAX_PATH, git, NULL))
        return fail(error, cap, "Git is not installed or not on PATH; install Git for Windows on this machine");
    /* Never wait for a credential prompt; long paths for deep repositories; quiet detached checkout. */
    SetEnvironmentVariableW(L"GIT_TERMINAL_PROMPT", L"0");
    swprintf(command, 2048, L"\"%ls\" -C \"%ls\" -c core.longpaths=true -c advice.detachedHead=false %ls", git, directory, arguments);
    memset(&startup, 0, sizeof(startup)); startup.cb = sizeof(startup);
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process))
        return fail(error, cap, "cannot start git");
    if (WaitForSingleObject(process.hProcess, 15 * 60 * 1000) != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess); CloseHandle(process.hThread);
    if (code) { snprintf(error, cap, "git failed (exit code %lu); check the url, commit, and network", code); return 0; }
    return 1;
}
/* A detached checkout records the commit id itself in .git/HEAD. */
static int head_is(const wchar_t *directory, const char *commit) {
    wchar_t path[MAX_PATH];
    char head[64] = "";
    FILE *f;
    swprintf(path, MAX_PATH, L"%ls\\.git\\HEAD", directory);
    if (!(f = _wfopen(path, L"rb"))) return 0;
    fgets(head, sizeof(head), f); fclose(f);
    return !strncmp(head, commit, 40);
}
static void remove_tree(const wchar_t *root) {
    wchar_t query[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW d;
    HANDLE h;
    swprintf(query, MAX_PATH, L"%ls\\*", root);
    h = FindFirstFileW(query, &d);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(d.cFileName, L".") || !wcscmp(d.cFileName, L"..")) continue;
            swprintf(path, MAX_PATH, L"%ls\\%ls", root, d.cFileName);
            if ((d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(d.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) remove_tree(path);
            else { SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL); DeleteFileW(path); }
        } while (FindNextFileW(h, &d));
        FindClose(h);
    }
    RemoveDirectoryW(root);
}
int project_fetch(const ProgrammingConfig *c, const ManifestProject *project, const char *commit, char *error, size_t cap) {
    wchar_t directory[MAX_PATH], partial[MAX_PATH], git_folder[MAX_PATH], arguments[600];
    const char *pin = commit && *commit ? commit : project->commit;
    char shown[200];
    swprintf(directory, MAX_PATH, L"%ls\\%ls", c->references, project->name);
    swprintf(git_folder, MAX_PATH, L"%ls\\.git", directory);
    WideCharToMultiByte(CP_UTF8, 0, project->name, -1, shown, sizeof(shown), NULL, NULL);
    if (GetFileAttributesW(directory) != INVALID_FILE_ATTRIBUTES) {
        if (GetFileAttributesW(git_folder) == INVALID_FILE_ATTRIBUTES) {
            snprintf(error, cap, "programming_files\\%s exists but is not a git checkout; move it away so it can be cloned", shown);
            return 0;
        }
        if (head_is(directory, pin)) return 1;
        printf("Updating %s to commit %.7s...\n", shown, pin); fflush(stdout);
        swprintf(arguments, 600, L"fetch -q --depth 1 origin %hs", pin);
        if (!run_git(directory, arguments, error, cap)) return 0;
        swprintf(arguments, 600, L"checkout -q --force --detach %hs", pin);
        if (!run_git(directory, arguments, error, cap)) return 0;
        return head_is(directory, pin) ? 1 : fail(error, cap, "the checkout is not at the pinned commit");
    }
    /* Clone beside the final folder, and move it into place only when complete. */
    printf("Cloning %s at commit %.7s...\n", shown, pin); fflush(stdout);
    swprintf(partial, MAX_PATH, L"%ls.partial", directory);
    remove_tree(partial);
    if (!CreateDirectoryW(partial, NULL)) return fail(error, cap, "cannot create the clone folder");
    swprintf(arguments, 600, L"remote add origin \"%ls\"", project->url);
    if (!run_git(partial, L"init -q", error, cap) || !run_git(partial, arguments, error, cap)) { remove_tree(partial); return 0; }
    swprintf(arguments, 600, L"fetch -q --depth 1 origin %hs", pin);
    if (!run_git(partial, arguments, error, cap)) { remove_tree(partial); return 0; }
    swprintf(arguments, 600, L"checkout -q --detach %hs", pin);
    if (!run_git(partial, arguments, error, cap) || !head_is(partial, pin)) {
        remove_tree(partial); if (!*error) fail(error, cap, "checkout failed"); return 0;
    }
    if (!MoveFileW(partial, directory)) { remove_tree(partial); return fail(error, cap, "cannot move the finished clone into place"); }
    return 1;
}

/* ---------- choosing ---------- */

static HistoryProject *in_history(ProjectHistory *h, const wchar_t *name) {
    size_t i;
    for (i = 0; i < h->project_count; ++i) if (!_wcsicmp(h->projects[i].reference, name)) return &h->projects[i];
    return NULL;
}
int project_select(const ProgrammingConfig *c, const ProjectManifest *m, const Profile *profile, ProjectHistory *h,
                   int record, ProjectReference *reference, HistoryProject **started, wchar_t destination[MAX_PATH],
                   char *error, size_t cap) {
    size_t *order, i;
    *started = NULL;
    memset(reference, 0, sizeof(*reference));
    /* 1. Continue the project that was started but not finished, at the commit it started from. */
    for (i = 0; i < h->project_count; ++i) {
        HistoryProject *p = &h->projects[i];
        const ManifestProject *entry;
        if (p->completed || p->skipped) continue;
        if (!(entry = project_manifest_find(m, p->reference))) continue;
        if (!project_fetch(c, entry, p->commit, error, cap) ||
            !project_reference_load(c, p->reference, reference, error, cap)) return 0;
        if (!p->commit[0]) strcpy(p->commit, entry->commit);
        wcscpy(destination, p->destination);
        *started = p;
        return 1;
    }
    /* 2. The next project in this profile's own order. */
    if (!(order = calloc(m->count ? m->count : 1, sizeof(*order)))) return fail(error, cap, "out of memory");
    project_manifest_order(m, profile, order);
    for (i = 0; i < m->count; ++i) {
        const ManifestProject *entry = &m->items[order[i]];
        HistoryProject *seen = in_history(h, entry->name);
        size_t characters;
        char shown[200];
        if (seen && (seen->completed || seen->skipped)) continue;
        if (!project_fetch(c, entry, NULL, error, cap) || !project_reference_load(c, entry->name, reference, error, cap)) {
            free(order); return 0;
        }
        characters = project_reference_characters(reference);
        if (characters > c->max_project_characters) {
            WideCharToMultiByte(CP_UTF8, 0, entry->name, -1, shown, sizeof(shown), NULL, NULL);
            printf("Skipping %s: %zu characters to type is over max_project_characters (%u).\n", shown, characters, c->max_project_characters);
            project_reference_free(reference);
            if (record) {
                HistoryProject *skip = project_history_add(h, entry->name, L"");
                if (!skip) { free(order); return fail(error, cap, "out of memory"); }
                skip->skipped = 1; strcpy(skip->commit, entry->commit);
                if (!project_history_save(h, error, cap)) { free(order); return 0; }
            }
            continue;
        }
        if (!project_destination(profile, entry->name, destination, error, cap)) { project_reference_free(reference); free(order); return 0; }
        if (record) {
            if (!(*started = project_history_add(h, entry->name, destination))) { project_reference_free(reference); free(order); return fail(error, cap, "out of memory"); }
            strcpy((*started)->commit, entry->commit);
        }
        free(order);
        return 1;
    }
    free(order);
    return fail(error, cap, "every project in the list is completed or skipped for this profile");
}
