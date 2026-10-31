#include "script_engine.h"
#include "browser_scroll_plan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <errno.h>
#include <limits.h>

#define MAX_FILE_BYTES (16 * 1024 * 1024)
#define MAX_COMMANDS 100000
#define MAX_ARGS 10

static int fail(char *error, size_t cap, unsigned line, const char *message) {
    snprintf(error, cap, "line %u: %s", line, message);
    return 0;
}

int read_utf8(const wchar_t *path, wchar_t **text, char *error, size_t cap) {
    FILE *file = _wfopen(path, L"rb");
    char *bytes;
    long size;
    int offset, count;
    *text = NULL;
    if (!file) { snprintf(error, cap, "cannot open UTF-8 file (errno %d)", errno); return 0; }
    if (fseek(file, 0, SEEK_END) || (size = ftell(file)) < 0 || size > MAX_FILE_BYTES ||
        fseek(file, 0, SEEK_SET)) {
        fclose(file); snprintf(error, cap, "file exceeds 16 MiB or cannot be read"); return 0;
    }
    bytes = malloc((size_t)size + 1);
    if (!bytes) { fclose(file); return fail(error, cap, 0, "out of memory"); }
    if (fread(bytes, 1, (size_t)size, file) != (size_t)size) {
        free(bytes); fclose(file); return fail(error, cap, 0, "file read failed");
    }
    fclose(file);
    bytes[size] = 0;
    offset = size >= 3 && (unsigned char)bytes[0] == 0xef &&
        (unsigned char)bytes[1] == 0xbb && (unsigned char)bytes[2] == 0xbf ? 3 : 0;
    if (memchr(bytes + offset, 0, (size_t)(size - offset))) {
        free(bytes); return fail(error, cap, 0, "embedded NUL byte");
    }
    count = size == offset ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        bytes + offset, (int)size - offset, NULL, 0);
    if (size != offset && !count) {
        free(bytes); return fail(error, cap, 0, "invalid UTF-8");
    }
    *text = calloc((size_t)count + 1, sizeof(wchar_t));
    if (!*text) { free(bytes); return fail(error, cap, 0, "out of memory"); }
    if (count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes + offset,
                                  (int)size - offset, *text, count);
    free(bytes);
    return 1;
}

WORD script_key(const wchar_t *name) {
    static const struct { const wchar_t *name; WORD code; } names[] = {
        {L"CTRL", VK_CONTROL}, {L"ALT", VK_MENU}, {L"SHIFT", VK_SHIFT},
        {L"WIN", VK_LWIN}, {L"ENTER", VK_RETURN}, {L"TAB", VK_TAB},
        {L"ESC", VK_ESCAPE}, {L"ESCAPE", VK_ESCAPE}, {L"BACKSPACE", VK_BACK},
        {L"DELETE", VK_DELETE}, {L"LEFT", VK_LEFT}, {L"RIGHT", VK_RIGHT},
        {L"UP", VK_UP}, {L"DOWN", VK_DOWN}, {L"HOME", VK_HOME}, {L"END", VK_END},
        {L"PAGEUP", VK_PRIOR}, {L"PAGEDOWN", VK_NEXT}, {L"SPACE", VK_SPACE},
        {L"INSERT", VK_INSERT}
    };
    size_t i;
    if (wcslen(name) == 1 && ((name[0] >= L'A' && name[0] <= L'Z') ||
                             (name[0] >= L'0' && name[0] <= L'9'))) return (WORD)name[0];
    for (i = 0; i < sizeof(names)/sizeof(names[0]); ++i)
        if (!wcscmp(name, names[i].name)) return names[i].code;
    if (name[0] == L'F' && name[1]) {
        wchar_t *end; long n = wcstol(name + 1, &end, 10);
        if (!*end && n >= 1 && n <= 24 && n != 8 && n != 9 && n != 12)
            return (WORD)(VK_F1 + n - 1);
    }
    return 0;
}

const char *command_name(CommandKind k) {
    static const char *names[] = {"ACTIVATE_WINDOW", "MOVE", "CLICK", "DOUBLE_CLICK",
        "RIGHT_CLICK", "DRAG", "SCROLL", "TYPE", "TYPEFILE", "KEY", "SHORTCUT",
        "WAIT", "PAUSE", "END", "COORDS", "TIMING", "CODE_REFERENCE",
        "TYPING_PACE", "TYPING_BREAKS", "BROWSER_SCROLL", "BROWSER_BREAK",
        "ACTION_PACE", "DESKTOP_CODE_REFERENCE", "REFERENCE_BATCH", "BROWSE_SESSION", "CODE_PROJECT", "SIDE_APP", "LOGGY3", "WORK_SESSION", "FLOW"};
    return names[k];
}

/* Tokenize in place; quoted strings support only documented escapes. */
static int tokenize(wchar_t *line, wchar_t **args, int *quoted) {
    wchar_t *r = line, *w = line;
    int count = 0;
    while (*r) {
        int quote;
        while (iswspace(*r)) ++r;
        if (!*r || *r == L'#') break;
        if (count == MAX_ARGS) return -1;
        quote = *r == L'"';
        quoted[count] = quote;
        if (quote) ++r;
        args[count++] = w;
        while (*r && (quote ? *r != L'"' : !iswspace(*r) && *r != L'#')) {
            if (*r == L'"') return -1;
            if (quote && *r == L'\\') {
                ++r;
                switch (*r) {
                    case L'n': *w++ = L'\n'; break;
                    case L'r': *w++ = L'\r'; break;
                    case L't': *w++ = L'\t'; break;
                    case L'\\': *w++ = L'\\'; break;
                    case L'"': *w++ = L'"'; break;
                    default: return -1;
                }
                ++r;
            } else *w++ = *r++;
        }
        if (quote) {
            if (*r != L'"') return -1;
            ++r;
            if (*r && !iswspace(*r) && *r != L'#') return -1;
        }
        { int comment = *r == L'#'; if (*r) ++r; *w++ = 0; if (comment) break; }
    }
    return count;
}

static int number(const wchar_t *text, int minimum, int maximum, int *out) {
    wchar_t *end;
    long n;
    if (!*text) return 0;
    errno = 0; n = wcstol(text, &end, 10);
    if (errno || *end || n < minimum || n > maximum) return 0;
    *out = (int)n;
    return 1;
}

void script_free(Script *s) {
    size_t i;
    for (i = 0; i < s->count; ++i) {
        free(s->commands[i].text);
        free(s->commands[i].path);
    }
    free(s->commands); s->commands = NULL; s->count = 0;
}

int script_parse(const wchar_t *source, const wchar_t *directory, Script *s,
                 char *error, size_t cap) {
    wchar_t *copy = _wcsdup(source), *line, *next;
    unsigned lineno = 0;
    int ended = 0;
    const char *reason = "invalid arguments";
    memset(s, 0, sizeof(*s));
    if (!copy) return fail(error, cap, 0, "out of memory");
    line = copy;
    while (line) {
        wchar_t *args[MAX_ARGS]; int quoted[MAX_ARGS], argc, i;
        Command c = {0};
        Command *grown;
        next = wcschr(line, L'\n'); if (next) *next++ = 0;
        c.line = ++lineno;
        argc = tokenize(line, args, quoted);
        if (argc < 0) { reason = "malformed quotes, escape, or too many arguments"; goto bad; }
        if (!argc) { line = next; continue; }
        if (ended) { reason = "command after END"; goto bad; }
        if (quoted[0]) goto bad;
        if (!wcscmp(args[0], L"ACTIVATE_WINDOW")) {
            c.kind = CMD_ACTIVATE;
            if (argc != 2 || !quoted[1] || !*args[1]) goto bad;
            c.text = _wcsdup(args[1]);
        } else if (!wcscmp(args[0], L"CODE_REFERENCE") || !wcscmp(args[0], L"DESKTOP_CODE_REFERENCE")) {
            wchar_t destination[32768], source_path[32768];
            DWORD length;
            char detail[256];
            c.kind = !wcscmp(args[0], L"CODE_REFERENCE") ? CMD_CODE_REFERENCE : CMD_DESKTOP_CODE_REFERENCE;
            if (argc != 3 || !quoted[1] || !quoted[2] || !*args[1] || !*args[2]) goto bad;
            /* The desktop destination must be an explicit absolute path. */
            if (wcslen(args[1]) < 3 || args[1][1] != L':' ||
                (args[1][2] != L'\\' && args[1][2] != L'/')) goto bad;
            length = GetFullPathNameW(args[1], 32768, destination, NULL);
            if (!length || length >= 32768 || wcschr(destination, L'"') ||
                wcschr(destination, L'%') || wcschr(destination, L'\n') || wcschr(destination, L'\r')) goto bad;
            if (args[2][0] == L'\\' || args[2][0] == L'/' ||
                (wcslen(args[2]) > 1 && args[2][1] == L':')) {
                if (wcslen(args[2]) >= 32768) goto bad;
                wcscpy(source_path, args[2]);
            } else {
                if (wcslen(directory) + wcslen(args[2]) + 2 >= 32768) goto bad;
                swprintf(source_path, 32768, L"%ls\\%ls", directory, args[2]);
            }
            if (!read_utf8(source_path, &c.text, detail, sizeof(detail))) {
                fail(error, cap, lineno, detail); goto cleanup;
            }
            c.path = _wcsdup(destination);
            if (!c.path) { free(c.text); reason = "out of memory"; goto bad; }
        } else if (!wcscmp(args[0], L"TYPE") || !wcscmp(args[0], L"TYPEFILE")) {
            c.kind = !wcscmp(args[0], L"TYPE") ? CMD_TYPE : CMD_TYPEFILE;
            if (argc != 2 || !quoted[1]) goto bad;
            if (c.kind == CMD_TYPE) c.text = _wcsdup(args[1]);
            else {
                wchar_t path[32768]; char detail[256];
                if (!*args[1]) goto bad;
                if (args[1][0] == L'\\' || args[1][0] == L'/' ||
                    (wcslen(args[1]) > 1 && args[1][1] == L':')) {
                    if (wcslen(args[1]) >= 32768) goto bad;
                    wcscpy(path, args[1]);
                } else {
                    if (wcslen(directory) + wcslen(args[1]) + 2 >= 32768) goto bad;
                    swprintf(path, 32768, L"%ls\\%ls", directory, args[1]);
                }
                if (!read_utf8(path, &c.text, detail, sizeof(detail))) {
                    fail(error, cap, lineno, detail); goto cleanup;
                }
            }
        } else if (!wcscmp(args[0], L"MOVE") || !wcscmp(args[0], L"DRAG")) {
            int coordinates;
            c.kind = !wcscmp(args[0], L"MOVE") ? CMD_MOVE : CMD_DRAG;
            coordinates = c.kind == CMD_MOVE ? 2 : 4;
            if (argc != coordinates + 2 && argc != coordinates + 3) goto bad;
            for (i = 0; i < coordinates; ++i)
                if (quoted[i+1] || !number(args[i+1], -100000, 100000, &c.n[i])) goto bad;
            if (!number(args[coordinates+1], 0, 3600000, &c.n[coordinates])) goto bad;
            if (argc == coordinates + 3) {
                if (!wcscmp(args[coordinates+2], L"CURVE")) c.n[coordinates+1] = 1;
                else if (wcscmp(args[coordinates+2], L"STRAIGHT")) goto bad;
            }
        } else if (!wcscmp(args[0], L"CLICK")) {
            c.kind = CMD_CLICK;
            if (argc != 2) goto bad;
            if (!wcscmp(args[1], L"LEFT")) c.n[0] = 0;
            else if (!wcscmp(args[1], L"RIGHT")) c.n[0] = 1;
            else if (!wcscmp(args[1], L"MIDDLE")) c.n[0] = 2;
            else goto bad;
        } else if (!wcscmp(args[0], L"DOUBLE_CLICK") || !wcscmp(args[0], L"RIGHT_CLICK") ||
                   !wcscmp(args[0], L"PAUSE") || !wcscmp(args[0], L"END")) {
            if (argc != 1) goto bad;
            c.kind = !wcscmp(args[0], L"DOUBLE_CLICK") ? CMD_DOUBLE :
                !wcscmp(args[0], L"RIGHT_CLICK") ? CMD_RIGHT :
                !wcscmp(args[0], L"PAUSE") ? CMD_PAUSE : CMD_END;
            if (c.kind == CMD_END) ended = 1;
        } else if (!wcscmp(args[0], L"WAIT") || !wcscmp(args[0], L"SCROLL")) {
            c.kind = !wcscmp(args[0], L"WAIT") ? CMD_WAIT : CMD_SCROLL;
            if (argc != 2 || !number(args[1], c.kind == CMD_WAIT ? 0 : -10000,
                                    c.kind == CMD_WAIT ? 3600000 : 10000, &c.n[0])) goto bad;
        } else if (!wcscmp(args[0], L"KEY") || !wcscmp(args[0], L"SHORTCUT")) {
            c.kind = !wcscmp(args[0], L"KEY") ? CMD_KEY : CMD_SHORTCUT;
            if (argc < 2 || argc > 9 || (c.kind == CMD_KEY && argc != 2)) goto bad;
            for (i = 1; i < argc; ++i) {
                int j;
                c.keys[i-1] = script_key(args[i]);
                if (!c.keys[i-1]) goto bad;
                for (j = 1; j < i; ++j) if (c.keys[j-1] == c.keys[i-1]) goto bad;
                if (c.kind == CMD_SHORTCUT && i < argc - 1 &&
                    c.keys[i-1] != VK_CONTROL && c.keys[i-1] != VK_SHIFT &&
                    c.keys[i-1] != VK_MENU && c.keys[i-1] != VK_LWIN) goto bad;
            }
            if (c.kind == CMD_SHORTCUT && argc < 3) goto bad;
            c.key_count = (unsigned)argc - 1;
        } else if (!wcscmp(args[0], L"COORDS")) {
            c.kind = CMD_COORDS;
            if (argc != 2) goto bad;
            if (!wcscmp(args[1], L"CLIENT")) c.n[0] = 1;
            else if (wcscmp(args[1], L"SCREEN")) goto bad;
        } else if (!wcscmp(args[0], L"TIMING")) {
            c.kind = CMD_TIMING;
            if (argc != 3 || !number(args[1], 0, 10000, &c.n[0]) ||
                !number(args[2], 0, 60000, &c.n[1])) goto bad;
        } else if (!wcscmp(args[0], L"TYPING_PACE")) {
            c.kind = CMD_TYPING_PACE;
            if (argc == 2 && !wcscmp(args[1], L"OFF")) c.n[0] = -1;
            else {
                if (argc != 6 || !number(args[1], 1, 10000, &c.n[0]) ||
                    !number(args[2], c.n[0], 10000, &c.n[1]) ||
                    !number(args[3], 0, 60000, &c.n[2]) ||
                    !number(args[4], c.n[2], 60000, &c.n[3]) ||
                    (wcscmp(args[5], L"AUTO") && !number(args[5], 1, INT_MAX, &c.n[4]))) goto bad;
            }
        } else if (!wcscmp(args[0], L"ACTION_PACE")) {
            c.kind = CMD_ACTION_PACE;
            if (argc == 2 && !wcscmp(args[1], L"OFF")) c.n[0] = -1;
            else if (argc != 6 || !number(args[1], 0, 60000, &c.n[0]) ||
                !number(args[2], c.n[0], 60000, &c.n[1]) ||
                !number(args[3], 100, 10000, &c.n[2]) ||
                !number(args[4], c.n[2], 10000, &c.n[3]) ||
                (wcscmp(args[5], L"AUTO") && !number(args[5], 1, INT_MAX, &c.n[4]))) goto bad;
        } else if (!wcscmp(args[0], L"TYPING_BREAKS")) {
            c.kind = CMD_TYPING_BREAKS;
            if (argc == 2 && !wcscmp(args[1], L"OFF")) c.n[0] = 0;
            else {
                if (argc != 5 || !number(args[1], 1, 100000, &c.n[0]) ||
                    !number(args[2], c.n[0], 100000, &c.n[1]) ||
                    !number(args[3], 1, 60000, &c.n[2]) ||
                    !number(args[4], c.n[2], 60000, &c.n[3])) goto bad;
            }
        } else if (!wcscmp(args[0], L"BROWSER_SCROLL")) {
            c.kind = CMD_BROWSER_SCROLL;
            if (argc != 4 || !quoted[1] || !browser_url_valid(args[1]) ||
                !number(args[2], 1000, 300000, &c.n[0]) ||
                !number(args[3], 1, INT_MAX, &c.n[1])) goto bad;
            c.text = _wcsdup(args[1]);
            if (!c.text) { reason = "out of memory"; goto bad; }
        } else if (!wcscmp(args[0], L"BROWSER_BREAK")) {
            c.kind = CMD_BROWSER_BREAK;
            if (argc == 2 && !wcscmp(args[1], L"OFF")) c.n[0] = 0;
            else {
                if (argc != 5 || !number(args[1], 20, 100000, &c.n[0]) || c.n[0] % 20 ||
                    !quoted[2] || !browser_url_valid(args[2]) ||
                    !number(args[3], 1000, 300000, &c.n[1]) ||
                    !number(args[4], 1, INT_MAX, &c.n[2])) goto bad;
                c.text = _wcsdup(args[2]);
                if (!c.text) { reason = "out of memory"; goto bad; }
            }
        } else { reason = "unknown command"; goto bad; }
        if ((c.kind == CMD_TYPE || c.kind == CMD_ACTIVATE) && !c.text) {
            reason = "out of memory"; goto bad;
        }
        if (s->count >= MAX_COMMANDS) { free(c.text); free(c.path); reason = "too many commands"; goto bad; }
        grown = realloc(s->commands, (s->count + 1) * sizeof(Command));
        if (!grown) { free(c.text); free(c.path); reason = "out of memory"; goto bad; }
        s->commands = grown; s->commands[s->count++] = c;
        line = next;
    }
    free(copy);
    if (!ended) { script_free(s); return fail(error, cap, lineno, "missing END"); }
    return 1;
bad:
    fail(error, cap, lineno, reason);
cleanup:
    free(copy); script_free(s); return 0;
}

int script_load(const wchar_t *path, Script *s, char *error, size_t cap) {
    wchar_t absolute[32768], *filepart, *source;
    DWORD length = GetFullPathNameW(path, 32768, absolute, &filepart);
    int result;
    memset(s, 0, sizeof(*s));
    if (!length || length >= 32768 || !filepart) return fail(error, cap, 0, "invalid script path");
    if (!read_utf8(absolute, &source, error, cap)) return 0;
    *filepart = 0;
    result = script_parse(source, absolute, s, error, cap);
    free(source); return result;
}
