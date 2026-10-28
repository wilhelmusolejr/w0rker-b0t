#include "script_engine.h"
#include "playback.h"
#include "window_manager.h"
#include "reference_config.h"
#include "playback_controls.h"
#include <shellapi.h>
#include <stdlib.h>
#include <wchar.h>

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
int main(void) {
    int argc, i, mode = 0, config_mode = 0, ok, exit_code = 2, countdown_selected = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const wchar_t *path = NULL, *log_path = NULL;
    DWORD countdown = 3;
    Script script = {0}; Playback p = {0}; char error[512];
    ReferenceConfig config = {0};
    if (!argv) return 2;
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
