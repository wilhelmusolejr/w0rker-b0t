/* Fake UI receiver: missing palettes/pickers must never receive setup text in the editor. */
#include "vscode_editor.h"
#include "keyboard.h"
#include "presentation.h"
#include "ui_targets.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>
static int checks, failures;
static unsigned input_checks, fail_input, typed, enters, status_checks;
static int status_ready;
static wchar_t texts[2][64], names[4][64], values[4][64];
#define CHECK(t) do { ++checks; if (!(t)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#t); } } while (0)
int keyboard_press(Playback *p, WORD key) { (void)p; if (key == VK_RETURN) ++enters; return 1; }
int keyboard_shortcut(Playback *p, const WORD *keys, unsigned count) {
    (void)p; (void)keys; (void)count; return 1;
}
int keyboard_type(Playback *p, const wchar_t *text) {
    (void)p; if (typed >= 2) return 0;
    swprintf(texts[typed++],64,L"%ls",text); return 1;
}
int presentation_rest(Playback *p, const char *step) { (void)p; (void)step; return 1; }
void playback_log(Playback *p, const char *format, ...) { (void)p; (void)format; }
int ui_wait_vscode_input(Playback *p, const wchar_t *name, const wchar_t *value, DWORD timeout) {
    unsigned i = input_checks++;
    (void)p; (void)timeout;
    if (i >= 4) return 0;
    swprintf(names[i],64,L"%ls",name); swprintf(values[i],64,L"%ls",value);
    return input_checks != fail_input;
}
int ui_wait_control_rect(Playback *p, int type, const wchar_t *name, int contains, RECT *rect, DWORD timeout) {
    (void)p; (void)type; (void)name; (void)contains; (void)rect; (void)timeout;
    ++status_checks; return status_ready;
}
static void reset(void) {
    input_checks = fail_input = typed = enters = status_checks = 0; status_ready = 1;
    memset(texts,0,sizeof(texts)); memset(names,0,sizeof(names)); memset(values,0,sizeof(values));
}
int main(void) {
    Playback p = {0}; unsigned i;
    reset(); CHECK(vscode_select_plain_text(&p));
    CHECK(typed == 2 && enters == 2 && input_checks == 4 && status_checks == 1);
    CHECK(!wcscmp(texts[0],L"Change Language Mode") && !wcscmp(texts[1],L"Plain Text"));
    CHECK(!wcscmp(names[0],L"Type the name of a command to run.") && !wcscmp(values[0],L">"));
    CHECK(!wcscmp(values[1],L">Change Language Mode"));
    CHECK(!wcscmp(names[2],L"Select Language Mode") && !wcscmp(values[2],L"") && !wcscmp(values[3],L"Plain Text"));
    for (i = 1; i <= 4; ++i) {
        reset(); fail_input = i;
        CHECK(!vscode_select_plain_text(&p));
        CHECK(input_checks == i && status_checks == 0);
        CHECK(typed == (i == 1 ? 0u : i == 4 ? 2u : 1u));
        CHECK(enters == (i <= 2 ? 0u : 1u));
    }
    reset(); status_ready = 0; CHECK(!vscode_select_plain_text(&p));
    reset(); CHECK(vscode_select_lf(&p));
    CHECK(typed == 1 && enters == 2 && input_checks == 3 && status_checks == 1);
    CHECK(!wcscmp(texts[0],L"Change End of Line Sequence"));
    CHECK(!wcscmp(values[1],L">Change End of Line Sequence"));
    CHECK(!wcscmp(names[2],L"Select End of Line Sequence") && !wcscmp(values[2],L""));
    for (i = 1; i <= 3; ++i) {
        reset(); fail_input = i;
        CHECK(!vscode_select_lf(&p));
        CHECK(input_checks == i && status_checks == 0);
        CHECK(typed == (i == 1 ? 0u : 1u));
        CHECK(enters == (i <= 2 ? 0u : 1u));
    }
    reset(); status_ready = 0; CHECK(!vscode_select_lf(&p));
    printf("Guarded VS Code setup: %d checks, %d failures; no desktop input\n",checks,failures);
    return failures ? 1 : 0;
}
