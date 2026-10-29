#ifndef WEB_PAGE_H
#define WEB_PAGE_H
#include "playback.h"
/* Read-only accessibility snapshot of the current browser page. Ported from
   experiments/browser-explorer/accessibility.ps1; coordinates are read at runtime. */
#define WEB_MAX_LINKS 160
#define WEB_MAX_TABS 16
typedef struct { RECT rect; wchar_t url[512], name[160]; } WebLink;
typedef struct { RECT rect; wchar_t name[160]; int selected; } WebTab;
typedef struct {
    wchar_t url[1024], title[256];
    RECT document;
    int scroll_percent; /* 0-100, or -1 when the page does not report it */
    unsigned overlay_count, link_count, tab_count;
    int unreadable; /* no accessible document: URL from the address bar, content area estimated, no links */
    uint32_t signature; /* changes when the title or visible links change */
    WebLink links[WEB_MAX_LINKS];
    WebTab tabs[WEB_MAX_TABS];
} WebPage;
int web_page_snapshot(Playback *playback, HWND browser, WebPage *page);
/* Waits until the page exposes a document with a URL. */
int web_page_wait(Playback *playback, HWND browser, WebPage *page, DWORD timeout_ms);
/* The browser's address bar ("Address and search bar") and whether it already has keyboard focus. */
int web_address_bar(Playback *playback, HWND browser, RECT *rect, int *focused);
/* True when the element under the point is (inside) a link with this name. */
int web_link_at(Playback *playback, POINT point, const wchar_t *name);
#endif
