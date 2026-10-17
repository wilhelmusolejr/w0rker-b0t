#define COBJMACROS
#include <initguid.h>
#include <oleacc.h>
#include <uiautomation.h>
#include "ui_targets.h"
#include <stdlib.h>
#include <wchar.h>
#include <wctype.h>

typedef struct { HWND list; unsigned matches; } DesktopList;
static BOOL CALLBACK find_desktop_list(HWND window, LPARAM parameter) {
    DesktopList *search = (DesktopList *)parameter;
    wchar_t cls[128];
    if (IsWindowVisible(window) && GetClassNameW(window, cls, 128) && !wcscmp(cls, L"SysListView32")) {
        search->list = window; ++search->matches;
    }
    return TRUE;
}
int ui_desktop_folder_rect(Playback *p, const wchar_t *name, RECT *rect) {
    DesktopList list = {0};
    IAccessible *accessible = NULL;
    VARIANT *children = NULL;
    LONG count = 0, obtained = 0, i;
    unsigned matches = 0;
    HRESULT initialized;
    if (!playback_guard(p)) return 0;
    initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 0;
    EnumChildWindows(p->target, find_desktop_list, (LPARAM)&list);
    if (list.matches != 1 || FAILED(AccessibleObjectFromWindow(list.list, OBJID_CLIENT,
        &IID_IAccessible, (void **)&accessible))) goto done;
    if (FAILED(IAccessible_get_accChildCount(accessible, &count)) || count < 1 || count > 4096) goto done;
    children = calloc((size_t)count, sizeof(*children));
    if (!children || FAILED(AccessibleChildren(accessible, 0, count, children, &obtained))) goto done;
    for (i = 0; i < obtained; ++i) {
        IAccessible *object = accessible;
        VARIANT child = children[i];
        BSTR label = NULL;
        LONG x, y, width, height;
        if (!playback_poll(p)) goto done;
        if (child.vt == VT_DISPATCH) {
            if (FAILED(IDispatch_QueryInterface(child.pdispVal, &IID_IAccessible, (void **)&object))) continue;
            VariantInit(&child); child.vt = VT_I4; child.lVal = CHILDID_SELF;
        } else if (child.vt != VT_I4) continue;
        if (SUCCEEDED(IAccessible_get_accName(object, child, &label)) && label && !_wcsicmp(label, name) &&
            SUCCEEDED(IAccessible_accLocation(object, &x, &y, &width, &height, child)) && width > 3 && height > 3) {
            rect->left = x; rect->top = y; rect->right = x + width; rect->bottom = y + height;
            ++matches;
        }
        SysFreeString(label);
        if (object != accessible) IAccessible_Release(object);
    }
done:
    if (children) { for (i = 0; i < count; ++i) VariantClear(&children[i]); free(children); }
    if (accessible) IAccessible_Release(accessible);
    CoUninitialize();
    if (matches != 1) playback_log(p, "desktop folder must match one visible icon; found %u", matches);
    return matches == 1 && playback_poll(p) && playback_guard(p);
}

static int control_rect(Playback *p, int type, const wchar_t *name, int contains_name, RECT *rect, int quiet) {
    IUIAutomation *automation = NULL;
    IUIAutomationElement *root = NULL;
    IUIAutomationElementArray *elements = NULL;
    IUIAutomationCondition *condition = NULL;
    VARIANT property;
    HRESULT initialized;
    unsigned matches = 0;
    int count = 0, i;
    if (!playback_guard(p)) return 0;
    initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 0;
    VariantInit(&property); property.vt = VT_I4; property.lVal = type;
    if (FAILED(CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation,
        (void **)&automation)) || FAILED(IUIAutomation_ElementFromHandle(automation, (UIA_HWND)p->target, &root)) ||
        FAILED(IUIAutomation_CreatePropertyCondition(automation, UIA_ControlTypePropertyId, property, &condition)) ||
        FAILED(IUIAutomationElement_FindAll(root, TreeScope_Descendants, condition, &elements)) ||
        FAILED(IUIAutomationElementArray_get_Length(elements, &count)) || count > 4096) goto done;
    for (i = 0; i < count; ++i) {
        IUIAutomationElement *element = NULL;
        BSTR label = NULL;
        BOOL offscreen = TRUE;
        RECT bounds;
        if (!playback_poll(p)) goto done;
        if (FAILED(IUIAutomationElementArray_GetElement(elements, i, &element))) continue;
        if (SUCCEEDED(IUIAutomationElement_get_CurrentName(element, &label)) && label &&
            (contains_name ? wcsstr(label, name) != NULL : !_wcsicmp(label, name)) &&
            SUCCEEDED(IUIAutomationElement_get_CurrentIsOffscreen(element, &offscreen)) && !offscreen &&
            SUCCEEDED(IUIAutomationElement_get_CurrentBoundingRectangle(element, &bounds)) &&
            bounds.right - bounds.left >= 4 && bounds.bottom - bounds.top >= 4) { *rect = bounds; ++matches; }
        SysFreeString(label);
        IUIAutomationElement_Release(element);
    }
done:
    if (elements) IUIAutomationElementArray_Release(elements);
    if (condition) IUIAutomationCondition_Release(condition);
    if (root) IUIAutomationElement_Release(root);
    if (automation) IUIAutomation_Release(automation);
    CoUninitialize();
    if (matches != 1 && !quiet) playback_log(p, "control must match one visible accessible element; found %u", matches);
    return matches == 1 && playback_poll(p) && playback_guard(p);
}
int ui_control_rect(Playback *p, int type, const wchar_t *name, int contains_name, RECT *rect) {
    return control_rect(p,type,name,contains_name,rect,0);
}
int ui_find_control_rect(Playback *p, int type, const wchar_t *name, RECT *rect) {
    return control_rect(p,type,name,0,rect,1);
}
int ui_wait_control_rect(Playback *p, int type, const wchar_t *name, int contains_name, RECT *rect, DWORD timeout_ms) {
    ULONGLONG start = GetTickCount64(), paused = p->paused_ms;
    do {
        if (!playback_poll(p) || !playback_guard(p)) return 0;
        if (control_rect(p,type,name,contains_name,rect,1)) return 1;
        if (!playback_wait(p,250)) return 0;
    } while (GetTickCount64()-start-(p->paused_ms-paused) < timeout_ms);
    playback_log(p,"expected accessible control did not become ready within %lu ms",timeout_ms); return 0;
}

static int focus_in_quick_input(IUIAutomation *automation, IUIAutomationTreeWalker *walker,
                                IUIAutomationElement *input, IUIAutomationElement *focused) {
    IUIAutomationElement *widget = NULL, *ancestor = focused;
    BSTR class_name = NULL;
    unsigned depth;
    int ready = 0;
    if (FAILED(IUIAutomationTreeWalker_GetParentElement(walker,input,&widget)) || !widget ||
        FAILED(IUIAutomationElement_get_CurrentClassName(widget,&class_name)) || !class_name ||
        wcsncmp(class_name,L"quick-input-widget",18) ||
        (class_name[18] && class_name[18] != L' ')) goto done;
    IUIAutomationElement_AddRef(ancestor);
    /* ARIA active-descendant focus can be on a result row rather than the input. */
    for (depth = 0; ancestor && depth < 8; ++depth) {
        IUIAutomationElement *parent = NULL;
        BOOL same = FALSE;
        if (SUCCEEDED(IUIAutomation_CompareElements(automation,widget,ancestor,&same)) && same) {
            ready = 1; break;
        }
        if (FAILED(IUIAutomationTreeWalker_GetParentElement(walker,ancestor,&parent))) break;
        IUIAutomationElement_Release(ancestor); ancestor = parent;
    }
    if (ancestor) IUIAutomationElement_Release(ancestor);
done:
    SysFreeString(class_name);
    if (widget) IUIAutomationElement_Release(widget);
    return ready;
}
static int vscode_input_ready(Playback *p, const wchar_t *name, const wchar_t *value) {
    IUIAutomation *automation = NULL;
    IUIAutomationElement *root = NULL, *focused = NULL;
    IUIAutomationElementArray *elements = NULL;
    IUIAutomationCondition *condition = NULL;
    IUIAutomationTreeWalker *walker = NULL;
    VARIANT property;
    HRESULT initialized;
    int count = 0, i;
    unsigned matches = 0;
    if (!playback_guard(p)) return 0;
    initialized = CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 0;
    VariantInit(&property); property.vt = VT_BSTR; property.bstrVal = SysAllocString(name);
    if (!property.bstrVal) goto done;
    if (FAILED(CoCreateInstance(&CLSID_CUIAutomation,NULL,CLSCTX_INPROC_SERVER,&IID_IUIAutomation,
        (void **)&automation)) || FAILED(IUIAutomation_ElementFromHandle(automation,(UIA_HWND)p->target,&root)) ||
        FAILED(IUIAutomation_GetFocusedElement(automation,&focused)) || !focused ||
        FAILED(IUIAutomation_get_ControlViewWalker(automation,&walker)) ||
        FAILED(IUIAutomation_CreatePropertyCondition(automation,UIA_NamePropertyId,property,&condition)) ||
        FAILED(IUIAutomationElement_FindAll(root,TreeScope_Descendants,condition,&elements)) ||
        FAILED(IUIAutomationElementArray_get_Length(elements,&count)) || count > 64) goto done;
    for (i = 0; i < count; ++i) {
        IUIAutomationElement *input = NULL;
        CONTROLTYPEID type = 0;
        BOOL offscreen = TRUE, enabled = FALSE;
        VARIANT current;
        if (!playback_poll(p)) goto done;
        VariantInit(&current);
        if (FAILED(IUIAutomationElementArray_GetElement(elements,i,&input))) continue;
        if (SUCCEEDED(IUIAutomationElement_get_CurrentControlType(input,&type)) &&
            (type == UIA_EditControlTypeId || type == UIA_ComboBoxControlTypeId) &&
            SUCCEEDED(IUIAutomationElement_get_CurrentIsEnabled(input,&enabled)) && enabled &&
            SUCCEEDED(IUIAutomationElement_get_CurrentIsOffscreen(input,&offscreen)) && !offscreen &&
            SUCCEEDED(IUIAutomationElement_GetCurrentPropertyValue(input,UIA_ValueValuePropertyId,&current)) &&
            current.vt == VT_BSTR && current.bstrVal && !wcscmp(current.bstrVal,value) &&
            focus_in_quick_input(automation,walker,input,focused)) ++matches;
        VariantClear(&current); IUIAutomationElement_Release(input);
    }
done:
    VariantClear(&property);
    if (elements) IUIAutomationElementArray_Release(elements);
    if (condition) IUIAutomationCondition_Release(condition);
    if (walker) IUIAutomationTreeWalker_Release(walker);
    if (focused) IUIAutomationElement_Release(focused);
    if (root) IUIAutomationElement_Release(root);
    if (automation) IUIAutomation_Release(automation);
    CoUninitialize();
    return matches == 1 && playback_poll(p) && playback_guard(p);
}
int ui_wait_vscode_input(Playback *p, const wchar_t *name, const wchar_t *value, DWORD timeout_ms) {
    ULONGLONG start = GetTickCount64(), paused = p->paused_ms;
    do {
        if (!playback_poll(p) || !playback_guard(p)) return 0;
        if (vscode_input_ready(p,name,value)) return 1;
        if (!playback_wait(p,200)) return 0;
    } while (GetTickCount64()-start-(p->paused_ms-paused) < timeout_ms);
    playback_log(p,"expected focused input was unavailable or had an unexpected value; no further setup text was sent");
    return 0;
}

static int web_host_excluded(const wchar_t *url, const wchar_t *name) {
    static const wchar_t *const blocked[] = {L"google.", L"gstatic.", L"googleusercontent.",
        L"googleadservices.", L"doubleclick.", L"youtube.", L"youtu.be"};
    wchar_t host[128];
    const wchar_t *start = wcsstr(url, L"://");
    size_t length = 0, i;
    if (!start) return 1;
    start += 3;
    while (start[length] && start[length] != L'/' && start[length] != L'?' && start[length] != L'#' &&
           start[length] != L':' && length < 127) { host[length] = towlower(start[length]); ++length; }
    host[length] = 0;
    if (!length) return 1;
    /* Google sends organic results through /goto or /url redirects; their names carry the site address. */
    if ((!wcsncmp(host, L"www.google.", 11) || !wcsncmp(host, L"google.", 7)) &&
        (!wcsncmp(start + length, L"/goto?", 6) || !wcsncmp(start + length, L"/url?", 5)))
        return !(wcsstr(name, L"https://") || wcsstr(name, L"http://") || wcsstr(name, L"www."));
    for (i = 0; i < sizeof(blocked)/sizeof(*blocked); ++i) {
        const wchar_t *hit = wcsstr(host, blocked[i]);
        if (hit && (hit == host || hit[-1] == L'.')) return 1;
    }
    return 0;
}
/* Google groups ads under a "Sponsored" label or an "Ads" region; skip anything inside one. */
static int web_link_sponsored(IUIAutomationTreeWalker *walker, IUIAutomationElement *link) {
    IUIAutomationElement *ancestor = NULL, *parent = NULL;
    unsigned depth;
    int sponsored = 0;
    if (FAILED(IUIAutomationTreeWalker_GetParentElement(walker, link, &ancestor))) return 1;
    for (depth = 0; ancestor && depth < 10 && !sponsored; ++depth) {
        BSTR label = NULL;
        if (SUCCEEDED(IUIAutomationElement_get_CurrentName(ancestor, &label)) && label &&
            (wcsstr(label, L"Sponsored") || !wcscmp(label, L"Ads") || !wcsncmp(label, L"Ads ", 4))) sponsored = 1;
        SysFreeString(label);
        if (FAILED(IUIAutomationTreeWalker_GetParentElement(walker, ancestor, &parent))) parent = NULL;
        IUIAutomationElement_Release(ancestor); ancestor = parent;
    }
    if (ancestor) IUIAutomationElement_Release(ancestor);
    return sponsored;
}
int ui_web_links(Playback *p, UiWebLink *links, unsigned capacity, unsigned *found) {
    IUIAutomation *automation = NULL;
    IUIAutomationElement *root = NULL;
    IUIAutomationElementArray *elements = NULL;
    IUIAutomationCondition *condition = NULL;
    IUIAutomationTreeWalker *walker = NULL;
    VARIANT property;
    HRESULT initialized;
    RECT client;
    POINT origin = {0, 0};
    int count = 0, i, ok = 0;
    *found = 0;
    if (!playback_guard(p) || !GetClientRect(p->target, &client) || !ClientToScreen(p->target, &origin)) return 0;
    /* Leave room for Chrome's tab strip, toolbar, and Google's sticky search header. */
    OffsetRect(&client, origin.x, origin.y);
    client.top += 180; client.bottom -= 20;
    initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 0;
    VariantInit(&property); property.vt = VT_I4; property.lVal = UIA_HyperlinkControlTypeId;
    if (FAILED(CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation,
        (void **)&automation)) || FAILED(IUIAutomation_ElementFromHandle(automation, (UIA_HWND)p->target, &root)) ||
        FAILED(IUIAutomation_get_ControlViewWalker(automation, &walker)) ||
        FAILED(IUIAutomation_CreatePropertyCondition(automation, UIA_ControlTypePropertyId, property, &condition)) ||
        FAILED(IUIAutomationElement_FindAll(root, TreeScope_Descendants, condition, &elements)) ||
        FAILED(IUIAutomationElementArray_get_Length(elements, &count))) goto done;
    if (count > 4096) count = 4096;
    for (i = 0; i < count && *found < capacity; ++i) {
        IUIAutomationElement *element = NULL;
        BSTR label = NULL;
        BOOL offscreen = TRUE;
        RECT bounds, inside;
        VARIANT value;
        if (!playback_poll(p)) goto done;
        VariantInit(&value);
        if (FAILED(IUIAutomationElementArray_GetElement(elements, i, &element))) continue;
        if (SUCCEEDED(IUIAutomationElement_get_CurrentName(element, &label)) && label && wcslen(label) >= 4 &&
            SUCCEEDED(IUIAutomationElement_get_CurrentIsOffscreen(element, &offscreen)) && !offscreen &&
            SUCCEEDED(IUIAutomationElement_get_CurrentBoundingRectangle(element, &bounds)) &&
            bounds.right - bounds.left >= 40 && bounds.bottom - bounds.top >= 10 &&
            IntersectRect(&inside, &bounds, &client) && EqualRect(&inside, &bounds) &&
            SUCCEEDED(IUIAutomationElement_GetCurrentPropertyValue(element, UIA_ValueValuePropertyId, &value)) &&
            value.vt == VT_BSTR && value.bstrVal && wcslen(value.bstrVal) < 220 && wcslen(label) < 400 &&
            (!wcsncmp(value.bstrVal, L"https://", 8) || !wcsncmp(value.bstrVal, L"http://", 7)) &&
            !web_host_excluded(value.bstrVal, label) && !web_link_sponsored(walker, element)) {
            links[*found].rect = bounds;
            wcscpy(links[*found].url, value.bstrVal);
            wcsncpy(links[*found].name, label, 119); links[*found].name[119] = 0;
            ++*found;
        }
        VariantClear(&value); SysFreeString(label);
        IUIAutomationElement_Release(element);
    }
    ok = 1;
done:
    if (elements) IUIAutomationElementArray_Release(elements);
    if (condition) IUIAutomationCondition_Release(condition);
    if (walker) IUIAutomationTreeWalker_Release(walker);
    if (root) IUIAutomationElement_Release(root);
    if (automation) IUIAutomation_Release(automation);
    CoUninitialize();
    return ok && playback_poll(p) && playback_guard(p);
}
