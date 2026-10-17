#ifndef SCRIPT_ENGINE_H
#define SCRIPT_ENGINE_H
#include <windows.h>
#include <stddef.h>
typedef enum {
    CMD_ACTIVATE, CMD_MOVE, CMD_CLICK, CMD_DOUBLE, CMD_RIGHT, CMD_DRAG,
    CMD_SCROLL, CMD_TYPE, CMD_TYPEFILE, CMD_KEY, CMD_SHORTCUT, CMD_WAIT,
    CMD_PAUSE, CMD_END, CMD_COORDS, CMD_TIMING, CMD_CODE_REFERENCE,
    CMD_TYPING_PACE, CMD_TYPING_BREAKS, CMD_BROWSER_SCROLL, CMD_BROWSER_BREAK,
    CMD_ACTION_PACE, CMD_DESKTOP_CODE_REFERENCE, CMD_REFERENCE_BATCH
} CommandKind;
typedef struct {
    CommandKind kind;
    unsigned line;
    int n[6];
    WORD keys[8];
    unsigned key_count;
    wchar_t *text;
    wchar_t *path;
} Command;
typedef struct { Command *commands; size_t count; } Script;
int script_load(const wchar_t *path, Script *script, char *error, size_t capacity);
int script_parse(const wchar_t *source, const wchar_t *directory, Script *script,
                 char *error, size_t capacity);
void script_free(Script *script);
int read_utf8(const wchar_t *path, wchar_t **text, char *error, size_t capacity);
WORD script_key(const wchar_t *name);
const char *command_name(CommandKind kind);
#endif
