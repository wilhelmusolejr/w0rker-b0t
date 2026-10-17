#include "script_engine.h"
#include "mouse.h"
#include "coding_action.h"
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include <math.h>

static int failures, checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)
static void parse_test(const wchar_t *source, int valid, unsigned line) {
    Script s; char error[256];
    int ok = script_parse(source, L".", &s, error, sizeof(error));
    CHECK(ok == valid);
    if (!ok && line) { char prefix[40]; snprintf(prefix, sizeof(prefix), "line %u:", line); CHECK(!strncmp(error, prefix, strlen(prefix))); }
    script_free(&s);
}
int main(void) {
    Script s; char error[256]; size_t i;
    const wchar_t *invalid[] = {
        L"WAIT -1\nEND", L"WAIT 3600001\nEND", L"WAIT 9999999999999999999\nEND",
        L"MOVE 1 2 -1\nEND", L"MOVE 1 2 3 WIGGLE\nEND", L"DRAG 1 2 3 4\nEND",
        L"CLICK OTHER\nEND", L"DOUBLE_CLICK x\nEND", L"RIGHT_CLICK LEFT\nEND",
        L"TYPE unquoted\nEND", L"TYPE \"unterminated\nEND", L"TYPE \"bad\\q\"\nEND",
        L"TYPE \"a\"b\nEND", L"TYPE \"trailing\\\nEND", L"\"WAIT\" 1\nEND",
        L"KEY F12\nEND", L"KEY F8\nEND", L"KEY F9\nEND", L"KEY UNKNOWN\nEND",
        L"SHORTCUT CTRL CTRL A\nEND", L"SHORTCUT A B\nEND", L"SHORTCUT CTRL\nEND",
        L"SCROLL 10001\nEND", L"COORDS INVALID\nEND", L"TIMING 1 -1\nEND",
        L"SHELL \"cmd.exe\"\nEND", L"PAUSE 1\nEND", L"ACTIVATE_WINDOW \"\"\nEND",
        L"ACTIVATE_WINDOW Excel\nEND", L"TYPEFILE \"\"\nEND",
        L"CODE_REFERENCE\nEND", L"CODE_REFERENCE \"relative\" \"anything.py\"\nEND",
        L"CODE_REFERENCE \"C:/demo\"\nEND", L"CODE_REFERENCE C:/demo \"sample.py\"\nEND",
        L"CODE_REFERENCE \"C:/demo\" \"\"\nEND",
        L"CODE_REFERENCE \"C:/demo\" \"__missing_reference_519864.py\"\nEND",
        L"TYPING_PACE 0 100 0 100 42\nEND",
        L"TYPING_PACE 100 50 0 100 42\nEND",
        L"TYPING_PACE 80 200 1800 600 42\nEND",
        L"TYPING_PACE 80 200 600 1800 0\nEND",
        L"TYPING_PACE 80 200 600 1800 2147483648\nEND",
        L"TYPING_PACE 80 200 600 1800\nEND",
        L"TYPING_BREAKS 0 100 1000 2000\nEND",
        L"TYPING_BREAKS 100 50 1000 2000\nEND",
        L"TYPING_BREAKS 100 200 2000 1000\nEND",
        L"TYPING_BREAKS 100 200 0 1000\nEND",
        L"TYPING_BREAKS 100 200 1000 60001\nEND",
        L"TYPING_BREAKS 100 200 1000\nEND",
        L"BROWSER_SCROLL \"javascript:alert(1)\" 20000 42\nEND",
        L"BROWSER_SCROLL \"file:///C:/page.html\" 20000 42\nEND",
        L"BROWSER_SCROLL https://example.com 20000 42\nEND",
        L"BROWSER_SCROLL \"https://\" 20000 42\nEND",
        L"BROWSER_SCROLL \"https://example.com\" 0 42\nEND",
        L"BROWSER_SCROLL \"https://example.com\" 300001 42\nEND",
        L"BROWSER_SCROLL \"https://example.com\" 20000 0\nEND",
        L"BROWSER_SCROLL \"https://example.com\" 20000\nEND",
        L"BROWSER_BREAK 0 \"https://example.com\" 20000 42\nEND",
        L"BROWSER_BREAK 30 \"https://example.com\" 20000 42\nEND",
        L"BROWSER_BREAK 40 \"https://example.com\" 20000\nEND",
        L"BROWSER_BREAK 40 \"https://example.com\" 20000 -1\nEND",
        L"BROWSER_BREAK 40 \"https://example.com\" 999 42\nEND",
        L"BROWSER_BREAK 40 \"javascript:alert(1)\" 20000 42\nEND"
    };
    parse_test(L"ACTION_PACE 900 2200 650 1400 AUTO\nTYPING_PACE 80 200 600 1800 AUTO\nACTION_PACE OFF\nEND", 1, 0);
    parse_test(L"ACTION_PACE 2200 900 650 1400 42\nEND", 0, 1);
    parse_test(L"ACTION_PACE 900 2200 1400 650 42\nEND", 0, 1);
    parse_test(L"ACTION_PACE 900 2200 0 1400 42\nEND", 0, 1);
    parse_test(L"ACTION_PACE 900 2200 650 1400 0\nEND", 0, 1);
    parse_test(L"ACTION_PACE 900 2200 650 1400 AUTO extra\nEND", 0, 1);
    parse_test(L"# comment\r\n\nWAIT 0 # another\nEND\n# tail", 1, 0);
    parse_test(L"WAIT 1", 0, 1);
    parse_test(L"END\nWAIT 1", 0, 2);
    parse_test(L"\n# comment\nNOPE\nEND", 0, 3);
    for (i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) parse_test(invalid[i], 0, 1);
    CHECK(script_parse(L"TYPE \"quote: \\\" slash: \\\\ tab:\\t newline:\\n # text\"\nEND", L".", &s, error, sizeof(error)));
    if (s.count) CHECK(!wcscmp(s.commands[0].text, L"quote: \" slash: \\ tab:\t newline:\n # text"));
    script_free(&s);
    CHECK(script_parse(L"BROWSER_SCROLL \"https://example.com\" 20000 42\n"
        L"BROWSER_BREAK 40 \"https://example.com\" 20000 42\nBROWSER_BREAK OFF\nEND",
        L".", &s, error, sizeof(error)));
    if (s.count == 4) {
        CHECK(s.commands[0].kind == CMD_BROWSER_SCROLL && s.commands[0].n[1] == 42);
        CHECK(s.commands[1].kind == CMD_BROWSER_BREAK && s.commands[1].n[0] == 40);
        CHECK(!wcscmp(s.commands[1].text, L"https://example.com"));
        CHECK(s.commands[2].n[0] == 0 && s.commands[2].text == NULL);
    } else CHECK(0);
    script_free(&s);
    CHECK(script_parse(L"TYPE \"\"\nEND", L".", &s, error, sizeof(error)));
    if (s.count) CHECK(s.commands[0].text[0] == 0);
    script_free(&s);
    parse_test(L"ACTIVATE_WINDOW \"Test\"\nCOORDS CLIENT\nMOVE -10 20 400 CURVE\n"
        L"CLICK LEFT\nCLICK MIDDLE\nRIGHT_CLICK\nDOUBLE_CLICK\nDRAG 1 2 3 4 50 STRAIGHT\n"
        L"SCROLL -3\nTIMING 40 300\nKEY ENTER\nSHORTCUT CTRL SHIFT S\nPAUSE\nEND", 1, 0);
    CHECK(script_key(L"F24") == VK_F24);
    CHECK(script_key(L"F12") == 0);
    CHECK(script_key(L"a") == 0);
    CHECK(script_parse(L"TYPING_PACE 80 200 600 1800 42\nTYPING_BREAKS 160 320 4000 10000\n"
        L"TYPING_BREAKS OFF\nTYPING_PACE OFF\nEND", L".", &s, error, sizeof(error)));
    if (s.count == 5) {
        CHECK(s.commands[0].kind == CMD_TYPING_PACE && s.commands[0].n[4] == 42);
        CHECK(s.commands[1].kind == CMD_TYPING_BREAKS && s.commands[1].n[3] == 10000);
        CHECK(s.commands[2].n[0] == 0 && s.commands[3].n[0] == -1);
    } else CHECK(0);
    script_free(&s);
    /* Round-trip strict UTF-8, BOM, surrogate pair, malformed input, and NUL. */
    {
        wchar_t temporary[MAX_PATH], path[MAX_PATH], *text = NULL;
        FILE *file;
        CHECK(GetTempPathW(MAX_PATH, temporary) > 0);
        CHECK(GetTempFileNameW(temporary, L"tut", 0, path) != 0);
        file = _wfopen(path, L"wb"); CHECK(file != NULL);
        if (file) { const unsigned char data[] = {0xef,0xbb,0xbf,0x41,0xc3,0xa9,0xf0,0x9f,0x98,0x80};
            fwrite(data,1,sizeof(data),file); fclose(file); }
        CHECK(read_utf8(path, &text, error, sizeof(error)));
        if (text) CHECK(text[0] == L'A' && text[1] == 0xe9 && text[2] == 0xd83d && text[3] == 0xde00 && text[4] == 0);
        free(text);
        file = _wfopen(path, L"wb"); if (file) { fputc(0xc0,file); fclose(file); }
        CHECK(!read_utf8(path, &text, error, sizeof(error)));
        file = _wfopen(path, L"wb"); if (file) { fputc(0,file); fclose(file); }
        CHECK(!read_utf8(path, &text, error, sizeof(error)));
        file = _wfopen(path, L"wb"); if (file) fclose(file);
        CHECK(read_utf8(path, &text, error, sizeof(error)));
        if (text) CHECK(text[0] == 0);
        free(text);
        {
            wchar_t source[2 * MAX_PATH + 100], forward[MAX_PATH];
            size_t j;
            wcscpy(forward, path);
            for (j = 0; forward[j]; ++j) if (forward[j] == L'\\') forward[j] = L'/';
            swprintf(source, 2 * MAX_PATH + 100,
                L"CODE_REFERENCE \"C:/vehicle demo\" \"%ls\"\nEND", forward);
            CHECK(script_parse(source, L".", &s, error, sizeof(error)));
            if (s.count) {
                CHECK(s.commands[0].kind == CMD_CODE_REFERENCE);
                CHECK(!_wcsicmp(s.commands[0].path, L"C:\\vehicle demo"));
                CHECK(s.commands[0].text != NULL && !s.commands[0].text[0]);
            }
            script_free(&s);
            swprintf(source, 2 * MAX_PATH + 100,
                L"DESKTOP_CODE_REFERENCE \"C:/vehicle demo\" \"%ls\"\nEND", forward);
            CHECK(script_parse(source, L".", &s, error, sizeof(error)));
            if (s.count) CHECK(s.commands[0].kind == CMD_DESKTOP_CODE_REFERENCE);
            script_free(&s);
        }
        DeleteFileW(path);
    }
    CHECK(coding_same_text(L"a\r\nb\n", L"a\nb\r\n"));
    CHECK(coding_same_text(L"a\rb", L"a\nb"));
    CHECK(coding_same_text(L"", L""));
    CHECK(!coding_same_text(L"    indent\n", L"  indent\n"));
    CHECK(!coding_same_text(L"text\n", L"text"));
    CHECK(!coding_same_text(L"a", L"b"));
    /* The old two-setting profile allowed pair completion to corrupt a saved prefix. */
    CHECK(!coding_workspace_pacing_ready(L"{\"[plaintext]\":{\"editor.autoIndent\":\"none\",\"editor.acceptSuggestionOnEnter\":\"off\"}}"));
    CHECK(coding_workspace_pacing_ready(L"{\"[plaintext]\":{\"editor.autoIndent\":\"none\",\"editor.acceptSuggestionOnEnter\":\"off\","
        L"\"editor.autoClosingBrackets\":\"never\",\"editor.autoClosingQuotes\":\"never\",\"editor.autoSurround\":\"never\"}}"));
    CHECK(!coding_workspace_pacing_ready(L"{\"[plaintext]\":{\"editor.autoIndent\":\"none\",\"editor.acceptSuggestionOnEnter\":\"off\","
        L"\"editor.autoClosingBrackets\":\"always\",\"editor.autoClosingQuotes\":\"never\",\"editor.autoSurround\":\"never\"}}"));
    CHECK(!coding_workspace_pacing_ready(L"{\"[plaintext]\":{\"editor.autoIndent\":\"none\",\"editor.acceptSuggestionOnEnter\":\"off\","
        L"\"editor.autoClosingBrackets\":\"never\",\"editor.autoClosingQuotes\":\"languageDefined\",\"editor.autoSurround\":\"never\"}}"));
    CHECK(!coding_workspace_pacing_ready(L"{\"editor.autoClosingBrackets\":\"never\"}"));
    CHECK(!coding_workspace_pacing_ready(L"{\"[plaintext]\":{}}"));
    CHECK(!coding_workspace_pacing_ready(L"invalid json"));
    CHECK(fabs(mouse_ease(0)) < 1e-12);
    CHECK(fabs(mouse_ease(1)-1) < 1e-12);
    CHECK(fabs(mouse_ease(0.5)-0.5) < 1e-12);
    for (i = 1; i <= 100; ++i) CHECK(mouse_ease(i/100.0) > mouse_ease((i-1)/100.0));
    printf("%d checks, %d failures (no desktop input generated)\n", checks, failures);
    return failures ? 1 : 0;
}
