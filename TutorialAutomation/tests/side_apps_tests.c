#include "side_apps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
static int checks, failures;
#define CHECK(t) do { ++checks; if (!(t)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#t); } } while (0)

static void parse_tests(void) {
    SideAppsConfig c;
    char error[256];
    SideApp app;
    CHECK(!side_apps_parse(L"{\"version\":1}", &c, error, sizeof(error)));
    CHECK(!side_apps_parse(L"{\"version\":1,\"chance_percent\":20,\"check_every_minutes\":[8,15],\"minutes\":[10,20],"
        L"\"leave_open_percent\":20,\"apps\":{\"notepad\":0,\"explorer\":0,\"calculator\":0,\"images\":0},"
        L"\"notes_folder\":\"Documents\\\\Notes\",\"folders\":[\"Desktop\"],\"image_folders\":[\"Pictures\"],\"notes\":[\"x\"]}",
        &c, error, sizeof(error)));   /* every weight 0 */
    CHECK(!side_apps_parse(L"{\"version\":1,\"chance_percent\":20,\"check_every_minutes\":[15,8],\"minutes\":[10,20],"
        L"\"leave_open_percent\":20,\"apps\":{\"notepad\":1},"
        L"\"notes_folder\":\"Documents\\\\Notes\",\"folders\":[\"Desktop\"],\"image_folders\":[\"Pictures\"],\"notes\":[\"x\"]}",
        &c, error, sizeof(error)));   /* min above max */
    CHECK(side_app_from_name(L"Calculator", &app) && app == APP_CALCULATOR);
    CHECK(!side_app_from_name(L"paint", &app));
}
static void shipped_tests(const wchar_t *apps_path, const wchar_t *profiles_path) {
    SideAppsConfig c;
    ProfileSet profiles;
    const Profile *wilhelmus;
    char error[256];
    MotionRng rng;
    unsigned counts[APP_COUNT] = {0}, i;
    wchar_t note[2100], folder[MAX_PATH], desktop[MAX_PATH];
    CHECK(side_apps_load(apps_path, &c, error, sizeof(error)));
    CHECK(profile_load(profiles_path, &profiles, error, sizeof(error)));
    if (!c.note_count || !profiles.count) { fprintf(stderr, "  %s\n", error); return; }
    wilhelmus = profile_find(&profiles, 1);
    CHECK(c.chance_percent == 20 && c.minutes[0] == 10 && c.minutes[1] == 20);
    /* Apps are picked roughly by their weights (30/30/15/25). */
    motion_seed(&rng, 9);
    for (i = 0; i < 4000; ++i) ++counts[side_apps_pick(&c, &rng)];
    CHECK(counts[APP_NOTEPAD] > 1000 && counts[APP_NOTEPAD] < 1400);
    CHECK(counts[APP_CALCULATOR] > 450 && counts[APP_CALCULATOR] < 750);
    /* Notes are filled from the profile: no placeholders left. */
    for (i = 0; i < 200; ++i) {
        CHECK(side_apps_note(&c, wilhelmus, &rng, note, 2100));
        CHECK(!wcschr(note, L'{') && !wcschr(note, L'}') && wcslen(note) > 10);
    }
    /* Folder names resolve to real folders; Projects is the profile's project_path. */
    CHECK(side_apps_folder(L"Documents", wilhelmus, folder) && GetFileAttributesW(folder) != INVALID_FILE_ATTRIBUTES);
    CHECK(side_apps_folder(L"Downloads", wilhelmus, folder) && wcsstr(folder, L"\\Downloads"));
    CHECK(side_apps_folder(L"Documents\\Notes", wilhelmus, folder) && wcsstr(folder, L"\\Notes"));
    CHECK(profile_project_root(wilhelmus, desktop, MAX_PATH) && side_apps_folder(L"Projects", wilhelmus, folder) && !wcscmp(folder, desktop));
    CHECK(!side_apps_folder(L"Documents\\..\\..", wilhelmus, folder));
    CHECK(!side_apps_folder(L"Nowhere", wilhelmus, folder));
    side_apps_free(&c);
    profile_free(&profiles);
}
int main(int argc, char **argv) {
    parse_tests();
    if (argc > 2) {
        wchar_t apps[MAX_PATH], profiles[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, apps, MAX_PATH);
        MultiByteToWideChar(CP_ACP, 0, argv[2], -1, profiles, MAX_PATH);
        shipped_tests(apps, profiles);
    } else { ++failures; fprintf(stderr, "FAIL: pass apps.json and profiles.json\n"); }
    printf("%d checks, %d failures (no desktop input generated)\n", checks, failures);
    return failures ? 1 : 0;
}
