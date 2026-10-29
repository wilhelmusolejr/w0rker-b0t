#include "vscode_editor.h"
#include "keyboard.h"
#include "presentation.h"
#include "ui_targets.h"
#include <uiautomationclient.h>
#include <wchar.h>

static const wchar_t command_prompt[] = L"Type the name of a command to run.";
static int shortcut(Playback *p, WORD modifier, WORD key) {
    WORD keys[] = {modifier,key};
    return keyboard_shortcut(p,keys,2);
}
static int command(Playback *p, const wchar_t *text) {
    WORD palette[] = {VK_CONTROL,VK_SHIFT,L'P'};
    wchar_t query[128];
    swprintf(query,128,L">%ls",text);
    if (!keyboard_press(p,VK_ESCAPE) || !shortcut(p,VK_CONTROL,L'1') ||
        !keyboard_shortcut(p,palette,3) ||
        !ui_wait_vscode_input(p,command_prompt,L">",10000)) {
        playback_log(p,"command palette did not become ready; command text was not typed"); return 0;
    }
    return presentation_rest(p,"command palette verified") && keyboard_type(p,text) &&
        ui_wait_vscode_input(p,command_prompt,query,10000) && keyboard_press(p,VK_RETURN);
}
int vscode_select_plain_text(Playback *p) {
    RECT status;
    if (!command(p,L"Change Language Mode") ||
        !ui_wait_vscode_input(p,L"Select Language Mode",L"",10000) ||
        !presentation_rest(p,"language picker verified") || !keyboard_type(p,L"Plain Text") ||
        !ui_wait_vscode_input(p,L"Select Language Mode",L"Plain Text",10000) ||
        !keyboard_press(p,VK_RETURN) || !keyboard_press(p,VK_ESCAPE) ||
        !shortcut(p,VK_CONTROL,L'1') ||
        !ui_wait_control_rect(p,UIA_ButtonControlTypeId,L"Plain Text",0,&status,10000)) return 0;
    playback_log(p,"verified editor language: Plain Text"); return 1;
}
int vscode_revert_file(Playback *p) {
    if (!command(p,L"File: Revert File") || !presentation_rest(p,"unsaved changes reverted")) return 0;
    playback_log(p,"editor reverted to the saved checkpoint"); return 1;
}
int vscode_select_lf(Playback *p) {
    RECT status;
    if (!command(p,L"Change End of Line Sequence") ||
        !ui_wait_vscode_input(p,L"Select End of Line Sequence",L"",10000) ||
        !presentation_rest(p,"line-ending picker verified") || !shortcut(p,VK_CONTROL,VK_HOME) ||
        !keyboard_press(p,VK_RETURN) || !keyboard_press(p,VK_ESCAPE) ||
        !shortcut(p,VK_CONTROL,L'1') ||
        !ui_wait_control_rect(p,UIA_ButtonControlTypeId,L"LF",0,&status,10000)) return 0;
    playback_log(p,"verified editor line endings: LF"); return 1;
}
