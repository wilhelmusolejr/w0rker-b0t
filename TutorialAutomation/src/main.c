#include "script_engine.h"
#include "playback.h"
#include "window_manager.h"
#include "reference_config.h"
#include "playback_controls.h"
#include "profile.h"
#include "browse_catalog.h"
#include "browser_session.h"
#include "project_action.h"
#include "project_manifest.h"
#include "side_apps.h"
#include "loggy3.h"
#include "work_session.h"
#include "flow_config.h"
#include <shellapi.h>
#include <stdlib.h>
#include <wchar.h>
#include <limits.h>
#include <string.h>
#include <stdint.h>

static Playback *active;
static volatile LONG console_stop;
static BOOL WINAPI console_control(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        InterlockedExchange(&console_stop, 1);
        if (active) { InterlockedExchange(&active->user_stop, 1); InterlockedExchange(&active->stopped, 1); }
        return TRUE;
    }
    return FALSE;
}
/* Wait before an automatic restart; F9, F12, or Ctrl+C cancels. Waits longer while the desktop is locked. */
static int restart_wait(Playback *p, unsigned seconds) {
    ULONGLONG started = GetTickCount64();
    playback_log(p,"auto-restart in %u seconds; press F9, F12, or Ctrl+C to cancel",seconds);
    for (;;) {
        if (InterlockedCompareExchange(&console_stop,0,0) || playback_remote_take() == PLAYBACK_REMOTE_STOP ||
            (GetAsyncKeyState(VK_F9) & 0x8000) || (GetAsyncKeyState(VK_F12) & 0x8000)) {
            playback_log(p,"auto-restart cancelled"); return 0;
        }
        if (GetTickCount64()-started >= (ULONGLONG)seconds*1000 && window_desktop_ready()) return 1;
        Sleep(100);
    }
}
static void usage(void) {
    puts("Windows Tutorial Automation\n"
         "tutorial --script FILE [--countdown SECONDS] [--log FILE]\n"
         "tutorial --validate FILE\n"
         "tutorial --dry-run FILE [--log FILE]\n"
         "tutorial --config FILE.json [--countdown SECONDS] [--log FILE]\n"
         "tutorial --validate-config FILE.json | --dry-run-config FILE.json\n"
         "tutorial --browse PROFILE_ID | --dry-run-browse PROFILE_ID [--minutes N] [--seed N]\n"
         "         [--profiles FILE.json] [--browsing FILE.json] [--launch taskbar|run] [--countdown SECONDS] [--log FILE]\n"
         "tutorial --code PROFILE_ID | --dry-run-code PROFILE_ID [--seed N] [--profiles FILE.json]\n"
         "         [--programming FILE.json] [--countdown SECONDS] [--log FILE]\n"
         "tutorial --fetch-projects [--programming FILE.json]   (clone every listed project at its pinned commit)\n"
         "tutorial --apps PROFILE_ID [--app notepad|explorer|calculator|images] [--minutes N] [--seed N]\n"
         "tutorial --session PROFILE_ID | --dry-run-session PROFILE_ID [--seed N] [--countdown SECONDS] [--log FILE]\n"
         "tutorial --flow FILE.json | --validate-flow FILE.json | --dry-run-flow FILE.json\n"
         "         [--seed N] [--countdown SECONDS] [--profiles FILE.json] [--programming FILE.json]\n"
         "         [--browsing FILE.json] [--apps-config FILE.json] [--progress FILE.json] [--restart-flow] [--log FILE]\n"
         "         [--duration-minutes N | --target-minutes N] [--coding-minutes N] [--browsing-minutes N]\n"
         "         (duration: this run, independent of Loggy3; default: today's Loggy3 target with a local fallback)\n"
         "tutorial --loggy3 status|show|minimize|close|record|stop|demo [--dry-run] [--hover-only] [--hover-seconds N]\n"
         "         [--hold SECONDS] [--minimize-after] [--profile ID] [--profiles FILE.json] [--countdown SECONDS] [--log FILE]\n"
         "--browse and --code take side-app breaks from profiles\\apps.json; --no-side-apps turns them off.\n"
         "F8 pause/resume | F9 stop | F12 emergency stop\n"
         "Scripts, JSON configs, and sources must be UTF-8.");
}
static FILE *execution_log(const wchar_t *path) {
    SYSTEMTIME now;
    wchar_t generated[256];
    if (path) return _wfopen(path,L"ab");
    GetLocalTime(&now); CreateDirectoryW(L"logs",NULL);
    swprintf(generated,256,L"logs\\playback-%04u%02u%02u-%02u%02u%02u-%lu.log",
        now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId());
    return _wfopen(generated,L"ab");
}
/* profiles/ sits next to build/ (MinGW) or two levels above build-msvc/Release/ (MSVC). */
static int default_profile_file(const wchar_t *name, wchar_t out[MAX_PATH]) {
    wchar_t exe[MAX_PATH], *slash;
    unsigned up;
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return 0;
    for (up = 0; up < 3; ++up) {
        DWORD attrs;
        if (!(slash = wcsrchr(exe, L'\\'))) return 0;
        *slash = 0;
        if (swprintf(out, MAX_PATH, L"%ls\\profiles\\%ls", exe, name) < 0) return 0;
        attrs = GetFileAttributesW(out);
        if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return 1;
    }
    return 0;
}
static int number_argument(wchar_t **argv, int argc, int *i, unsigned long low, unsigned long high, unsigned long *out) {
    wchar_t *end;
    if (++*i >= argc || !*argv[*i] || argv[*i][0] == L'-') return 0;
    *out = wcstoul(argv[*i], &end, 10);
    return !*end && *out >= low && *out <= high;
}
/* profiles/apps.json, when present and not turned off: side-app breaks during the main task. */
static const SideAppsConfig *load_side_apps(int disabled, SideAppsConfig *config) {
    wchar_t path[MAX_PATH];
    char error[256];
    if (disabled) { puts("Side-app breaks: off (--no-side-apps)."); return NULL; }
    if (!default_profile_file(L"apps.json", path)) { puts("Side-app breaks: off (profiles\\apps.json not found)."); return NULL; }
    if (!side_apps_load(path, config, error, sizeof(error))) { fprintf(stderr, "apps.json: %s; side-app breaks are off\n", error); return NULL; }
    printf("Side-app breaks: %u%% chance every %u-%u minutes of the main task, %u-%u minutes each "
        "(Notepad %u, File Explorer %u, Calculator %u, images %u).\n", config->chance_percent, config->check_minutes[0],
        config->check_minutes[1], config->minutes[0], config->minutes[1], config->weights[APP_NOTEPAD],
        config->weights[APP_EXPLORER], config->weights[APP_CALCULATOR], config->weights[APP_IMAGES]);
    return config;
}
static int browse_main(wchar_t **argv, int argc) {
    const wchar_t *profiles_path = NULL, *browsing_path = NULL, *log_path = NULL;
    wchar_t profiles_default[MAX_PATH], browsing_default[MAX_PATH];
    unsigned long profile_id = 0, minutes = 10, seed = 0, countdown = 10, value;
    int i, dry = 0, taskbar = 1, ok, exit_code = 2;
    ProfileSet profiles = {0};
    BrowseCatalog catalog = {0};
    BrowseSession session = {0};
    SideAppsConfig side_apps;
    int no_side_apps = 0;
    Script script = {0};
    Playback p = {0};
    char error[512];
    for (i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--browse") || !wcscmp(argv[i], L"--dry-run-browse")) {
            dry = !wcscmp(argv[i], L"--dry-run-browse");
            if (profile_id || !number_argument(argv, argc, &i, 1, UINT_MAX, &profile_id)) goto invalid;
        } else if (!wcscmp(argv[i], L"--minutes")) {
            if (!number_argument(argv, argc, &i, 1, 480, &minutes)) goto invalid;
        } else if (!wcscmp(argv[i], L"--seed")) {
            if (!number_argument(argv, argc, &i, 0, INT32_MAX, &seed)) goto invalid;
        } else if (!wcscmp(argv[i], L"--countdown")) {
            if (!number_argument(argv, argc, &i, 0, 3600, &countdown)) goto invalid;
        } else if (!wcscmp(argv[i], L"--profiles") && i+1 < argc && !profiles_path) profiles_path = argv[++i];
        else if (!wcscmp(argv[i], L"--browsing") && i+1 < argc && !browsing_path) browsing_path = argv[++i];
        else if (!wcscmp(argv[i], L"--log") && i+1 < argc && !log_path) log_path = argv[++i];
        else if (!wcscmp(argv[i], L"--no-side-apps")) no_side_apps = 1;
        else if (!wcscmp(argv[i], L"--launch") && i+1 < argc) {
            ++i;
            if (!wcscmp(argv[i], L"taskbar")) taskbar = 1;
            else if (!wcscmp(argv[i], L"run")) taskbar = 0;
            else goto invalid;
        } else goto invalid;
    }
    if (!profiles_path) {
        if (!default_profile_file(L"profiles.json", profiles_default)) { fputs("profiles\\profiles.json not found; use --profiles\n", stderr); return 2; }
        profiles_path = profiles_default;
    }
    if (!browsing_path) {
        if (!default_profile_file(L"browsing.json", browsing_default)) { fputs("profiles\\browsing.json not found; use --browsing\n", stderr); return 2; }
        browsing_path = browsing_default;
    }
    if (!profile_load(profiles_path, &profiles, error, sizeof(error))) { fprintf(stderr, "profiles: %s\n", error); goto finish; }
    if (!browse_catalog_load(browsing_path, &catalog, error, sizeof(error))) { fprintf(stderr, "browsing: %s\n", error); goto finish; }
    session.profile = profile_find(&profiles, (unsigned)profile_id);
    if (!session.profile) { fprintf(stderr, "no profile with id %lu\n", profile_id); goto finish; }
    session.catalog = &catalog;
    session.duration_ms = (DWORD)(minutes * 60000);
    session.seed = seed ? (uint32_t)seed : action_recording_seed();
    session.via_taskbar = taskbar;
    memset(&side_apps, 0, sizeof(side_apps));
    session.side_apps = load_side_apps(no_side_apps, &side_apps);
    if (dry) {
        BrowsePlan plan;
        browser_session_plan(&session, &plan);
        browser_session_print_plan(&session, &plan);
        puts("Dry-run complete; no input generated. Repeat with --seed to open the same tabs live.");
        exit_code = 0; goto finish;
    }
    script.commands = calloc(1, sizeof(*script.commands));
    if (!script.commands) goto finish;
    script.count = 1; script.commands[0].kind = CMD_BROWSE_SESSION; script.commands[0].line = 1;
    p.browse_session = &session;
    p.log = execution_log(log_path);
    if (!p.log) { fputs("Cannot open execution log. Use --log with a writable path.\n", stderr); goto finish; }
    window_enable_dpi();
    active = &p; SetConsoleCtrlHandler(console_control, TRUE);
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    if (!playback_remote_open()) playback_log(&p, "remote control events unavailable; Telegram /pause /resume /stop will not work");
    ok = playback_run(&p, &script, (DWORD)countdown * 1000);
    SetConsoleCtrlHandler(console_control, FALSE); active = NULL;
    playback_remote_close(); SetThreadExecutionState(ES_CONTINUOUS);
    exit_code = ok ? 0 : 1;
    goto finish;
invalid:
    usage();
finish:
    if (p.log) fclose(p.log);
    script_free(&script); browse_catalog_free(&catalog); profile_free(&profiles);
    if (session.side_apps) side_apps_free(&side_apps);
    return exit_code;
}
static int code_main(wchar_t **argv, int argc) {
    const wchar_t *profiles_path = NULL, *programming_path = NULL, *log_path = NULL;
    wchar_t profiles_default[MAX_PATH], programming_default[MAX_PATH], destination[MAX_PATH];
    ProjectManifest manifest = {0};
    unsigned long profile_id = 0, seed = 0, countdown = 10;
    int i, dry = 0, ok, exit_code = 2;
    ProfileSet profiles = {0};
    ProgrammingConfig config;
    ProjectHistory history = {0};
    ProjectReference reference = {0};
    ProjectPlan plan = {0};
    CodingProject job = {0};
    SideAppsConfig side_apps;
    int no_side_apps = 0;
    HistoryProject *started = NULL;
    Script script = {0};
    Playback p = {0};
    char error[512];
    memset(&config, 0, sizeof(config));
    for (i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--code") || !wcscmp(argv[i], L"--dry-run-code")) {
            dry = !wcscmp(argv[i], L"--dry-run-code");
            if (profile_id || !number_argument(argv, argc, &i, 1, UINT_MAX, &profile_id)) goto invalid;
        } else if (!wcscmp(argv[i], L"--seed")) {
            if (!number_argument(argv, argc, &i, 0, INT32_MAX, &seed)) goto invalid;
        } else if (!wcscmp(argv[i], L"--countdown")) {
            if (!number_argument(argv, argc, &i, 0, 3600, &countdown)) goto invalid;
        } else if (!wcscmp(argv[i], L"--profiles") && i+1 < argc && !profiles_path) profiles_path = argv[++i];
        else if (!wcscmp(argv[i], L"--programming") && i+1 < argc && !programming_path) programming_path = argv[++i];
        else if (!wcscmp(argv[i], L"--log") && i+1 < argc && !log_path) log_path = argv[++i];
        else if (!wcscmp(argv[i], L"--no-side-apps")) no_side_apps = 1;
        else goto invalid;
    }
    memset(&side_apps, 0, sizeof(side_apps));
    if (!profiles_path) {
        if (!default_profile_file(L"profiles.json", profiles_default)) { fputs("profiles\\profiles.json not found; use --profiles\n", stderr); return 2; }
        profiles_path = profiles_default;
    }
    if (!programming_path) {
        if (!default_profile_file(L"programming.json", programming_default)) { fputs("profiles\\programming.json not found; use --programming\n", stderr); return 2; }
        programming_path = programming_default;
    }
    if (!profile_load(profiles_path, &profiles, error, sizeof(error))) { fprintf(stderr, "profiles: %s\n", error); goto finish; }
    if (!programming_config_load(programming_path, &config, error, sizeof(error))) { fprintf(stderr, "programming: %s\n", error); goto finish; }
    job.profile = profile_find(&profiles, (unsigned)profile_id);
    if (!job.profile) { fprintf(stderr, "no profile with id %lu\n", profile_id); goto finish; }
    if (job.profile->main_task != PROFILE_TASK_PROGRAMMING || !job.profile->project_path) {
        fputs("this profile's main task is not programming, or it has no project_path\n", stderr); goto finish;
    }
    if (!project_history_load(&config, job.profile, &history, error, sizeof(error))) { fprintf(stderr, "history: %s\n", error); goto finish; }
    if (!project_manifest_load(config.project_list, &manifest, error, sizeof(error))) { fprintf(stderr, "projects: %s\n", error); goto finish; }
    /* A new project gets its history entry now (live runs only); it is written once the folder exists. */
    if (!project_select(&config, &manifest, job.profile, &history, !dry, &reference, &started, destination, error, sizeof(error))) {
        printf("%s\n", error); exit_code = strstr(error, "completed or skipped") ? 0 : 2; goto finish;
    }
    if (!project_plan_build(&reference, started, destination, &plan, error, sizeof(error))) { fprintf(stderr, "%s\n", error); goto finish; }
    job.config = &config; job.history = &history; job.reference = &reference; job.plan = &plan;
    job.seed = seed ? (uint32_t)seed : action_recording_seed();
    job.side_apps = load_side_apps(no_side_apps, &side_apps);
    coding_project_print(&job);
    if (plan.conflicts) { puts("Resolve the conflicts above (or move that folder away) and run again; no input was sent."); goto finish; }
    if (dry) { puts("Dry-run complete; no input generated and no files written."); exit_code = 0; goto finish; }
    script.commands = calloc(1, sizeof(*script.commands));
    if (!script.commands) goto finish;
    script.count = 1; script.commands[0].kind = CMD_CODE_PROJECT; script.commands[0].line = 1;
    p.coding_project = &job;
    p.log = execution_log(log_path);
    if (!p.log) { fputs("Cannot open execution log. Use --log with a writable path.\n", stderr); goto finish; }
    window_enable_dpi();
    active = &p; SetConsoleCtrlHandler(console_control, TRUE);
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    if (!playback_remote_open()) playback_log(&p, "remote control events unavailable; Telegram /pause /resume /stop will not work");
    ok = playback_run(&p, &script, (DWORD)countdown * 1000);
    SetConsoleCtrlHandler(console_control, FALSE); active = NULL;
    playback_remote_close(); SetThreadExecutionState(ES_CONTINUOUS);
    exit_code = ok ? 0 : 1;
    goto finish;
invalid:
    usage();
finish:
    if (p.log) fclose(p.log);
    script_free(&script); project_plan_free(&plan); project_reference_free(&reference);
    project_history_free(&history); programming_config_free(&config); profile_free(&profiles);
    project_manifest_free(&manifest);
    if (job.side_apps) side_apps_free(&side_apps);
    return exit_code;
}
/* One side-app break on its own, to try an app out: --apps PROFILE_ID [--app NAME] [--minutes N]. */
static int apps_main(wchar_t **argv, int argc) {
    const wchar_t *profiles_path = NULL, *log_path = NULL;
    wchar_t profiles_default[MAX_PATH], apps_path[MAX_PATH];
    unsigned long profile_id = 0, minutes = 0, seed = 0, countdown = 10;
    int i, picked = 0, ok, exit_code = 2;
    ProfileSet profiles = {0};
    SideAppsConfig config;
    SideAppJob job;
    Script script = {0};
    Playback p = {0};
    MotionRng rng;
    char error[512];
    memset(&config, 0, sizeof(config)); memset(&job, 0, sizeof(job));
    for (i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--apps")) {
            if (profile_id || !number_argument(argv, argc, &i, 1, UINT_MAX, &profile_id)) goto invalid;
        } else if (!wcscmp(argv[i], L"--app") && i+1 < argc) {
            if (!side_app_from_name(argv[++i], &job.app)) { fputs("--app is notepad, explorer, calculator, or images\n", stderr); return 2; }
            picked = 1;
        } else if (!wcscmp(argv[i], L"--minutes")) {
            if (!number_argument(argv, argc, &i, 1, 240, &minutes)) goto invalid;
        } else if (!wcscmp(argv[i], L"--seed")) {
            if (!number_argument(argv, argc, &i, 0, INT32_MAX, &seed)) goto invalid;
        } else if (!wcscmp(argv[i], L"--countdown")) {
            if (!number_argument(argv, argc, &i, 0, 3600, &countdown)) goto invalid;
        } else if (!wcscmp(argv[i], L"--profiles") && i+1 < argc && !profiles_path) profiles_path = argv[++i];
        else if (!wcscmp(argv[i], L"--log") && i+1 < argc && !log_path) log_path = argv[++i];
        else goto invalid;
    }
    if (!profiles_path) {
        if (!default_profile_file(L"profiles.json", profiles_default)) { fputs("profiles\\profiles.json not found; use --profiles\n", stderr); return 2; }
        profiles_path = profiles_default;
    }
    if (!default_profile_file(L"apps.json", apps_path)) { fputs("profiles\\apps.json not found\n", stderr); return 2; }
    if (!profile_load(profiles_path, &profiles, error, sizeof(error))) { fprintf(stderr, "profiles: %s\n", error); goto finish; }
    if (!side_apps_load(apps_path, &config, error, sizeof(error))) { fprintf(stderr, "apps.json: %s\n", error); goto finish; }
    if (!(job.profile = profile_find(&profiles, (unsigned)profile_id))) { fprintf(stderr, "no profile with id %lu\n", profile_id); goto finish; }
    job.config = &config;
    job.seed = seed ? (uint32_t)seed : action_recording_seed();
    motion_seed(&rng, job.seed);
    if (!picked) job.app = side_apps_pick(&config, &rng);
    job.duration_ms = (DWORD)(minutes ? minutes : motion_between(&rng, config.minutes[0], config.minutes[1])) * 60000;
    printf("Side-app break: %s for %lu minute(s), seed %lu.\n", side_app_name(job.app), job.duration_ms / 60000, (unsigned long)job.seed);
    script.commands = calloc(1, sizeof(*script.commands));
    if (!script.commands) goto finish;
    script.count = 1; script.commands[0].kind = CMD_SIDE_APP; script.commands[0].line = 1;
    p.side_app_job = &job;
    p.log = execution_log(log_path);
    if (!p.log) { fputs("Cannot open execution log. Use --log with a writable path.\n", stderr); goto finish; }
    window_enable_dpi();
    active = &p; SetConsoleCtrlHandler(console_control, TRUE);
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    if (!playback_remote_open()) playback_log(&p, "remote control events unavailable; Telegram /pause /resume /stop will not work");
    ok = playback_run(&p, &script, (DWORD)countdown * 1000);
    SetConsoleCtrlHandler(console_control, FALSE); active = NULL;
    playback_remote_close(); SetThreadExecutionState(ES_CONTINUOUS);
    exit_code = ok ? 0 : 1;
    goto finish;
invalid:
    usage();
finish:
    if (p.log) fclose(p.log);
    script_free(&script); side_apps_free(&config); profile_free(&profiles);
    return exit_code;
}
/* Loggy3 window and recording actions: --loggy3 ACTION. status reads only (no countdown, no input). */
static int loggy_main(wchar_t **argv, int argc) {
    const wchar_t *profiles_path = NULL, *log_path = NULL;
    wchar_t profiles_default[MAX_PATH];
    unsigned long profile_id = 1, seed = 0, countdown = 3, hold = 5, hover = 5;
    int i, ok, exit_code = 2, has_action = 0, countdown_set = 0;
    ProfileSet profiles = {0};
    LoggyJob job;
    Script script = {0};
    Playback p = {0};
    char error[512];
    memset(&job, 0, sizeof(job));
    for (i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--loggy3") && i+1 < argc && !has_action) {
            if (!loggy_action_from_name(argv[++i], &job.action)) { fputs("--loggy3 is status, show, minimize, close, record, stop, or demo\n", stderr); return 2; }
            has_action = 1;
        } else if (!wcscmp(argv[i], L"--dry-run")) job.dry_run = 1;
        else if (!wcscmp(argv[i], L"--hover-only")) job.hover_only = 1;
        else if (!wcscmp(argv[i], L"--minimize-after")) job.minimize_after = 1;
        else if (!wcscmp(argv[i], L"--hover-seconds")) { if (!number_argument(argv, argc, &i, 0, 60, &hover)) goto invalid; }
        else if (!wcscmp(argv[i], L"--hold")) { if (!number_argument(argv, argc, &i, 0, 300, &hold)) goto invalid; }
        else if (!wcscmp(argv[i], L"--profile")) { if (!number_argument(argv, argc, &i, 1, UINT_MAX, &profile_id)) goto invalid; }
        else if (!wcscmp(argv[i], L"--seed")) { if (!number_argument(argv, argc, &i, 0, INT32_MAX, &seed)) goto invalid; }
        else if (!wcscmp(argv[i], L"--countdown")) { if (!number_argument(argv, argc, &i, 0, 3600, &countdown)) goto invalid; countdown_set = 1; }
        else if (!wcscmp(argv[i], L"--profiles") && i+1 < argc && !profiles_path) profiles_path = argv[++i];
        else if (!wcscmp(argv[i], L"--log") && i+1 < argc && !log_path) log_path = argv[++i];
        else goto invalid;
    }
    if (!has_action) goto invalid;
    if (job.hover_only && job.action != LOGGY_RECORD && job.action != LOGGY_STOP) { fputs("--hover-only goes with record or stop\n", stderr); return 2; }
    /* Reading the state sends no input, so it starts at once. */
    if (!countdown_set && (job.action == LOGGY_STATUS || job.dry_run)) countdown = 0;
    if (!profiles_path) {
        if (!default_profile_file(L"profiles.json", profiles_default)) { fputs("profiles\\profiles.json not found; use --profiles\n", stderr); return 2; }
        profiles_path = profiles_default;
    }
    if (!profile_load(profiles_path, &profiles, error, sizeof(error))) { fprintf(stderr, "profiles: %s\n", error); goto finish; }
    if (!(job.profile = profile_find(&profiles, (unsigned)profile_id))) { fprintf(stderr, "no profile with id %lu\n", profile_id); goto finish; }
    job.seed = seed ? (uint32_t)seed : action_recording_seed();
    job.hold_ms = (DWORD)hold * 1000; job.hover_ms = (DWORD)hover * 1000;
    printf("Loggy3 %s%s%s, profile %lu's mouse hand, seed %lu.\n", loggy_action_name(job.action), job.dry_run ? " (dry run)" : "",
        job.hover_only ? " (hover only)" : "", profile_id, (unsigned long)job.seed);
    script.commands = calloc(1, sizeof(*script.commands));
    if (!script.commands) goto finish;
    script.count = 1; script.commands[0].kind = CMD_LOGGY3; script.commands[0].line = 1;
    p.loggy_job = &job;
    p.log = execution_log(log_path);
    if (!p.log) { fputs("Cannot open execution log. Use --log with a writable path.\n", stderr); goto finish; }
    window_enable_dpi();
    active = &p; SetConsoleCtrlHandler(console_control, TRUE);
    if (!playback_remote_open()) playback_log(&p, "remote control events unavailable; Telegram /pause /resume /stop will not work");
    ok = playback_run(&p, &script, (DWORD)countdown * 1000);
    SetConsoleCtrlHandler(console_control, FALSE); active = NULL;
    playback_remote_close();
    if (job.action == LOGGY_STATUS || job.action == LOGGY_RECORD || job.action == LOGGY_STOP)
        printf("Recording state: %s\n", loggy_state_name(job.result));
    exit_code = ok ? 0 : 1;
    goto finish;
invalid:
    usage();
finish:
    if (p.log) fclose(p.log);
    script_free(&script); profile_free(&profiles);
    return exit_code;
}
/* An accessibility work session, or an input-free preview of its plan. */
static int session_main(wchar_t **argv, int argc) {
    const wchar_t *profiles_path = NULL, *log_path = NULL;
    wchar_t path[MAX_PATH];
    unsigned long profile_id = 0, seed = 0, countdown = 10, target = 0, duration = 0, coding = 0, browsing = 0;
    int i, dry = 0, ok, exit_code = 2;
    ProfileSet profiles = {0};
    SessionConfig config;
    ProgrammingConfig programming;
    ProjectManifest manifest = {0};
    BrowseCatalog catalog = {0};
    SideAppsConfig side_apps;
    WorkSession session;
    Script script = {0};
    Playback p = {0};
    char error[512];
    memset(&config, 0, sizeof(config)); memset(&programming, 0, sizeof(programming));
    memset(&side_apps, 0, sizeof(side_apps)); memset(&session, 0, sizeof(session));
    for (i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--session") || !wcscmp(argv[i], L"--dry-run-session")) {
            dry = !wcscmp(argv[i], L"--dry-run-session");
            if (profile_id || !number_argument(argv, argc, &i, 1, UINT_MAX, &profile_id)) goto invalid;
        } else if (!wcscmp(argv[i], L"--seed")) {
            if (!number_argument(argv, argc, &i, 0, INT32_MAX, &seed)) goto invalid;
        } else if (!wcscmp(argv[i], L"--countdown")) {
            if (!number_argument(argv, argc, &i, 0, 3600, &countdown)) goto invalid;
        } else if (!wcscmp(argv[i], L"--target-minutes")) {
            if (!number_argument(argv, argc, &i, 1, 24 * 60, &target)) goto invalid;
        } else if (!wcscmp(argv[i], L"--duration-minutes")) {
            if (!number_argument(argv, argc, &i, 1, 24 * 60, &duration)) goto invalid;
        } else if (!wcscmp(argv[i], L"--coding-minutes")) {
            if (!number_argument(argv, argc, &i, 1, 600, &coding)) goto invalid;
        } else if (!wcscmp(argv[i], L"--browsing-minutes")) {
            if (!number_argument(argv, argc, &i, 1, 600, &browsing)) goto invalid;
        } else if (!wcscmp(argv[i], L"--profiles") && i+1 < argc && !profiles_path) profiles_path = argv[++i];
        else if (!wcscmp(argv[i], L"--log") && i+1 < argc && !log_path) log_path = argv[++i];
        else goto invalid;
    }
    if (target && duration) { fputs("--duration-minutes and --target-minutes cannot be combined\n", stderr); goto invalid; }
    if (!profiles_path) {
        static wchar_t profiles_default[MAX_PATH];
        if (!default_profile_file(L"profiles.json", profiles_default)) { fputs("profiles\\profiles.json not found; use --profiles\n", stderr); return 2; }
        profiles_path = profiles_default;
    }
    if (!profile_load(profiles_path, &profiles, error, sizeof(error))) { fprintf(stderr, "profiles: %s\n", error); goto finish; }
    if (!(session.profile = profile_find(&profiles, (unsigned)profile_id))) { fprintf(stderr, "no profile with id %lu\n", profile_id); goto finish; }
    if (session.profile->main_task != PROFILE_TASK_PROGRAMMING || !session.profile->project_path) {
        fputs("this profile's main task is not programming, or it has no project_path\n", stderr); goto finish;
    }
    if (!default_profile_file(L"session.json", path) || !session_config_load(path, &config, error, sizeof(error))) {
        fprintf(stderr, "session.json: %s\n", *path ? error : "not found"); goto finish;
    }
    /* Preview needs only the profile and session settings, even before projects are downloaded. */
    if (target) config.target_minutes[0] = config.target_minutes[1] = (unsigned)target;
    if (coding) config.coding_minutes[0] = config.coding_minutes[1] = (unsigned)coding;
    if (browsing) config.browsing_minutes[0] = config.browsing_minutes[1] = (unsigned)browsing;
    session.seed = seed ? (uint32_t)seed : action_recording_seed();
    session.duration_minutes = (unsigned)duration;
    if (dry) {
        SessionPlan plan;
        LoggyToday today;
        HWND w = duration ? NULL : loggy_window(NULL);
        long so_far = 0;
        if (duration) config.target_minutes[0] = config.target_minutes[1] = (unsigned)duration;
        else if (w && loggy_today(w, NULL, &today) && today.found) so_far = loggy_today_seconds(&today);
        else puts("(Loggy3's today row is unavailable for this preview; showing a zero-based plan. A live run will try to read it.)");
        session_plan(&config, session.seed, (unsigned)so_far, session.profile->randomness_score, &plan);
        printf("Seed %lu.\n", (unsigned long)session.seed);
        session_print_plan(&config, &plan, session.profile, (unsigned)so_far, duration != 0);
        puts("Dry-run complete; no input generated. Repeat with --seed to get the same session live.");
        exit_code = 0; goto finish;
    }
    if (!default_profile_file(L"programming.json", path) || !programming_config_load(path, &programming, error, sizeof(error))) {
        fprintf(stderr, "programming.json: %s\n", error); goto finish;
    }
    if (!project_manifest_load(programming.project_list, &manifest, error, sizeof(error))) { fprintf(stderr, "projects: %s\n", error); goto finish; }
    if (!default_profile_file(L"browsing.json", path) || !browse_catalog_load(path, &catalog, error, sizeof(error))) {
        fprintf(stderr, "browsing.json: %s\n", error); goto finish;
    }
    if (!default_profile_file(L"apps.json", path) || !side_apps_load(path, &side_apps, error, sizeof(error))) {
        fprintf(stderr, "apps.json: %s\n", error); goto finish;
    }
    session.config = &config; session.programming = &programming; session.manifest = &manifest;
    session.catalog = &catalog; session.side_apps = &side_apps;
    script.commands = calloc(1, sizeof(*script.commands));
    if (!script.commands) goto finish;
    script.count = 1; script.commands[0].kind = CMD_WORK_SESSION; script.commands[0].line = 1;
    p.work_session = &session;
    p.log = execution_log(log_path);
    if (!p.log) { fputs("Cannot open execution log. Use --log with a writable path.\n", stderr); goto finish; }
    window_enable_dpi();
    active = &p; SetConsoleCtrlHandler(console_control, TRUE);
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    if (!playback_remote_open()) playback_log(&p, "remote control events unavailable; Telegram /pause /resume /stop will not work");
    ok = playback_run(&p, &script, (DWORD)countdown * 1000);
    SetConsoleCtrlHandler(console_control, FALSE); active = NULL;
    playback_remote_close(); SetThreadExecutionState(ES_CONTINUOUS);
    exit_code = ok ? 0 : 1;
    goto finish;
invalid:
    usage();
finish:
    if (p.log) fclose(p.log);
    script_free(&script); browse_catalog_free(&catalog); side_apps_free(&side_apps);
    project_manifest_free(&manifest); programming_config_free(&programming); profile_free(&profiles);
    return exit_code;
}
/* A sequential JSON flow. Only listed actions run; Loggy3 is never started automatically. */
static int flow_main(wchar_t **argv, int argc) {
    const wchar_t *flow_path = NULL, *profiles_path = NULL, *programming_path = NULL;
    const wchar_t *browsing_path = NULL, *apps_path = NULL, *log_path = NULL, *progress_path = NULL;
    wchar_t profiles_default[MAX_PATH], programming_default[MAX_PATH], browsing_default[MAX_PATH];
    wchar_t apps_default[MAX_PATH], generated_progress[MAX_PATH], workflow_wide[80];
    unsigned long seed = 0, countdown = 10;
    int i, mode = 0, restart = 0, code_needed = 0, browse_needed = 0, apps_needed = 0;
    int ok, exit_code = 2;
    FlowConfig flow;
    ProfileSet profiles = {0};
    ProgrammingConfig programming = {0};
    ProjectManifest manifest = {0};
    BrowseCatalog catalog = {0};
    SideAppsConfig apps = {0};
    WorkSession resources = {0};
    FlowJob job = {0};
    Script script = {0};
    Playback p = {0};
    char error[512];
    for (i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--flow") || !wcscmp(argv[i], L"--validate-flow") || !wcscmp(argv[i], L"--dry-run-flow")) {
            if (mode || i + 1 >= argc) goto invalid;
            mode = !wcscmp(argv[i], L"--flow") ? 1 : !wcscmp(argv[i], L"--validate-flow") ? 2 : 3;
            flow_path = argv[++i];
        } else if (!wcscmp(argv[i], L"--seed")) {
            if (!number_argument(argv, argc, &i, 0, INT32_MAX, &seed)) goto invalid;
        } else if (!wcscmp(argv[i], L"--countdown")) {
            if (!number_argument(argv, argc, &i, 0, 3600, &countdown)) goto invalid;
        } else if (!wcscmp(argv[i], L"--profiles") && i+1 < argc && !profiles_path) profiles_path = argv[++i];
        else if (!wcscmp(argv[i], L"--programming") && i+1 < argc && !programming_path) programming_path = argv[++i];
        else if (!wcscmp(argv[i], L"--browsing") && i+1 < argc && !browsing_path) browsing_path = argv[++i];
        else if (!wcscmp(argv[i], L"--apps-config") && i+1 < argc && !apps_path) apps_path = argv[++i];
        else if (!wcscmp(argv[i], L"--progress") && i+1 < argc && !progress_path) progress_path = argv[++i];
        else if (!wcscmp(argv[i], L"--log") && i+1 < argc && !log_path) log_path = argv[++i];
        else if (!wcscmp(argv[i], L"--restart-flow")) restart = 1;
        else goto invalid;
    }
    if (!mode || (restart && mode != 1)) goto invalid;
    if (!flow_config_load(flow_path, &flow, error, sizeof(error))) { fprintf(stderr, "flow: %s\n", error); goto finish; }
    flow_config_print(&flow);
    if (mode == 2) { exit_code = 0; goto finish; }
    if (!profiles_path) {
        if (!default_profile_file(L"profiles.json", profiles_default)) { fputs("profiles\\profiles.json not found; use --profiles\n", stderr); goto finish; }
        profiles_path = profiles_default;
    }
    if (!profile_load(profiles_path, &profiles, error, sizeof(error))) { fprintf(stderr, "profiles: %s\n", error); goto finish; }
    resources.profile = profile_find(&profiles, flow.profile_id);
    if (!resources.profile) { fprintf(stderr, "no profile with id %u\n", flow.profile_id); goto finish; }
    if (mode == 3) { puts("Dry-run complete; no input generated and no progress written."); exit_code = 0; goto finish; }
    for (i = 0; i < (int)flow.step_count; ++i) {
        FlowAction action = flow.steps[i].action;
        if (action == FLOW_CODE) code_needed = 1;
        if (action == FLOW_BROWSE) browse_needed = 1;
        if (action == FLOW_NOTES || action == FLOW_CALCULATE) apps_needed = 1;
    }
    if (code_needed || (flow.checkpoint_after_each_step && !progress_path)) {
        if (!programming_path) {
            if (!default_profile_file(L"programming.json", programming_default)) { fputs("profiles\\programming.json not found; use --programming\n", stderr); goto finish; }
            programming_path = programming_default;
        }
        if (!programming_config_load(programming_path, &programming, error, sizeof(error))) { fprintf(stderr, "programming: %s\n", error); goto finish; }
        resources.programming = &programming;
    }
    if (code_needed) {
        if (resources.profile->main_task != PROFILE_TASK_PROGRAMMING || !resources.profile->project_path) {
            fputs("this profile has no programming project_path\n", stderr); goto finish;
        }
        if (!project_manifest_load(programming.project_list, &manifest, error, sizeof(error))) { fprintf(stderr, "projects: %s\n", error); goto finish; }
        resources.manifest = &manifest;
    }
    if (browse_needed) {
        if (!browsing_path) {
            if (!default_profile_file(L"browsing.json", browsing_default)) { fputs("profiles\\browsing.json not found; use --browsing\n", stderr); goto finish; }
            browsing_path = browsing_default;
        }
        if (!browse_catalog_load(browsing_path, &catalog, error, sizeof(error))) { fprintf(stderr, "browsing: %s\n", error); goto finish; }
        resources.catalog = &catalog;
    }
    if (apps_needed) {
        if (!apps_path) {
            if (!default_profile_file(L"apps.json", apps_default)) { fputs("profiles\\apps.json not found; use --apps-config\n", stderr); goto finish; }
            apps_path = apps_default;
        }
        if (!side_apps_load(apps_path, &apps, error, sizeof(error))) { fprintf(stderr, "apps: %s\n", error); goto finish; }
        job.apps = &apps;
    }
    if (flow.checkpoint_after_each_step && !progress_path) {
        size_t j;
        if (!CreateDirectoryW(programming.progress_folder, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
            fputs("cannot create flow progress folder\n", stderr); goto finish;
        }
        for (j = 0; flow.workflow_id[j]; ++j) workflow_wide[j] = (wchar_t)flow.workflow_id[j];
        workflow_wide[j] = 0;
        if (swprintf(generated_progress, MAX_PATH, L"%ls\\flow-%ls-%u.json", programming.progress_folder,
                     workflow_wide, flow.profile_id) < 0) { fputs("flow progress path is too long\n", stderr); goto finish; }
        progress_path = generated_progress;
    }
    job.config = &flow; job.resources = &resources; job.progress_path = progress_path;
    job.seed = seed ? (uint32_t)seed : action_recording_seed(); job.restart = restart;
    script.commands = calloc(1, sizeof(*script.commands));
    if (!script.commands) goto finish;
    script.count = 1; script.commands[0].kind = CMD_FLOW; script.commands[0].line = 1;
    p.flow_job = &job;
    p.log = execution_log(log_path);
    if (!p.log) { fputs("Cannot open execution log. Use --log with a writable path.\n", stderr); goto finish; }
    window_enable_dpi();
    active = &p; SetConsoleCtrlHandler(console_control, TRUE);
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    if (!playback_remote_open()) playback_log(&p, "remote control events unavailable; Telegram /pause /resume /stop will not work");
    ok = playback_run(&p, &script, (DWORD)countdown * 1000);
    SetConsoleCtrlHandler(console_control, FALSE); active = NULL;
    playback_remote_close(); SetThreadExecutionState(ES_CONTINUOUS);
    exit_code = ok ? 0 : 1;
    goto finish;
invalid:
    usage();
finish:
    if (p.log) fclose(p.log);
    script_free(&script); side_apps_free(&apps); browse_catalog_free(&catalog);
    project_manifest_free(&manifest); programming_config_free(&programming); profile_free(&profiles);
    return exit_code;
}
/* Clone every project in the list ahead of time, at its pinned commit. */
static int fetch_main(wchar_t **argv, int argc) {
    const wchar_t *programming_path = NULL;
    wchar_t programming_default[MAX_PATH];
    ProgrammingConfig config;
    ProjectManifest manifest = {0};
    char error[512];
    size_t i, failed = 0;
    int a;
    memset(&config, 0, sizeof(config));
    for (a = 1; a < argc; ++a) {
        if (!wcscmp(argv[a], L"--fetch-projects")) continue;
        if (!wcscmp(argv[a], L"--programming") && a+1 < argc && !programming_path) programming_path = argv[++a];
        else { usage(); return 2; }
    }
    if (!programming_path) {
        if (!default_profile_file(L"programming.json", programming_default)) { fputs("profiles\\programming.json not found; use --programming\n", stderr); return 2; }
        programming_path = programming_default;
    }
    if (!programming_config_load(programming_path, &config, error, sizeof(error))) { fprintf(stderr, "programming: %s\n", error); return 2; }
    if (!project_manifest_load(config.project_list, &manifest, error, sizeof(error))) {
        fprintf(stderr, "projects: %s\n", error); programming_config_free(&config); return 2;
    }
    for (i = 0; i < manifest.count; ++i) {
        char name[200];
        WideCharToMultiByte(CP_UTF8, 0, manifest.items[i].name, -1, name, sizeof(name), NULL, NULL);
        if (project_fetch(&config, &manifest.items[i], NULL, error, sizeof(error))) printf("ready: %s\n", name);
        else { printf("FAILED: %s: %s\n", name, error); ++failed; }
    }
    printf("%zu of %zu project(s) ready in the reference folder.\n", manifest.count - failed, manifest.count);
    project_manifest_free(&manifest); programming_config_free(&config);
    return failed ? 1 : 0;
}
int main(void) {
    int argc, i, mode = 0, config_mode = 0, ok, exit_code = 2, countdown_selected = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const wchar_t *path = NULL, *log_path = NULL;
    DWORD countdown = 3;
    Script script = {0}; Playback p = {0}; char error[512];
    ReferenceConfig config = {0};
    if (!argv) return 2;
    for (i = 1; i < argc; ++i)
        if (!wcscmp(argv[i], L"--browse") || !wcscmp(argv[i], L"--dry-run-browse")) {
            exit_code = browse_main(argv, argc); LocalFree(argv); return exit_code;
        } else if (!wcscmp(argv[i], L"--code") || !wcscmp(argv[i], L"--dry-run-code")) {
            exit_code = code_main(argv, argc); LocalFree(argv); return exit_code;
        } else if (!wcscmp(argv[i], L"--apps")) {
            exit_code = apps_main(argv, argc); LocalFree(argv); return exit_code;
        } else if (!wcscmp(argv[i], L"--session") || !wcscmp(argv[i], L"--dry-run-session")) {
            exit_code = session_main(argv, argc); LocalFree(argv); return exit_code;
        } else if (!wcscmp(argv[i], L"--flow") || !wcscmp(argv[i], L"--validate-flow") || !wcscmp(argv[i], L"--dry-run-flow")) {
            exit_code = flow_main(argv, argc); LocalFree(argv); return exit_code;
        } else if (!wcscmp(argv[i], L"--loggy3")) {
            exit_code = loggy_main(argv, argc); LocalFree(argv); return exit_code;
        } else if (!wcscmp(argv[i], L"--fetch-projects")) {
            exit_code = fetch_main(argv, argc); LocalFree(argv); return exit_code;
        }
    for (i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--help")) { usage(); exit_code = 0; goto finish; }
        if (!wcscmp(argv[i], L"--script") || !wcscmp(argv[i], L"--validate") || !wcscmp(argv[i], L"--dry-run") ||
            !wcscmp(argv[i], L"--config") || !wcscmp(argv[i], L"--validate-config") || !wcscmp(argv[i], L"--dry-run-config")) {
            if (mode || i+1 >= argc) goto invalid;
            config_mode = wcsstr(argv[i],L"config") != NULL;
            mode = (!wcscmp(argv[i], L"--script") || !wcscmp(argv[i], L"--config")) ? 1 :
                (!wcscmp(argv[i], L"--validate") || !wcscmp(argv[i], L"--validate-config")) ? 2 : 3;
            path = argv[++i];
        } else if (!wcscmp(argv[i], L"--countdown")) {
            wchar_t *end; unsigned long value;
            if (++i >= argc || !*argv[i] || argv[i][0] == L'-') goto invalid;
            value = wcstoul(argv[i], &end, 10);
            if (*end || value > 3600) goto invalid;
            countdown = (DWORD)value;
            countdown_selected = 1;
        } else if (!wcscmp(argv[i], L"--log")) {
            if (++i >= argc || log_path) goto invalid;
            log_path = argv[i];
        } else goto invalid;
    }
    if (!mode) goto invalid;
    if (mode == 1) {
        p.log = execution_log(log_path);
        if (!p.log) { fputs("Cannot open execution log. Use --log with a writable path.\n",stderr); goto finish; }
    }
    if (config_mode) {
        if (!reference_config_load(path,&config,error,sizeof(error)) ||
            !reference_config_preflight(&config,error,sizeof(error))) {
            fprintf(stderr,"%s\n",error);
            if (p.log) playback_log(&p,"startup check failed: %s",error);
            goto finish;
        }
        p.reference_config = &config;
        script.commands = calloc(1,sizeof(*script.commands));
        if (!script.commands) goto finish;
        script.count = 1; script.commands[0].kind = CMD_REFERENCE_BATCH; script.commands[0].line = 1;
        if (!countdown_selected) countdown = config.countdown_seconds;
        { size_t f, enabled = 0, done = 0, units = 0;
          for (f = 0; f < config.file_count; ++f) if (config.files[f].enabled) {
              ++enabled; done += config.files[f].done != 0;
              units += wcslen(config.files[f].normalized)-config.files[f].offset;
          }
          printf("Reference config: %zu enabled files, %zu already complete, %zu remaining UTF-16 units.\n",enabled,done,units);
          if (config.auto_restart)
              printf("Auto-restart: up to %u times, %u seconds after an unexpected stop (never after F9/F12/Ctrl+C).\n",
                  config.max_restarts,config.restart_wait_seconds);
        }
    } else if (!script_load(path, &script, error, sizeof(error))) {
        fprintf(stderr,"%s\n",error);
        if (p.log) playback_log(&p,"startup check failed: %s",error);
        goto finish;
    }
    if (!config_mode && !countdown_selected) {
        size_t step;
        for (step = 0; step < script.count; ++step)
            if (script.commands[step].kind == CMD_CODE_REFERENCE ||
                script.commands[step].kind == CMD_DESKTOP_CODE_REFERENCE ||
                script.commands[step].kind == CMD_BROWSER_SCROLL) { countdown = 10; break; }
    }
    if (mode == 2) { printf("Valid: %zu commands; no input generated.\n", script.count); exit_code = 0; goto finish; }
    if (config_mode && mode == 3) {
        size_t f;
        printf("DRY-RUN: desktop -> folder -> cmd -> code . -> VS Code new file / Save As; countdown %lu seconds.\n",countdown);
        printf("Typing %u-%u ms/character, %u-%u ms/line; pauses %u-%u ms every %u-%u characters.\n",
            config.char_ms[0],config.char_ms[1],config.newline_ms[0],config.newline_ms[1],
            config.typing_pause_ms[0],config.typing_pause_ms[1],config.pause_every_chars[0],config.pause_every_chars[1]);
        printf("Browser %s (%s launch): random Python / JavaScript / GitHub / URL-list / research pages every %u-%u coding seconds, visits %u-%u seconds.\n",
            config.browser_enabled ? "enabled" : "disabled",
            config.browser_launch == REFERENCE_LAUNCH_TASKBAR ? "taskbar icon" : "Windows Run",config.interval_seconds[0],config.interval_seconds[1],
            config.visit_seconds[0],config.visit_seconds[1]);
        { size_t topic; unsigned counts[REFERENCE_CATEGORY_COUNT] = {0};
          for (topic = 0; topic < config.topic_count; ++topic) ++counts[config.topics[topic].category];
          printf("Browser choices: %u Python, %u JavaScript, %u GitHub, %u URL-list pages, %u research queries.\n",
              counts[REFERENCE_PYTHON],counts[REFERENCE_JAVASCRIPT],counts[REFERENCE_GITHUB],counts[REFERENCE_URL],counts[REFERENCE_RESEARCH]);
          { unsigned k, total = 0, any = 0, share[REFERENCE_CATEGORY_COUNT] = {0};
            for (k = 0; k < REFERENCE_CATEGORY_COUNT; ++k) if (counts[k]) any |= config.weights[k];
            for (k = 0; k < REFERENCE_CATEGORY_COUNT; ++k) if (counts[k]) total += share[k] = any ? config.weights[k] : 1;
            if (total) printf("Visit mix: research %u%%, URL list %u%%, Python docs %u%%, JavaScript docs %u%%, GitHub %u%%; research opens %u-%u result(s).\n",
                share[REFERENCE_RESEARCH]*100/total,share[REFERENCE_URL]*100/total,
                share[REFERENCE_PYTHON]*100/total,share[REFERENCE_JAVASCRIPT]*100/total,share[REFERENCE_GITHUB]*100/total,
                config.results_to_open[0],config.results_to_open[1]);
          }
        }
        for (f = 0; f < config.file_count; ++f) {
            char name[200]; WideCharToMultiByte(CP_UTF8,0,config.files[f].name,-1,name,sizeof(name),NULL,NULL);
            printf("  %s %s: %zu characters, resume offset %zu\n",!config.files[f].enabled ? "DISABLED" :
                config.files[f].done ? "SKIP COMPLETE" : config.files[f].owned ? "RESUME" : "CREATE",
                name,wcslen(config.files[f].normalized),config.files[f].offset);
        }
        puts("Dry-run complete; no input generated or output files written."); exit_code = 0; goto finish;
    }
    p.dry_run = mode == 3;
    if (!p.log) p.log = execution_log(log_path);
    if (!p.log) { fputs("Cannot open execution log. Use --log with a writable path.\n", stderr); goto finish; }
    if (!p.dry_run) window_enable_dpi();
    active = &p; SetConsoleCtrlHandler(console_control, TRUE);
    if (!p.dry_run) {
        /* Keep the display and system awake for the whole job, including restart waits. */
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
        if (!playback_remote_open()) playback_log(&p,"remote control events unavailable; Telegram /pause /resume /stop will not work");
    }
    ok = playback_run(&p, &script, countdown * 1000);
    { unsigned restarts = 0;
      while (!ok && !p.dry_run && config_mode && config.auto_restart && restarts < config.max_restarts &&
             !InterlockedCompareExchange(&p.user_stop,0,0) && !InterlockedCompareExchange(&console_stop,0,0)) {
          FILE *log = p.log;
          active = NULL;
          ++restarts;
          playback_log(&p,"unexpected stop; automatic restart %u of %u",restarts,config.max_restarts);
          if (!restart_wait(&p,config.restart_wait_seconds)) break;
          /* Re-check every output so the restart resumes from what was actually saved. */
          if (!reference_config_preflight(&config,error,sizeof(error))) {
              playback_log(&p,"restart check failed: %s",error); fprintf(stderr,"%s\n",error); break;
          }
          memset(&p,0,sizeof(p)); p.log = log; p.reference_config = &config;
          active = &p;
          ok = playback_run(&p, &script, countdown * 1000);
      }
    }
    SetConsoleCtrlHandler(console_control, FALSE); active = NULL;
    if (!p.dry_run) { playback_remote_close(); SetThreadExecutionState(ES_CONTINUOUS); }
    exit_code = ok ? 0 : 1;
    goto finish;
invalid:
    usage();
finish:
    if (p.log) fclose(p.log);
    script_free(&script); reference_config_free(&config); LocalFree(argv); return exit_code;
}
