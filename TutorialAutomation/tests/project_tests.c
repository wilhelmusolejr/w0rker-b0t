#include "project_reference.h"
#include "project_manifest.h"
#include "reference_config.h"
#include "typing_pace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
static int checks, failures;
#define CHECK(t) do { ++checks; if (!(t)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#t); } } while (0)

static int write_text(const wchar_t *path, const char *text) {
    FILE *f = _wfopen(path, L"wb");
    int ok;
    if (!f) return 0;
    ok = fwrite(text, 1, strlen(text), f) == strlen(text);
    return fclose(f) == 0 && ok;
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
            if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(path); else DeleteFileW(path);
        } while (FindNextFileW(h, &d));
        FindClose(h);
    }
    RemoveDirectoryW(root);
}
static void pattern_and_typo_tests(void) {
    unsigned i;
    CHECK(project_pattern_match(L"*.png", L"home_ads.PNG"));
    CHECK(project_pattern_match(L"package-lock.json", L"package-lock.json"));
    CHECK(!project_pattern_match(L"*.png", L"main.py"));
    CHECK(project_pattern_match(L"*.py?", L"x.pyc"));
    for (i = 0; i < 200; ++i) {
        wchar_t a = typing_typo_neighbor(L'a', i), up = typing_typo_neighbor(L'K', i);
        CHECK(a && a != L'a' && a >= L'a' && a <= L'z');
        CHECK(up >= L'A' && up <= L'Z' && up != L'K');
    }
    CHECK(!typing_typo_neighbor(L'(', 3) && !typing_typo_neighbor(L' ', 3) && !typing_typo_neighbor(L'7', 3));
}
/* The shipped kruzpayr reference: 42 files, images and the npm lock file are copied, the rest typed. */
static void shipped_reference_tests(const ProgrammingConfig *c) {
    ProjectReference r;
    char error[256];
    size_t i, copies = 0, typed = 0, main_index = (size_t)-1, readme_dirs = 0;
    CHECK(project_reference_load(c, L"kruzpayr", &r, error, sizeof(error)));
    if (!r.file_count) { fprintf(stderr, "  %s\n", error); return; }
    CHECK(r.file_count == 42);
    for (i = 0; i < r.file_count; ++i) {
        if (r.files[i].kind == PROJECT_COPY) ++copies; else ++typed;
        if (!wcscmp(r.files[i].relative, L"automation\\app\\main.py")) main_index = i;
        CHECK(!r.files[i].text || !wcschr(r.files[i].text, L'\r'));
    }
    /* automation\requirements.txt is UTF-16 (pip freeze in PowerShell), so it is copied like the generated file it is. */
    CHECK(copies == 16 && typed == 26);
    CHECK(main_index != (size_t)-1 && r.files[main_index].kind == PROJECT_TYPE && r.files[main_index].length > 20000);
    /* Files come before subfolders, and parents before children. */
    CHECK(!wcscmp(r.files[0].relative, L".gitignore"));
    for (i = 0; i < r.folder_count; ++i) readme_dirs += !wcscmp(r.folders[i], L"automation\\app");
    CHECK(readme_dirs == 1 && r.folder_count == 12);
    project_reference_free(&r);
}
/* A tiny reference in a temporary folder exercises create, done, resume, copy, and conflict. */
static void plan_tests(void) {
    wchar_t temp[MAX_PATH], base[MAX_PATH], refs[MAX_PATH], project[MAX_PATH], dest[MAX_PATH], path[MAX_PATH];
    ProgrammingConfig c;
    ProjectReference r;
    ProjectHistory h;
    HistoryProject *hp;
    ProjectPlan plan;
    Profile profile;
    char error[256];
    size_t a = 0, b = 0, img = 0, i;
    memset(&c, 0, sizeof(c)); memset(&profile, 0, sizeof(profile)); memset(&h, 0, sizeof(h));
    GetTempPathW(MAX_PATH, temp);
    swprintf(base, MAX_PATH, L"%lsproject_tests_%lu", temp, GetCurrentProcessId());
    remove_tree(base);
    swprintf(refs, MAX_PATH, L"%ls\\refs", base); swprintf(project, MAX_PATH, L"%ls\\demo", refs);
    swprintf(dest, MAX_PATH, L"%ls\\out", base);
    CreateDirectoryW(base, NULL); CreateDirectoryW(refs, NULL); CreateDirectoryW(project, NULL);
    swprintf(path, MAX_PATH, L"%ls\\src", project); CreateDirectoryW(path, NULL);
    swprintf(path, MAX_PATH, L"%ls\\a.py", project); CHECK(write_text(path, "print('a')\r\nprint('b')\r\n"));
    swprintf(path, MAX_PATH, L"%ls\\src\\b.js", project); CHECK(write_text(path, "const b = 1;\n"));
    swprintf(path, MAX_PATH, L"%ls\\src\\img.png", project); CHECK(write_text(path, "\x89PNG\r\n\x1a\n\0\0"));
    wcscpy(c.references, refs);
    swprintf(c.progress_folder, MAX_PATH, L"%ls\\progress", base);
    c.copy_patterns[0] = L"*.png"; c.copy_count = 1;
    c.max_typed_bytes = 100000;
    CHECK(project_reference_load(&c, L"demo", &r, error, sizeof(error)));
    CHECK(r.file_count == 3 && r.folder_count == 1);
    for (i = 0; i < r.file_count; ++i) {
        if (!wcscmp(r.files[i].relative, L"a.py")) a = i;
        if (!wcscmp(r.files[i].relative, L"src\\b.js")) b = i;
        if (!wcscmp(r.files[i].relative, L"src\\img.png")) img = i;
    }
    CHECK(r.files[a].kind == PROJECT_TYPE && !wcscmp(r.files[a].text, L"print('a')\nprint('b')\n"));
    CHECK(r.files[img].kind == PROJECT_COPY);
    /* Fresh destination: everything is to be created. */
    profile.local_id = L"T-1";
    CHECK(project_history_load(&c, &profile, &h, error, sizeof(error)) && h.project_count == 0);
    hp = project_history_add(&h, L"demo", dest);
    CHECK(project_plan_build(&r, hp, dest, &plan, error, sizeof(error)));
    CHECK(plan.fresh && plan.missing_folders == 1 && plan.remaining_files == 3 && !plan.conflicts);
    CHECK(plan.steps[a].kind == STEP_CREATE && plan.steps[img].kind == STEP_COPY);
    project_plan_free(&plan);
    /* A partial a.py this profile started resumes; an unknown partial b.js is a conflict. */
    CreateDirectoryW(dest, NULL);
    swprintf(path, MAX_PATH, L"%ls\\src", dest); CreateDirectoryW(path, NULL);
    swprintf(path, MAX_PATH, L"%ls\\a.py", dest); CHECK(write_text(path, "print('a')\r\npri"));
    swprintf(path, MAX_PATH, L"%ls\\src\\b.js", dest); CHECK(write_text(path, "const"));
    CHECK(project_history_mark(hp, L"a.py", r.files[a].sha256, HISTORY_PARTIAL));
    CHECK(project_plan_build(&r, hp, dest, &plan, error, sizeof(error)));
    CHECK(!plan.fresh && plan.missing_folders == 0);
    CHECK(plan.steps[a].kind == STEP_RESUME && plan.steps[a].offset == 14);
    CHECK(plan.steps[b].kind == STEP_CONFLICT && plan.conflicts == 1);
    project_plan_free(&plan);
    /* Finished and matching files are done; history survives a save and reload. */
    swprintf(path, MAX_PATH, L"%ls\\src\\b.js", dest); CHECK(write_text(path, "const b = 1;\n"));
    CHECK(project_plan_build(&r, hp, dest, &plan, error, sizeof(error)));
    CHECK(plan.steps[b].kind == STEP_DONE && !plan.conflicts);
    project_plan_free(&plan);
    CHECK(project_history_mark(hp, L"src\\img.png", "", HISTORY_COPIED));
    hp->completed = 1;
    CHECK(project_history_save(&h, error, sizeof(error)));
    project_history_free(&h);
    CHECK(project_history_load(&c, &profile, &h, error, sizeof(error)));
    CHECK(h.project_count == 1 && h.projects[0].completed && !wcscmp(h.projects[0].destination, dest));
    CHECK(h.project_count && project_history_file(&h.projects[0], L"a.py") &&
        project_history_file(&h.projects[0], L"a.py")->status == HISTORY_PARTIAL &&
        project_history_file(&h.projects[0], L"src\\img.png")->status == HISTORY_COPIED);
    project_history_free(&h);
    project_reference_free(&r);
    c.copy_count = 0;
    remove_tree(base);
}
static void manifest_tests(void) {
    ProjectManifest m;
    char error[256];
    wchar_t text[4096];
    size_t used, i, a[10], b[10], again[10], seen, differ = 0;
    Profile one, two;
    used = (size_t)swprintf(text, 4096, L"{\"version\":1,\"projects\":[");
    for (i = 0; i < 10; ++i)
        used += (size_t)swprintf(text + used, 4096 - used, L"%ls{\"name\":\"p%u\",\"url\":\"https://github.com/o/p%u\","
            L"\"commit\":\"%040u\"}", i ? L"," : L"", (unsigned)i, (unsigned)i, (unsigned)i);
    swprintf(text + used, 4096 - used, L"]}");
    CHECK(project_manifest_parse(text, &m, error, sizeof(error)) && m.count == 10);
    CHECK(project_manifest_find(&m, L"P3") == &m.items[3] && !project_manifest_find(&m, L"p10"));
    CHECK(!strcmp(m.items[2].commit, "0000000000000000000000000000000000000002"));
    /* Each profile has its own fixed order: a permutation that is stable across runs. */
    memset(&one, 0, sizeof(one)); memset(&two, 0, sizeof(two));
    one.local_id = L"WO-0001"; two.local_id = L"WO-0002";
    project_manifest_order(&m, &one, a); project_manifest_order(&m, &two, b); project_manifest_order(&m, &one, again);
    for (i = 0, seen = 0; i < 10; ++i) { seen |= (size_t)1 << a[i]; differ += a[i] != b[i]; CHECK(a[i] == again[i]); }
    CHECK(seen == 0x3ff && differ > 0);
    project_manifest_free(&m);
    CHECK(!project_manifest_parse(L"{\"version\":1,\"projects\":[{\"name\":\"x\",\"url\":\"https://g/x\",\"commit\":\"abc\"}]}", &m, error, sizeof(error)));
    CHECK(!project_manifest_parse(L"{\"version\":1,\"projects\":[{\"name\":\"x\",\"url\":\"http://g/x\","
        L"\"commit\":\"0000000000000000000000000000000000000000\"}]}", &m, error, sizeof(error)));
    CHECK(!project_manifest_parse(L"{\"version\":1,\"projects\":[{\"name\":\"../x\",\"url\":\"https://g/x\","
        L"\"commit\":\"0000000000000000000000000000000000000000\"}]}", &m, error, sizeof(error)));
    CHECK(!project_manifest_parse(L"{\"version\":1,\"projects\":[{\"name\":\"x\",\"url\":\"https://g/x\",\"commit\":\"0000000000000000000000000000000000000000\"},"
        L"{\"name\":\"X\",\"url\":\"https://g/y\",\"commit\":\"0000000000000000000000000000000000000000\"}]}", &m, error, sizeof(error)));
}
int main(int argc, char **argv) {
    manifest_tests();
    pattern_and_typo_tests();
    plan_tests();
    if (argc > 1) {
        wchar_t path[MAX_PATH];
        ProgrammingConfig c;
        char error[256];
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, MAX_PATH);
        CHECK(programming_config_load(path, &c, error, sizeof(error)));
        if (c.max_typed_bytes) { shipped_reference_tests(&c); programming_config_free(&c); }
        else fprintf(stderr, "  %s\n", error);
    } else { ++failures; fprintf(stderr, "FAIL: pass programming.json\n"); }
    printf("%d checks, %d failures (no desktop input generated)\n", checks, failures);
    return failures ? 1 : 0;
}
