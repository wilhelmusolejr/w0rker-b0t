#define COBJMACROS
#include <initguid.h>
#include <oleacc.h>
#include <uiautomation.h>
#include "web_page.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Chromium's assistive-technology handshake: some builds only build the renderer's
   accessibility tree after a client asks the render widget for it. */
static BOOL CALLBACK wake_renderer(HWND window, LPARAM unused) {
    wchar_t cls[64];
    (void)unused;
    if (GetClassNameW(window, cls, 64) && !wcscmp(cls, L"Chrome_RenderWidgetHostHWND")) {
        DWORD_PTR result;
        IAccessible *accessible = NULL;
        SendMessageTimeoutW(window, WM_GETOBJECT, 0, OBJID_CLIENT, SMTO_ABORTIFHUNG, 750, &result);
        if (SUCCEEDED(AccessibleObjectFromWindow(window, OBJID_CLIENT, &IID_IAccessible, (void **)&accessible)) && accessible)
            IAccessible_Release(accessible);
    }
    return TRUE;
}
static IUIAutomationCondition *type_condition(IUIAutomation *a, int type, int visible_only) {
    IUIAutomationCondition *by_type = NULL, *visible = NULL, *both = NULL;
    VARIANT v;
    VariantInit(&v); v.vt = VT_I4; v.lVal = type;
    if (FAILED(IUIAutomation_CreatePropertyCondition(a, UIA_ControlTypePropertyId, v, &by_type))) return NULL;
    if (!visible_only) return by_type;
    VariantInit(&v); v.vt = VT_BOOL; v.boolVal = VARIANT_FALSE;
    if (SUCCEEDED(IUIAutomation_CreatePropertyCondition(a, UIA_IsOffscreenPropertyId, v, &visible)))
        IUIAutomation_CreateAndCondition(a, by_type, visible, &both);
    IUIAutomationCondition_Release(by_type);
    if (visible) IUIAutomationCondition_Release(visible);
    return both;
}
static void copy_text(wchar_t *out, size_t cap, const wchar_t *text) {
    size_t n = text ? wcslen(text) : 0;
    if (n >= cap) n = cap - 1;
    if (n) wmemcpy(out, text, n);
    out[n] = 0;
}
static int string_property(IUIAutomationElement *e, PROPERTYID id, int cached, wchar_t *out, size_t cap) {
    VARIANT v;
    int ok = 0;
    VariantInit(&v);
    if (SUCCEEDED(cached ? IUIAutomationElement_GetCachedPropertyValue(e, id, &v) :
                          IUIAutomationElement_GetCurrentPropertyValue(e, id, &v)) &&
        v.vt == VT_BSTR && v.bstrVal && *v.bstrVal && wcslen(v.bstrVal) < cap) { wcscpy(out, v.bstrVal); ok = 1; }
    VariantClear(&v);
    return ok;
}
static uint32_t hash(uint32_t h, const wchar_t *text) {
    for (; *text; ++text) { h ^= (uint32_t)*text; h *= 16777619u; }
    return h;
}
static IUIAutomationElement *largest_document(IUIAutomation *a, IUIAutomationElement *root, RECT *best) {
    IUIAutomationCondition *condition = type_condition(a, UIA_DocumentControlTypeId, 1);
    IUIAutomationElementArray *found = NULL;
    IUIAutomationElement *document = NULL;
    LONG area = 0;
    int count = 0, i;
    if (!condition) return NULL;
    if (SUCCEEDED(IUIAutomationElement_FindAll(root, TreeScope_Descendants, condition, &found)) &&
        SUCCEEDED(IUIAutomationElementArray_get_Length(found, &count))) {
        for (i = 0; i < count && i < 64; ++i) {
            IUIAutomationElement *e = NULL;
            RECT r;
            if (FAILED(IUIAutomationElementArray_GetElement(found, i, &e))) continue;
            if (SUCCEEDED(IUIAutomationElement_get_CurrentBoundingRectangle(e, &r)) &&
                (r.right - r.left) * (r.bottom - r.top) > area) {
                area = (r.right - r.left) * (r.bottom - r.top);
                if (document) IUIAutomationElement_Release(document);
                document = e; *best = r; continue;
            }
            IUIAutomationElement_Release(e);
        }
    }
    if (found) IUIAutomationElementArray_Release(found);
    IUIAutomationCondition_Release(condition);
    return document;
}
/* Chromium can expose a page as a separate accessibility fragment under its render widget
   instead of beneath the browser window, especially after a cross-site navigation. */
typedef struct { HWND windows[16]; unsigned count; } Renderers;
static BOOL CALLBACK collect_renderer(HWND window, LPARAM parameter) {
    Renderers *r = (Renderers *)parameter;
    wchar_t cls[64];
    if (r->count < 16 && IsWindowVisible(window) && GetClassNameW(window, cls, 64) &&
        !wcscmp(cls, L"Chrome_RenderWidgetHostHWND")) r->windows[r->count++] = window;
    return TRUE;
}
static IUIAutomationElement *renderer_document(IUIAutomation *a, HWND browser, RECT *best) {
    Renderers renderers = {{0}, 0};
    IUIAutomationElement *document = NULL;
    LONG area = 0;
    unsigned i;
    EnumChildWindows(browser, collect_renderer, (LPARAM)&renderers);
    for (i = 0; i < renderers.count; ++i) {
        IUIAutomationElement *fragment = NULL, *candidate = NULL;
        CONTROLTYPEID type = 0;
        RECT r;
        if (FAILED(IUIAutomation_ElementFromHandle(a, (UIA_HWND)renderers.windows[i], &fragment)) || !fragment) continue;
        if (SUCCEEDED(IUIAutomationElement_get_CurrentControlType(fragment, &type)) && type == UIA_DocumentControlTypeId &&
            SUCCEEDED(IUIAutomationElement_get_CurrentBoundingRectangle(fragment, &r))) {
            candidate = fragment; IUIAutomationElement_AddRef(candidate);
        } else candidate = largest_document(a, fragment, &r);
        IUIAutomationElement_Release(fragment);
        if (!candidate) continue;
        if ((r.right - r.left) * (r.bottom - r.top) > area) {
            area = (r.right - r.left) * (r.bottom - r.top);
            if (document) IUIAutomationElement_Release(document);
            document = candidate; *best = r;
        } else IUIAutomationElement_Release(candidate);
    }
    return document;
}
static void address_bar_url(IUIAutomation *a, IUIAutomationElement *root, wchar_t *url, size_t cap) {
    IUIAutomationCondition *condition = type_condition(a, UIA_EditControlTypeId, 0);
    IUIAutomationElementArray *edits = NULL;
    int count = 0, i;
    if (!condition) return;
    if (SUCCEEDED(IUIAutomationElement_FindAll(root, TreeScope_Descendants, condition, &edits)) &&
        SUCCEEDED(IUIAutomationElementArray_get_Length(edits, &count))) {
        for (i = 0; i < count && i < 32; ++i) {
            IUIAutomationElement *e = NULL;
            BSTR name = NULL;
            wchar_t value[1024];
            if (FAILED(IUIAutomationElementArray_GetElement(edits, i, &e))) continue;
            if (SUCCEEDED(IUIAutomationElement_get_CurrentName(e, &name)) && name &&
                (wcsstr(name, L"Address and search bar") || wcsstr(name, L"Search or enter address")) &&
                string_property(e, UIA_ValueValuePropertyId, 0, value, 1000)) {
                /* Chrome hides the scheme in the address bar. */
                if (wcsncmp(value, L"http://", 7) && wcsncmp(value, L"https://", 8)) swprintf(url, cap, L"https://%ls", value);
                else copy_text(url, cap, value);
                i = count;
            }
            SysFreeString(name); IUIAutomationElement_Release(e);
        }
    }
    if (edits) IUIAutomationElementArray_Release(edits);
    IUIAutomationCondition_Release(condition);
}
static unsigned count_overlays(IUIAutomation *a, IUIAutomationElement *document) {
    IUIAutomationCondition *dialog = NULL, *alert = NULL, *either = NULL;
    IUIAutomationElementArray *found = NULL;
    VARIANT v;
    int count = 0;
    VariantInit(&v); v.vt = VT_BSTR; v.bstrVal = SysAllocString(L"dialog");
    IUIAutomation_CreatePropertyCondition(a, UIA_AriaRolePropertyId, v, &dialog); VariantClear(&v);
    VariantInit(&v); v.vt = VT_BSTR; v.bstrVal = SysAllocString(L"alertdialog");
    IUIAutomation_CreatePropertyCondition(a, UIA_AriaRolePropertyId, v, &alert); VariantClear(&v);
    if (dialog && alert && SUCCEEDED(IUIAutomation_CreateOrCondition(a, dialog, alert, &either)) &&
        SUCCEEDED(IUIAutomationElement_FindAll(document, TreeScope_Descendants, either, &found)))
        IUIAutomationElementArray_get_Length(found, &count);
    if (found) IUIAutomationElementArray_Release(found);
    if (either) IUIAutomationCondition_Release(either);
    if (dialog) IUIAutomationCondition_Release(dialog);
    if (alert) IUIAutomationCondition_Release(alert);
    return (unsigned)count;
}
static void collect_links(IUIAutomation *a, IUIAutomationElement *document, WebPage *page) {
    IUIAutomationCondition *condition = type_condition(a, UIA_HyperlinkControlTypeId, 1);
    IUIAutomationCacheRequest *cache = NULL;
    IUIAutomationElementArray *found = NULL;
    int count = 0, i;
    if (!condition || FAILED(IUIAutomation_CreateCacheRequest(a, &cache)) ||
        FAILED(IUIAutomationCacheRequest_AddProperty(cache, UIA_NamePropertyId)) ||
        FAILED(IUIAutomationCacheRequest_AddProperty(cache, UIA_BoundingRectanglePropertyId)) ||
        FAILED(IUIAutomationCacheRequest_AddProperty(cache, UIA_ValueValuePropertyId)) ||
        FAILED(IUIAutomationElement_FindAllBuildCache(document, TreeScope_Descendants, condition, cache, &found)) ||
        FAILED(IUIAutomationElementArray_get_Length(found, &count))) goto done;
    if (count > 4000) count = 4000;
    for (i = 0; i < count && page->link_count < WEB_MAX_LINKS; ++i) {
        IUIAutomationElement *e = NULL;
        BSTR name = NULL;
        RECT r;
        WebLink *link = &page->links[page->link_count];
        if (FAILED(IUIAutomationElementArray_GetElement(found, i, &e))) continue;
        if (SUCCEEDED(IUIAutomationElement_get_CachedName(e, &name)) && name && *name &&
            SUCCEEDED(IUIAutomationElement_get_CachedBoundingRectangle(e, &r)) &&
            r.right - r.left >= 4 && r.bottom - r.top >= 4 &&
            r.left >= page->document.left && r.right <= page->document.right &&
            r.top >= page->document.top && r.bottom <= page->document.bottom &&
            string_property(e, UIA_ValueValuePropertyId, 1, link->url, 512) &&
            (!wcsncmp(link->url, L"https://", 8) || !wcsncmp(link->url, L"http://", 7))) {
            link->rect = r;
            copy_text(link->name, 160, name);
            if (page->link_count < 60) page->signature = hash(page->signature, link->name);
            ++page->link_count;
        }
        SysFreeString(name); IUIAutomationElement_Release(e);
    }
done:
    if (found) IUIAutomationElementArray_Release(found);
    if (cache) IUIAutomationCacheRequest_Release(cache);
    if (condition) IUIAutomationCondition_Release(condition);
}
static void collect_tabs(IUIAutomation *a, IUIAutomationElement *root, WebPage *page) {
    IUIAutomationCondition *condition = type_condition(a, UIA_TabItemControlTypeId, 1);
    IUIAutomationElementArray *found = NULL;
    int count = 0, i;
    if (!condition) return;
    if (SUCCEEDED(IUIAutomationElement_FindAll(root, TreeScope_Descendants, condition, &found)) &&
        SUCCEEDED(IUIAutomationElementArray_get_Length(found, &count))) {
        for (i = 0; i < count && page->tab_count < WEB_MAX_TABS; ++i) {
            IUIAutomationElement *e = NULL;
            BSTR name = NULL;
            RECT r;
            VARIANT selected;
            WebTab *tab = &page->tabs[page->tab_count];
            VariantInit(&selected);
            if (FAILED(IUIAutomationElementArray_GetElement(found, i, &e))) continue;
            /* Browser tabs sit above the web document; tab widgets inside pages are ignored. */
            if (SUCCEEDED(IUIAutomationElement_get_CurrentBoundingRectangle(e, &r)) &&
                r.bottom <= page->document.top && r.right - r.left >= 8 && r.bottom - r.top >= 8) {
                tab->rect = r;
                if (SUCCEEDED(IUIAutomationElement_get_CurrentName(e, &name))) copy_text(tab->name, 160, name);
                tab->selected = SUCCEEDED(IUIAutomationElement_GetCurrentPropertyValue(e, UIA_SelectionItemIsSelectedPropertyId, &selected)) &&
                    selected.vt == VT_BOOL && selected.boolVal;
                ++page->tab_count;
            }
            VariantClear(&selected); SysFreeString(name); IUIAutomationElement_Release(e);
        }
    }
    if (found) IUIAutomationElementArray_Release(found);
    IUIAutomationCondition_Release(condition);
}
int web_page_snapshot(Playback *p, HWND browser, WebPage *page) {
    IUIAutomation *a = NULL;
    IUIAutomationElement *root = NULL, *document = NULL;
    VARIANT scroll;
    BSTR title = NULL;
    HRESULT initialized;
    int ok = 0;
    memset(page, 0, sizeof(*page));
    page->scroll_percent = -1;
    page->signature = 2166136261u;
    if (!playback_guard(p)) return 0;
    EnumChildWindows(browser, wake_renderer, 0);
    initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 0;
    if (FAILED(CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation, (void **)&a)) ||
        FAILED(IUIAutomation_ElementFromHandle(a, (UIA_HWND)browser, &root))) goto done;
    if (!(document = largest_document(a, root, &page->document)) &&
        !(document = renderer_document(a, browser, &page->document))) {
        /* The page is not readable yet: keep the address and treat the content area as the page,
           so reading and scrolling can continue without link clicks. */
        RECT client;
        POINT origin = {0, 0};
        address_bar_url(a, root, page->url, 1024);
        if (page->url[0] && GetClientRect(browser, &client) && ClientToScreen(browser, &origin)) {
            OffsetRect(&client, origin.x, origin.y);
            client.top += 140;
            if (client.bottom - client.top > 200) { page->document = client; page->unreadable = 1; ok = 1; }
        }
        goto done;
    }
    if (SUCCEEDED(IUIAutomationElement_get_CurrentName(document, &title))) copy_text(page->title, 256, title);
    if (!string_property(document, UIA_ValueValuePropertyId, 0, page->url, 1024) &&
        !string_property(document, UIA_LegacyIAccessibleValuePropertyId, 0, page->url, 1024)) page->url[0] = 0;
    if (wcsncmp(page->url, L"http", 4)) address_bar_url(a, root, page->url, 1024);
    VariantInit(&scroll);
    if (SUCCEEDED(IUIAutomationElement_GetCurrentPropertyValue(document, UIA_ScrollVerticalScrollPercentPropertyId, &scroll)) &&
        scroll.vt == VT_R8 && scroll.dblVal >= 0) page->scroll_percent = (int)(scroll.dblVal + 0.5);
    VariantClear(&scroll);
    page->signature = hash(page->signature, page->title);
    page->overlay_count = count_overlays(a, document);
    collect_links(a, document, page);
    collect_tabs(a, root, page);
    ok = page->url[0] != 0;
done:
    SysFreeString(title);
    if (document) IUIAutomationElement_Release(document);
    if (root) IUIAutomationElement_Release(root);
    if (a) IUIAutomation_Release(a);
    CoUninitialize();
    return ok && playback_poll(p);
}
int web_page_wait(Playback *p, HWND browser, WebPage *page, DWORD timeout_ms) {
    ULONGLONG started = GetTickCount64(), paused = p->paused_ms;
    do {
        if (web_page_snapshot(p, browser, page)) return 1;
        if (InterlockedCompareExchange(&p->stopped, 0, 0) || !playback_wait(p, 400)) return 0;
    } while (GetTickCount64() - started - (p->paused_ms - paused) < timeout_ms);
    playback_log(p, "the page did not expose an accessible document within %lu ms", timeout_ms);
    return 0;
}
int web_address_bar(Playback *p, HWND browser, RECT *rect, int *focused) {
    IUIAutomation *a = NULL;
    IUIAutomationElement *root = NULL;
    IUIAutomationElementArray *edits = NULL;
    IUIAutomationCondition *condition = NULL;
    HRESULT initialized;
    int count = 0, i, found = 0;
    if (!playback_guard(p)) return 0;
    initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 0;
    if (SUCCEEDED(CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation, (void **)&a)) &&
        SUCCEEDED(IUIAutomation_ElementFromHandle(a, (UIA_HWND)browser, &root)) &&
        (condition = type_condition(a, UIA_EditControlTypeId, 1)) &&
        SUCCEEDED(IUIAutomationElement_FindAll(root, TreeScope_Descendants, condition, &edits)) &&
        SUCCEEDED(IUIAutomationElementArray_get_Length(edits, &count))) {
        for (i = 0; i < count && i < 32 && !found; ++i) {
            IUIAutomationElement *e = NULL;
            BSTR name = NULL;
            BOOL focus = FALSE;
            RECT r;
            if (FAILED(IUIAutomationElementArray_GetElement(edits, i, &e))) continue;
            if (SUCCEEDED(IUIAutomationElement_get_CurrentName(e, &name)) && name &&
                (wcsstr(name, L"Address and search bar") || wcsstr(name, L"Search or enter address")) &&
                SUCCEEDED(IUIAutomationElement_get_CurrentBoundingRectangle(e, &r)) &&
                r.right - r.left >= 80 && r.bottom - r.top >= 10) {
                *rect = r;
                *focused = SUCCEEDED(IUIAutomationElement_get_CurrentHasKeyboardFocus(e, &focus)) && focus;
                found = 1;
            }
            SysFreeString(name); IUIAutomationElement_Release(e);
        }
    }
    if (edits) IUIAutomationElementArray_Release(edits);
    if (condition) IUIAutomationCondition_Release(condition);
    if (root) IUIAutomationElement_Release(root);
    if (a) IUIAutomation_Release(a);
    CoUninitialize();
    return found && playback_poll(p);
}
int web_link_at(Playback *p, POINT point, const wchar_t *name) {
    IUIAutomation *a = NULL;
    IUIAutomationElement *hit = NULL;
    IUIAutomationTreeWalker *walker = NULL;
    HRESULT initialized;
    unsigned depth;
    int matched = 0;
    if (!playback_guard(p)) return 0;
    initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 0;
    if (SUCCEEDED(CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, &IID_IUIAutomation, (void **)&a)) &&
        SUCCEEDED(IUIAutomation_get_ControlViewWalker(a, &walker)) &&
        SUCCEEDED(IUIAutomation_ElementFromPoint(a, point, &hit))) {
        for (depth = 0; hit && depth < 12 && !matched; ++depth) {
            IUIAutomationElement *parent = NULL;
            CONTROLTYPEID type = 0;
            BSTR label = NULL;
            if (SUCCEEDED(IUIAutomationElement_get_CurrentControlType(hit, &type)) && type == UIA_HyperlinkControlTypeId &&
                SUCCEEDED(IUIAutomationElement_get_CurrentName(hit, &label)) && label && !wcsncmp(label, name, 159))
                matched = 1;
            SysFreeString(label);
            if (FAILED(IUIAutomationTreeWalker_GetParentElement(walker, hit, &parent))) parent = NULL;
            IUIAutomationElement_Release(hit); hit = parent;
        }
    }
    if (hit) IUIAutomationElement_Release(hit);
    if (walker) IUIAutomationTreeWalker_Release(walker);
    if (a) IUIAutomation_Release(a);
    CoUninitialize();
    return matched;
}
