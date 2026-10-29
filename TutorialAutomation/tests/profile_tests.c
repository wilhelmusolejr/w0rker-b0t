#include "profile.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
static int checks, failures;
#define CHECK(t) do { ++checks; if (!(t)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#t); } } while (0)

/* A valid profile as key/value pairs; tests replace one value, drop one key, or add one. */
static const wchar_t *const base[][2] = {
    {L"id", L"1"}, {L"local_id", L"\"A-1\""}, {L"name", L"\"Ann\""}, {L"gender", L"\"female\""},
    {L"occupation", L"\"Programmer\""}, {L"computer_name", L"\"ANN-PC\""}, {L"main_task", L"\"programming\""},
    {L"interests", L"[\"baking\"]"}, {L"randomness_score", L"50"}, {L"typo_percent", L"2"}
};
#define BASE_COUNT (sizeof(base)/sizeof(*base))
/* value NULL removes key; an unknown key is appended. */
static int parses_with(const wchar_t *key, const wchar_t *value) {
    wchar_t text[2048];
    ProfileSet set;
    char error[256];
    size_t i, used = 0;
    int found = 0, first = 1, ok;
    used += (size_t)swprintf(text, 2048, L"{\"version\":1,\"profiles\":[{");
    for (i = 0; i < BASE_COUNT; ++i) {
        const wchar_t *v = base[i][1];
        if (key && !wcscmp(key, base[i][0])) { found = 1; if (!value) continue; v = value; }
        used += (size_t)swprintf(text+used, 2048-used, L"%ls\"%ls\":%ls", first ? L"" : L",", base[i][0], v);
        first = 0;
    }
    if (key && !found) used += (size_t)swprintf(text+used, 2048-used, L",\"%ls\":%ls", key, value);
    swprintf(text+used, 2048-used, L"}]}");
    ok = profile_parse(text, &set, error, sizeof(error));
    profile_free(&set);
    return ok;
}
static void field_tests(void) {
    ProfileSet set;
    char error[256];
    CHECK(parses_with(NULL, NULL));
    CHECK(parses_with(L"side_tasks", L"[\"browser\",\"excel\"]"));
    CHECK(parses_with(L"hobbies", L"[\"yoga\"]"));
    CHECK(!parses_with(L"side_tasks", L"[\"programming\"]"));
    CHECK(!parses_with(L"side_tasks", L"[\"browser\",\"browser\"]"));
    CHECK(!parses_with(L"side_tasks", L"[\"gaming\"]"));
    CHECK(!parses_with(L"hobbies", L"[\"yoga\",\"Yoga\"]"));
    CHECK(!parses_with(L"hobbies", L"[\"\"]"));
    CHECK(!parses_with(L"unknown_field", L"1"));
    CHECK(!parses_with(L"interests", NULL));
    CHECK(!parses_with(L"typo_percent", NULL));
    CHECK(!profile_parse(L"{\"version\":2,\"profiles\":[]}", &set, error, sizeof(error)));
    CHECK(!profile_parse(L"{\"version\":1,\"profiles\":[]}", &set, error, sizeof(error)));
    CHECK(!profile_parse(L"{\"version\":1,\"profiles\":[{\"id\":1}]}", &set, error, sizeof(error)));
    profile_free(&set);
}
static void value_tests(void) {
    static const wchar_t *const bad[][2] = {
        {L"id", L"0"}, {L"local_id", L"\"has space\""}, {L"gender", L"\"other\""}, {L"name", L"\"\""},
        {L"computer_name", L"\"THIS-NAME-IS-TOO-LONG\""}, {L"computer_name", L"\"12345\""},
        {L"computer_name", L"\"-PC\""}, {L"computer_name", L"\"PC_1\""}, {L"main_task", L"\"gaming\""},
        {L"randomness_score", L"101"}, {L"typo_percent", L"21"}, {L"interests", L"[]"}
    };
    size_t i;
    for (i = 0; i < sizeof(bad)/sizeof(*bad); ++i) {
        int ok = parses_with(bad[i][0], bad[i][1]);
        CHECK(!ok);
        if (ok) fprintf(stderr, "  accepted %ls=%ls\n", bad[i][0], bad[i][1]);
    }
}
static void duplicate_tests(void) {
    ProfileSet set;
    char error[256];
    const wchar_t *one = L"{\"id\":%u,\"local_id\":\"%ls\",\"name\":\"N\",\"gender\":\"male\",\"occupation\":\"Programmer\","
        L"\"computer_name\":\"PC-1\",\"main_task\":\"programming\",\"interests\":[\"x\"],\"randomness_score\":1,\"typo_percent\":1}";
    wchar_t a[512], b[512], text[1200];
    swprintf(a, 512, one, 1u, L"X-1"); swprintf(b, 512, one, 1u, L"X-2");
    swprintf(text, 1200, L"{\"version\":1,\"profiles\":[%ls,%ls]}", a, b);
    CHECK(!profile_parse(text, &set, error, sizeof(error)));
    swprintf(b, 512, one, 2u, L"x-1");
    swprintf(text, 1200, L"{\"version\":1,\"profiles\":[%ls,%ls]}", a, b);
    CHECK(!profile_parse(text, &set, error, sizeof(error)));
    swprintf(b, 512, one, 2u, L"X-2");
    swprintf(text, 1200, L"{\"version\":1,\"profiles\":[%ls,%ls]}", a, b);
    CHECK(profile_parse(text, &set, error, sizeof(error)) && set.count == 2);
    CHECK(profile_find(&set, 2) && !wcscmp(profile_find(&set, 2)->local_id, L"X-2"));
    CHECK(!profile_find(&set, 3));
    profile_free(&set);
}
static void shipped_file_tests(const wchar_t *path) {
    ProfileSet set;
    char error[256];
    const Profile *w, *x;
    CHECK(profile_load(path, &set, error, sizeof(error)));
    if (!set.count) { fprintf(stderr, "  %s\n", error); return; }
    CHECK(set.count == 2);
    w = profile_find(&set, 1); x = profile_find(&set, 2);
    CHECK(w && !wcscmp(w->name, L"Wilhelmus Ole") && w->gender == PROFILE_MALE && w->main_task == PROFILE_TASK_PROGRAMMING);
    CHECK(x && !wcscmp(x->name, L"Williana Ole") && x->gender == PROFILE_FEMALE && x->main_task == PROFILE_TASK_PROGRAMMING);
    CHECK(w && w->interest_count > 0 && x && x->interest_count > 0);
    profile_free(&set);
}
int main(int argc, char **argv) {
    field_tests();
    value_tests();
    duplicate_tests();
    if (argc > 1) {
        wchar_t path[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, MAX_PATH);
        shipped_file_tests(path);
    }
    else { ++failures; fprintf(stderr, "FAIL: pass the path to profiles.json\n"); }
    printf("%d checks, %d failures (no desktop input generated)\n", checks, failures);
    return failures ? 1 : 0;
}
