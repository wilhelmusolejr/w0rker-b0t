#include "keyboard.h"

static DWORD profile_typing_delay(const Playback *p, DWORD delay_ms) {
    unsigned base = p->typing_pace.enabled ?
        (p->typing_pace.char_min_ms + p->typing_pace.char_max_ms) / 2 : p->char_ms;
    return typing_speed_delay(delay_ms, base, p->typing_speed);
}

static DWORD extended(WORD key) {
    switch (key) {
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_HOME:
        case VK_END: case VK_PRIOR: case VK_NEXT: case VK_INSERT: case VK_DELETE:
        case VK_LWIN: return KEYEVENTF_EXTENDEDKEY;
        default: return 0;
    }
}
int keyboard_key(Playback *p, WORD key, int down) {
    INPUT input = {0};
    if (!playback_guard(p)) return 0;
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.wScan = (WORD)MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = extended(key) | (down ? 0 : KEYEVENTF_KEYUP);
    if (!playback_send(p, &input, 1)) return 0;
    p->held[key] = (unsigned char)down;
    return 1;
}
int keyboard_press(Playback *p, WORD key) {
    INPUT input[2] = {{0}};
    if (!playback_guard(p)) return 0;
    input[0].type = input[1].type = INPUT_KEYBOARD;
    input[0].ki.wVk = input[1].ki.wVk = key;
    input[0].ki.wScan = input[1].ki.wScan = (WORD)MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    input[0].ki.dwFlags = extended(key);
    input[1].ki.dwFlags = extended(key) | KEYEVENTF_KEYUP;
    if (!p->action_pace.enabled) return playback_send(p, input, 2);
    if (!playback_send(p, input, 1)) return 0;
    p->held[key] = 1;
    /* A press may change focus. Always finish its release, even in the new window. */
    if (!playback_wait(p, action_pace_pick(&p->action_pace, 70, 140)) ||
        !playback_send(p, input + 1, 1)) return 0;
    p->held[key] = 0;
    return playback_wait(p, action_pace_pick(&p->action_pace, 140, 320));
}
int keyboard_shortcut(Playback *p, const WORD *keys, unsigned count) {
    INPUT input[16] = {{0}};
    unsigned i;
    if (!count || count > 8 || !playback_guard(p)) return 0;
    /* A shortcut can switch windows. Its releases must still complete after that switch. */
    for (i = 0; i < count; ++i) {
        input[i].type = input[2*count-1-i].type = INPUT_KEYBOARD;
        input[i].ki.wVk = input[2*count-1-i].ki.wVk = keys[i];
        input[i].ki.wScan = input[2*count-1-i].ki.wScan = (WORD)MapVirtualKeyW(keys[i], MAPVK_VK_TO_VSC);
        input[i].ki.dwFlags = extended(keys[i]);
        input[2*count-1-i].ki.dwFlags = extended(keys[i]) | KEYEVENTF_KEYUP;
    }
    if (!p->action_pace.enabled) return playback_send(p, input, count * 2);
    if (!playback_send(p, input, count)) return 0;
    for (i = 0; i < count; ++i) p->held[keys[i]] = 1;
    if (!playback_wait(p, action_pace_pick(&p->action_pace, 90, 180)) ||
        !playback_send(p, input + count, count)) return 0;
    for (i = 0; i < count; ++i) p->held[keys[i]] = 0;
    return playback_wait(p, action_pace_pick(&p->action_pace, 180, 400));
}
static int send_unicode(Playback *p, wchar_t ch) {
    INPUT input[2] = {{0}};
    input[0].type = input[1].type = INPUT_KEYBOARD;
    input[0].ki.wScan = input[1].ki.wScan = ch;
    input[0].ki.dwFlags = KEYEVENTF_UNICODE;
    input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    return playback_send(p, input, 2);
}
/* Start of the line holding `at` when only indentation precedes it there, else NULL. */
static const wchar_t *indent_only_line(const wchar_t *document, const wchar_t *at) {
    const wchar_t *s = at;
    while (s > document && s[-1] != L'\n') {
        --s;
        if (*s != L' ' && *s != L'\t') return NULL;
    }
    return s;
}
/* VS Code re-indents a closing bracket typed first on a line to match the opening bracket it finds
   ("electric characters"). editor.autoIndent does not turn this off, and in Plain Text a bracket
   inside a string can be matched, so the indentation may change. After such a bracket, put the
   source indentation back: Home stops before the bracket, Shift+Home selects whatever indentation
   VS Code left, and typing replaces it. A line with no indentation types and erases one space. */
static int restore_indent(Playback *p, const wchar_t *line, const wchar_t *bracket) {
    WORD select[] = {VK_SHIFT, VK_HOME};
    const wchar_t *c;
    if (!keyboard_press(p, VK_HOME) || !keyboard_shortcut(p, select, 2)) return 0;
    if (line == bracket) {
        if (!send_unicode(p, L' ') || !keyboard_press(p, VK_BACK)) return 0;
    } else for (c = line; c < bracket; ++c)
        if (!(*c == L'\t' ? keyboard_press(p, VK_TAB) : send_unicode(p, L' '))) return 0;
    return keyboard_press(p, VK_END);
}
/* Sometimes hit a neighbouring key first, notice it, and erase it. Only letters, so the editor
   never auto-closes or re-indents anything that Backspace would then have to undo. */
static int typo(Playback *p, wchar_t ch) {
    uint32_t value = p->typo_state ? p->typo_state : UINT32_C(0x2545f491);
    wchar_t wrong;
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    p->typo_state = value;
    if ((value >> 8) % 100 >= p->typo_percent || !(wrong = typing_typo_neighbor(ch, value >> 20))) return 1;
    ++p->typos;
    return send_unicode(p, wrong) && playback_wait(p, profile_typing_delay(p, 140 + (value >> 4) % 420)) &&
        keyboard_press(p, VK_BACK) && playback_wait(p, profile_typing_delay(p, 60 + (value >> 12) % 160));
}
static int type_text(Playback *p, const wchar_t *text, const wchar_t *document, DWORD budget_ms,
                     size_t *consumed) {
    size_t i;
    ULONGLONG start = GetTickCount64(), paused = p->paused_ms;
    if (consumed) *consumed = 0;
    for (i = 0; text[i]; ++i) {
        INPUT input[4] = {{0}};
        UINT count = 2, j;
        wchar_t ch = text[i];
        if (!playback_poll(p) || !playback_guard(p)) return 0;
        if (ch == L'\r') {
            if (text[i+1] == L'\n') continue;
            ch = L'\n';
        }
        if (ch == L'\n' || ch == L'\t') {
            if (!keyboard_press(p, ch == L'\n' ? VK_RETURN : VK_TAB)) return 0;
        } else {
            if (ch >= 0xd800 && ch <= 0xdbff && text[i+1] >= 0xdc00 && text[i+1] <= 0xdfff)
                count = 4;
            for (j = 0; j < count; ++j) {
                input[j].type = INPUT_KEYBOARD;
                input[j].ki.wScan = text[i + j/2];
                input[j].ki.dwFlags = KEYEVENTF_UNICODE | (j % 2 ? KEYEVENTF_KEYUP : 0);
            }
            if (count == 2 && p->typo_percent && !typo(p, ch)) return 0;
            if (!playback_send(p, input, count)) return 0;
            if (count == 4) ++i;
            else if (document && (ch == L'}' || ch == L']' || ch == L')')) {
                const wchar_t *line = indent_only_line(document, text + i);
                if (line && !restore_indent(p, line, text + i)) return 0;
            }
        }
        if (p->typing_pace.enabled) {
            TypingDelay delay = typing_pace_next(&p->typing_pace, ch);
            if (!playback_wait(p, profile_typing_delay(p, (DWORD)delay.delay_ms))) return 0;
            if (delay.break_ms) {
                playback_log(p, "presentation pause: %.1f seconds; F8 pause/resume, F12 stop",
                    delay.break_ms / 1000.0);
                if (!playback_wait(p, (DWORD)delay.break_ms)) return 0;
            }
        } else if (!playback_wait(p, profile_typing_delay(p, ch == L'\n' ? p->line_ms : p->char_ms))) return 0;
        if (consumed) *consumed = i+1;
        if (budget_ms && GetTickCount64()-start-(p->paused_ms-paused) >= budget_ms) return 1;
    }
    return 1;
}
int keyboard_type(Playback *p, const wchar_t *text) { return type_text(p,text,NULL,0,NULL); }
int keyboard_type_code(Playback *p, const wchar_t *text, const wchar_t *document) {
    return type_text(p,text,document,0,NULL);
}
int keyboard_type_slice(Playback *p, const wchar_t *text, const wchar_t *document, DWORD budget_ms,
                        size_t *consumed) {
    return type_text(p,text,document,budget_ms,consumed);
}
