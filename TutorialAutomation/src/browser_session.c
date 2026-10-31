#include "browser_session.h"
#include "browser_action.h"
#include "browser_scroll_plan.h"
#include "human_motion.h"
#include "keyboard.h"
#include "mouse.h"
#include "ui_targets.h"
#include "web_page.h"
#include "web_policy.h"
#include "window_manager.h"
#include "side_apps.h"
#include "side_apps.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VISITED 96

typedef struct {
    Playback *p;
    const BrowseSession *s;
    MotionRng rng;
    MotionScroll scroll;
    HWND window;
    WebPage *page;
    ULONGLONG started, paused;
    ULONGLONG last_move;
    unsigned stalls, depth, tabs_open;
    wchar_t scope[1024];
    wchar_t visited[MAX_VISITED][256];
    unsigned visit_counts[MAX_VISITED], visited_count;
    wchar_t bookmarked[16][512];
    unsigned bookmark_count;
    unsigned pages, clicks, idles, wanders, side_searches, bookmarks, scrolls;
    SideAppsTimer apps;
    int has_apps;
} Session;

/* ---------- plan ---------- */

void browser_session_plan(const BrowseSession *s, BrowsePlan *plan) {
    const BrowseCatalog *c = s->catalog;
    MotionRng rng;
    unsigned i, high = c->tabs[1] < BROWSE_MAX_TABS ? c->tabs[1] : BROWSE_MAX_TABS;
    memset(plan, 0, sizeof(*plan));
    motion_seed(&rng, s->seed);
    plan->tab_count = motion_between(&rng, c->tabs[0], high);
    for (i = 0; i < plan->tab_count; ++i) {
        unsigned tries, j;
        /* Each tab shows something different: re-draw a repeated site or search a few times. */
        for (tries = 0; tries < 6; ++tries) {
            if (!browse_pick_opener(c, s->profile, &rng, &plan->openers[i])) break;
            for (j = 0; j < i && wcscmp(plan->openers[j].text, plan->openers[i].text); ++j) {}
            if (j == i) break;
        }
        if (!plan->openers[i].text[0]) { plan->tab_count = i; break; }
        plan->visit_seconds[i] = motion_between(&rng, c->tab_visit_seconds[0], c->tab_visit_seconds[1]);
        plan->side_search[i] = motion_chance(&rng, c->new_tab_search_percent);
        if (plan->side_search[i]) {
            plan->side[i].search = 1;
            if (!browse_pick_search(c, s->profile, &rng, &plan->side[i].kind, plan->side[i].text, 300))
                plan->side_search[i] = 0;
        }
        plan->bookmark[i] = motion_chance(&rng, c->bookmark_percent);
    }
}
static void print_wide(const char *label, const wchar_t *text) {
    char utf8[700];
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
    printf("%s%s", label, utf8);
}
void browser_session_print_plan(const BrowseSession *s, const BrowsePlan *plan) {
    unsigned i;
    print_wide("Profile: ", s->profile->name);
    printf(" (id %u, randomness %u, mistype level %u)\n", s->profile->id, s->profile->randomness_score, s->profile->mistype_level);
    printf("Session: %.1f minutes, seed %lu, browser opened from the %s.\n", s->duration_ms / 60000.0,
        (unsigned long)s->seed, s->via_taskbar ? "taskbar icon" : "Windows Run dialog");
    printf("Tabs to fill: %u\n", plan->tab_count);
    for (i = 0; i < plan->tab_count; ++i) {
        const BrowseOpener *o = &plan->openers[i];
        printf("  tab %u: ", i + 1);
        if (o->search) { printf("Google search [%s] ", browse_search_kind_name(o->kind)); print_wide("\"", o->text); printf("\""); }
        else { print_wide("direct site ", o->text); print_wide(" [", o->site->category); printf("]"); }
        printf(", first visit about %u s", plan->visit_seconds[i]);
        if (plan->side_search[i]) print_wide(", side search in a new tab \"", plan->side[i].text), printf("\"");
        if (plan->bookmark[i]) printf(", bookmark");
        putchar('\n');
    }
    printf("Then: switch between open tabs until the time is up. Idle %u%% of actions for %u-%u s; pages read %u-%u s.\n",
        s->catalog->idle_percent, s->catalog->idle_seconds[0], s->catalog->idle_seconds[1],
        s->catalog->page_seconds[0], s->catalog->page_seconds[1]);
}

/* ---------- clock ---------- */

static DWORD since(const Session *x, ULONGLONG start, ULONGLONG paused) {
    ULONGLONG elapsed = GetTickCount64() - start - (x->p->paused_ms - paused);
    return elapsed > 0xffffffffu ? 0xffffffffu : (DWORD)elapsed;
}
static DWORD session_left(const Session *x) {
    DWORD used = since(x, x->started, x->paused);
    /* A work session asked this slot to end: no more tabs or visits. */
    if (playback_yield(x->p)) return 0;
    return used >= x->s->duration_ms ? 0 : x->s->duration_ms - used;
}
/* Wait, but never past the end of the session. */
static int hold(Session *x, DWORD ms) {
    DWORD left = session_left(x);
    return playback_wait(x->p, ms < left ? ms : left);
}

/* ---------- pointer ---------- */

static int in_browser(const Session *x, POINT point) {
    HWND hit = WindowFromPoint(point);
    return hit && GetAncestor(hit, GA_ROOT) == x->window;
}
static int move_to(Session *x, POINT target, int purpose, double width) {
    if (!mouse_move_human(x->p, &x->rng, target, purpose, width)) return 0;
    x->last_move = GetTickCount64();
    return 1;
}
/* Move away from what was just clicked or read, to a different resting point each time. */
static int relax(Session *x, const char *reason, unsigned percent) {
    POINT point, current;
    if (!motion_chance(&x->rng, percent)) return 1;
    if (strcmp(reason, "after-click") && GetTickCount64() - x->last_move < 1500) return 1;
    if (!motion_rest_point(&x->rng, &x->page->document, &point) || !GetCursorPos(&current)) return 1;
    if (labs(point.x - current.x) + labs(point.y - current.y) < 25 || !in_browser(x, point)) return 1;
    playback_log(x->p, "pointer rest (%s) at %ld,%ld", reason, point.x, point.y);
    return move_to(x, point, MOTION_REST, 160);
}
/* Random mouse movement: drift over the page a few times without clicking. */
static int wander(Session *x) {
    unsigned moves = motion_between(&x->rng, 1, 3), i;
    ++x->wanders;
    playback_log(x->p, "random mouse movement: %u drift(s) over the page", moves);
    for (i = 0; i < moves && session_left(x); ++i) {
        POINT point;
        if (!motion_rest_point(&x->rng, &x->page->document, &point) || !in_browser(x, point)) break;
        if (!move_to(x, point, MOTION_REST, 160) || !hold(x, motion_between(&x->rng, 250, 1400))) return 0;
    }
    return 1;
}
/* Random idle: hands off the mouse and keyboard. */
static int idle(Session *x) {
    unsigned seconds = motion_between(&x->rng, x->s->catalog->idle_seconds[0], x->s->catalog->idle_seconds[1]);
    ++x->idles;
    playback_log(x->p, "random idle: %u seconds with no input", seconds);
    return hold(x, seconds * 1000);
}
static int read_pause(Session *x) {
    double seconds = x->scroll.last_fast ? motion_triangular(&x->rng, 0.8, 4.0, 1.4) : motion_triangular(&x->rng, 2.0, 11.0, 4.0);
    if (!relax(x, "reading", 32)) return 0;
    playback_log(x->p, "reading for %.1f seconds", seconds);
    return hold(x, (DWORD)(seconds * 1000));
}

/* ---------- page ---------- */

static int snapshot(Session *x) {
    if (web_page_snapshot(x->p, x->window, x->page)) return 1;
    return web_page_wait(x->p, x->window, x->page, 12000);
}
static int scroll(Session *x) {
    RECT *doc = &x->page->document;
    LONG width = doc->right - doc->left, height = doc->bottom - doc->top;
    POINT at, drift_anchor;
    MotionScrollPass pass;
    int old_percent = x->page->scroll_percent, in_content, reposition, drift;
    uint32_t old_signature = x->page->signature;
    unsigned b, s;
    if (!GetCursorPos(&at)) return 0;
    in_content = at.x > doc->left + width / 5 && at.x < doc->left + width * 86 / 100 &&
                 at.y > doc->top + height * 17 / 100 && at.y < doc->top + height * 85 / 100;
    reposition = !in_content || x->stalls >= 2 || motion_chance(&x->rng, 18);
    if (reposition && motion_rest_point(&x->rng, doc, &at)) {
        if (!in_browser(x, at)) { playback_log(x->p, "scroll area is covered by another window"); return 0; }
        if (!move_to(x, at, MOTION_REST, 160)) return 0;
    }
    motion_scroll_plan(&x->scroll, x->page->scroll_percent, x->stalls, &pass);
    drift = motion_chance(&x->rng, 50);
    if (drift && !GetCursorPos(&drift_anchor)) return 0;
    ++x->scrolls;
    for (b = 0; b < pass.count && session_left(x); ++b) {
        const MotionBurst *burst = &pass.bursts[b];
        playback_log(x->p, "scroll %s %s: %u notches%s", burst->fast ? "fast" : "slow",
            burst->direction < 0 ? "down" : "up", motion_burst_notches(burst),
            drift ? " with small pointer drift" : "");
        for (s = 0; s < burst->step_count; ++s)
            if (!mouse_wheel(x->p, burst->steps[s].notches) ||
                (drift && !mouse_scroll_nudge(x->p, &x->rng, drift_anchor, doc)) ||
                !hold(x, burst->steps[s].delay_ms)) return 0;
        if (!hold(x, burst->pause_after_ms)) return 0;
    }
    if (motion_chance(&x->rng, 25) && !relax(x, "after-scroll", 100)) return 0;
    if (!hold(x, pass.reading_ms) || !snapshot(x)) return 0;
    if (old_percent >= 0 && x->page->scroll_percent >= 0) x->stalls = old_percent == x->page->scroll_percent ? x->stalls + 1 : 0;
    else x->stalls = old_signature == x->page->signature ? x->stalls + 1 : 0;
    return 1;
}
static int shortcut(Playback *p, WORD modifier, WORD key) {
    WORD keys[2];
    keys[0] = modifier; keys[1] = key;
    return keyboard_shortcut(p, keys, 2);
}
static int click_rect(Session *x, const RECT *target, const wchar_t *link_name);
/* Click into the address bar like a person would. A first click selects the whole address, so typing
   replaces it. Falls back to Ctrl+L only when the bar cannot be found or clicked. */
static int focus_address_bar(Session *x) {
    RECT bar, text;
    int focused = 0, result;
    if (web_address_bar(x->p, x->window, &bar, &focused)) {
        /* Aim at the text area only: the site-info button sits on the left, the bookmark star and
           extension icons on the right. */
        text = bar;
        text.left = bar.left + (bar.right - bar.left) * 12 / 100;
        text.right = bar.left + (bar.right - bar.left) * 55 / 100;
        playback_log(x->p, "clicking the address bar");
        if (!(result = click_rect(x, &text, NULL))) return 0;
        if (result == 1) {
            /* Clicking a bar that already had focus only moves the caret; select its text so typing replaces it. */
            if (focused && !shortcut(x->p, VK_CONTROL, L'A')) return 0;
            return 1;
        }
    }
    playback_log(x->p, "address bar not clickable; using Ctrl+L");
    return shortcut(x->p, VK_CONTROL, L'L');
}
/* Type into the address bar: a URL opens directly, anything else is a Google search. */
static int address_bar(Session *x, const wchar_t *text) {
    char utf8[700];
    if (!focus_address_bar(x)) return 0;
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), NULL, NULL);
    playback_log(x->p, "typing in the address bar: %s", utf8);
    return playback_wait(x->p, motion_between(&x->rng, 250, 700)) &&
        keyboard_type(x->p, text) && playback_wait(x->p, motion_between(&x->rng, 200, 600)) &&
        keyboard_press(x->p, VK_RETURN) && playback_wait(x->p, motion_between(&x->rng, 2200, 3500));
}
static int scope_is_search(const Session *x) {
    wchar_t host[256];
    return !web_host(x->scope, host, 256) || wcsstr(host, L"google.") != NULL;
}
static unsigned *visit_count(Session *x, const wchar_t *url) {
    unsigned i;
    size_t n = wcscspn(url, L"#");
    if (n > 255) n = 255;
    for (i = 0; i < x->visited_count; ++i)
        if (wcslen(x->visited[i]) == n && !wcsncmp(x->visited[i], url, n)) return &x->visit_counts[i];
    if (x->visited_count == MAX_VISITED) return NULL;
    wmemcpy(x->visited[x->visited_count], url, n); x->visited[x->visited_count][n] = 0;
    x->visit_counts[x->visited_count] = 0;
    return &x->visit_counts[x->visited_count++];
}
static void arrive(Session *x) {
    unsigned *count = visit_count(x, x->page->url);
    char utf8[1100];
    if (count) ++*count;
    ++x->pages; x->stalls = 0;
    WideCharToMultiByte(CP_UTF8, 0, x->page->url, -1, utf8, sizeof(utf8), NULL, NULL);
    playback_log(x->p, "page: %s", utf8);
    if (x->page->unreadable)
        playback_log(x->p, "this page is not readable through accessibility; reading and scrolling without clicking links");
}
/* Same-site links that do not sign in, buy, download, or change anything; less-visited pages first. */
static int pick_link(Session *x) {
    double weights[WEB_MAX_LINKS], total = 0, roll;
    unsigned i;
    if (scope_is_search(x)) return -1;
    for (i = 0; i < x->page->link_count; ++i) {
        const WebLink *link = &x->page->links[i];
        unsigned *seen;
        weights[i] = 0;
        if (wcslen(link->name) < 2 || !web_name_safe(link->name) || !web_url_allowed(link->url) ||
            !web_same_site(x->scope, link->url) || web_same_page(link->url, x->page->url)) continue;
        seen = visit_count(x, link->url);
        weights[i] = 1.0 / (1 + (seen ? *seen : 0));
        weights[i] *= weights[i];
        total += weights[i];
    }
    if (total <= 0) return -1;
    roll = motion_unit(&x->rng) * total;
    for (i = 0; i < x->page->link_count; ++i) {
        if (roll < weights[i]) return (int)i;
        roll -= weights[i];
    }
    return -1;
}
static int click_rect(Session *x, const RECT *target, const wchar_t *link_name) {
    RECT region = *target;
    POINT point, actual;
    if (link_name) IntersectRect(&region, target, &x->page->document);
    if (!motion_click_point(&x->rng, &region, &point) || !in_browser(x, point)) return 2;
    if (!move_to(x, point, MOTION_AIM, (double)(region.right - region.left < region.bottom - region.top ?
            region.right - region.left : region.bottom - region.top))) return 0;
    if (!playback_wait(x->p, motion_chance(&x->rng, 88) ? motion_between(&x->rng, 70, 280) : motion_between(&x->rng, 400, 900)))
        return 0;
    if (!GetCursorPos(&actual) || labs(actual.x - point.x) > 2 || labs(actual.y - point.y) > 2) {
        playback_log(x->p, "the pointer moved before the click; stopping"); return 0;
    }
    /* Check that the link itself, not an overlay or banner, is under the pointer. */
    if (link_name && !web_link_at(x->p, actual, link_name)) return 2;
    if (!mouse_click(x->p, 0, 0)) return 0;
    ++x->clicks;
    return 1;
}
/* Returns 1 after a click (navigated or not), 2 when the target was skipped, 0 to stop. */
static int click_link(Session *x, int index) {
    WebLink link = x->page->links[index];
    wchar_t before[1024];
    uint32_t signature = x->page->signature;
    unsigned tabs = x->page->tab_count, attempt;
    char name[480];
    int result;
    WideCharToMultiByte(CP_UTF8, 0, link.name, -1, name, sizeof(name), NULL, NULL);
    wcscpy(before, x->page->url);
    playback_log(x->p, "clicking link \"%s\"", name);
    result = click_rect(x, &link.rect, link.name);
    if (result != 1) {
        unsigned *seen = visit_count(x, link.url);
        if (seen) ++*seen; /* make a covered link less likely next time */
        if (result == 2) playback_log(x->p, "link skipped: covered, moved, or outside the page");
        return result;
    }
    if (!relax(x, "after-click", 78)) return 0;
    for (attempt = 0; attempt < 7; ++attempt) {
        if (!hold(x, motion_between(&x->rng, 450, 900)) || !snapshot(x)) return 0;
        if (x->page->tab_count > tabs) {
            playback_log(x->p, "result: the link opened a new tab");
            ++x->tabs_open;
            break;
        }
        if (!web_same_page(before, x->page->url)) {
            playback_log(x->p, "result: navigation");
            ++x->depth; arrive(x); return 1;
        }
        if (x->page->signature != signature) { playback_log(x->p, "result: page content changed"); return 1; }
    }
    if (attempt == 7) playback_log(x->p, "result: no observable change");
    return 1;
}
static int dismiss_overlay(Session *x) {
    playback_log(x->p, "a dialog or overlay is open; pressing Escape");
    return keyboard_press(x->p, VK_ESCAPE) && hold(x, motion_between(&x->rng, 600, 1400)) && snapshot(x);
}
static int bookmark(Session *x) {
    unsigned i;
    for (i = 0; i < x->bookmark_count; ++i) if (web_same_page(x->bookmarked[i], x->page->url)) return 1;
    playback_log(x->p, "bookmarking this page (Ctrl+D, then Enter for Done)");
    if (!shortcut(x->p, VK_CONTROL, L'D') || !playback_wait(x->p, motion_between(&x->rng, 1200, 2200)) ||
        !keyboard_press(x->p, VK_RETURN) || !playback_wait(x->p, 700)) return 0;
    if (x->bookmark_count < 16) wcsncpy(x->bookmarked[x->bookmark_count++], x->page->url, 511);
    ++x->bookmarks;
    return 1;
}
/* Open one organic Google result; returns 2 when none is suitable and the results page is read instead. */
static int open_result(Session *x) {
    UiWebLink links[32];
    unsigned count = 0, pick;
    char name[480];
    POINT point;
    if (!ui_web_links(x->p, links, 32, &count)) return 0;
    if (!count) { playback_log(x->p, "no organic search result in view; reading the results"); return 2; }
    pick = motion_between(&x->rng, 0, (count < 5 ? count : 5) - 1);
    WideCharToMultiByte(CP_UTF8, 0, links[pick].name, -1, name, sizeof(name), NULL, NULL);
    playback_log(x->p, "opening search result %u: %s", pick + 1, name);
    if (!motion_click_point(&x->rng, &links[pick].rect, &point) || !in_browser(x, point)) return 2;
    if (!move_to(x, point, MOTION_AIM, links[pick].rect.bottom - links[pick].rect.top) ||
        !playback_wait(x->p, motion_between(&x->rng, 90, 320)) || !mouse_click(x->p, 0, 0)) return 0;
    ++x->clicks;
    return hold(x, motion_between(&x->rng, 2500, 4000)) && snapshot(x) ? 1 : 0;
}
/* Fill the current tab from a plan entry. */
static int open_opener(Session *x, const BrowseOpener *o, int typed) {
    if (!typed && !address_bar(x, o->text)) return 0;
    if (!snapshot(x)) return 0;
    if (o->search) {
        int result;
        if (x->page->overlay_count && !dismiss_overlay(x)) return 0;
        if (!scroll(x)) return 0;
        if (!(result = open_result(x))) return 0;
        x->depth = 0;
    }
    wcscpy(x->scope, x->page->url);
    arrive(x);
    return 1;
}
/* A quick search in a new tab, then back to the main browsing. */
static int side_search(Session *x, const BrowseOpener *o) {
    unsigned seconds = motion_between(&x->rng, x->s->catalog->side_search_seconds[0], x->s->catalog->side_search_seconds[1]);
    ULONGLONG start = GetTickCount64(), paused = x->p->paused_ms;
    wchar_t scope[1024];
    ++x->side_searches;
    playback_log(x->p, "side search in a new tab for about %u seconds", seconds);
    wcscpy(scope, x->scope);
    if (!shortcut(x->p, VK_CONTROL, L'T') || !playback_wait(x->p, motion_between(&x->rng, 600, 1200)) ||
        !address_bar(x, o->text) || !snapshot(x)) return 0;
    while (since(x, start, paused) < seconds * 1000 && session_left(x)) {
        if (motion_chance(&x->rng, 70) ? !scroll(x) : !read_pause(x)) return 0;
    }
    playback_log(x->p, "closing the side-search tab and returning to the main browsing");
    if (!shortcut(x->p, VK_CONTROL, L'W') || !playback_wait(x->p, motion_between(&x->rng, 700, 1300)) || !snapshot(x)) return 0;
    wcscpy(x->scope, scope);
    return 1;
}
static int switch_tab(Session *x) {
    unsigned candidates[WEB_MAX_TABS], count = 0, i;
    if (!snapshot(x)) return 0;
    for (i = 0; i < x->page->tab_count; ++i) if (!x->page->tabs[i].selected) candidates[count++] = i;
    if (!count) return 1;
    if (motion_chance(&x->rng, 65)) {
        WebTab tab = x->page->tabs[candidates[motion_between(&x->rng, 0, count - 1)]];
        char name[480];
        WideCharToMultiByte(CP_UTF8, 0, tab.name, -1, name, sizeof(name), NULL, NULL);
        playback_log(x->p, "switching tabs with the mouse: %s", name);
        if (!click_rect(x, &tab.rect, NULL)) return 0;
    } else {
        playback_log(x->p, "switching tabs with Ctrl+Tab");
        if (!shortcut(x->p, VK_CONTROL, VK_TAB)) return 0;
    }
    if (!hold(x, motion_between(&x->rng, 700, 1500)) || !snapshot(x)) return 0;
    wcscpy(x->scope, x->page->url);
    x->depth = 0;
    arrive(x);
    return 1;
}
/* Read one tab for its budget: scroll, read, wander, idle, follow links, and maybe a side search or bookmark. */
static int visit(Session *x, DWORD budget_ms, const BrowseOpener *side, int want_bookmark) {
    const BrowseCatalog *c = x->s->catalog;
    ULONGLONG start = GetTickCount64(), paused = x->p->paused_ms, page_start = start, page_paused = paused;
    DWORD dwell = motion_between(&x->rng, c->page_seconds[0], c->page_seconds[1]) * 1000;
    DWORD side_at = side ? budget_ms * motion_between(&x->rng, 30, 70) / 100 : 0;
    DWORD mark_at = want_bookmark ? budget_ms * motion_between(&x->rng, 15, 60) / 100 : 0;
    unsigned overlays = 0;
    playback_log(x->p, "visiting this tab for about %.0f seconds", budget_ms / 1000.0);
    if (!relax(x, "page-arrival", 85)) return 0;
    while (since(x, start, paused) < budget_ms && session_left(x)) {
        DWORD elapsed = since(x, start, paused);
        unsigned roll;
        if (x->page->overlay_count && overlays < 2) {
            ++overlays;
            if (!dismiss_overlay(x)) return 0;
            continue;
        }
        if (side && elapsed >= side_at) {
            if (!side_search(x, side)) return 0;
            side = NULL; continue;
        }
        if (want_bookmark && elapsed >= mark_at) {
            if (!bookmark(x)) return 0;
            want_bookmark = 0; continue;
        }
        if (motion_chance(&x->rng, c->idle_percent)) { if (!idle(x)) return 0; continue; }
        if (since(x, page_start, page_paused) >= dwell) {
            int link = pick_link(x), result = 2;
            if (link >= 0 && !(result = click_link(x, link))) return 0;
            if (result == 1) {
                page_start = GetTickCount64(); page_paused = x->p->paused_ms;
                dwell = motion_between(&x->rng, c->page_seconds[0], c->page_seconds[1]) * 1000;
                overlays = 0;
                continue;
            }
            if (x->depth && motion_chance(&x->rng, 30)) {
                playback_log(x->p, "going back (Alt+Left)");
                if (!shortcut(x->p, VK_MENU, VK_LEFT) || !hold(x, motion_between(&x->rng, 1500, 2600)) || !snapshot(x)) return 0;
                --x->depth; arrive(x);
                page_start = GetTickCount64(); page_paused = x->p->paused_ms;
                continue;
            }
        }
        /* After fast scanning, mostly keep scrolling; after slow reading, pause more. */
        roll = motion_between(&x->rng, 0, 99);
        if (x->scroll.last_fast ? roll < 58 : roll < 34) { if (!scroll(x)) return 0; }
        else if (x->scroll.last_fast ? roll < 82 : roll < 80) { if (!read_pause(x)) return 0; }
        else { if (!wander(x)) return 0; }
    }
    return 1;
}

/* ---------- session and recovery ---------- */

/* A Chrome window left open earlier (by an earlier browsing slot, say), if any. */
static BOOL CALLBACK find_chrome(HWND w, LPARAM parameter) {
    wchar_t cls[64], title[256];
    size_t length;
    if (!IsWindowVisible(w) || GetWindow(w, GW_OWNER) || !GetClassNameW(w, cls, 64) || wcscmp(cls, L"Chrome_WidgetWin_1") ||
        !GetWindowTextW(w, title, 256) || (length = wcslen(title)) < 13 || wcscmp(title + length - 13, L"Google Chrome") ||
        !window_process_is(w, L"chrome.exe")) return TRUE;
    *(HWND *)parameter = w;
    return FALSE;
}
/* Open a tab and fill it: in the Chrome window already open (a new tab), or a new browser window
   when none is open, otherwise Ctrl+T. */
static int start_tab(Session *x, const BrowseOpener *o) {
    Playback *p = x->p;
    if (!x->window || !IsWindow(x->window)) {
        HWND open = NULL;
        x->window = NULL; x->tabs_open = 0;
        /* Until the browser opens, the guarded target is whatever has focus now. */
        window_fix_target(p);
        if (!p->target) { playback_log(p, "no window has focus; click the console window and start again"); return 0; }
        if (x->s->via_taskbar) EnumWindows(find_chrome, (LPARAM)&open);
        if (open && taskbar_show_window(p, &x->rng, L"Google Chrome", open)) {
            /* Back to the browser that is still open: a new tab there, like a person would. */
            x->window = open;
            playback_log(p, "Chrome is already open; opening a new tab (Ctrl+T)");
            if (!shortcut(p, VK_CONTROL, L'T') || !playback_wait(p, motion_between(&x->rng, 600, 1200))) return 0;
            x->tabs_open = 1;
        } else {
            if (open && InterlockedCompareExchange(&p->user_stop, 0, 0)) return 0;
            /* Open Chrome without typing anything, then fill the tab through a clicked address bar. */
            if (!browser_open_window(p, NULL, x->s->via_taskbar, &x->window)) return 0;
            x->tabs_open = 1;
        }
    } else {
        playback_log(p, "opening a new tab (Ctrl+T)");
        if (!shortcut(p, VK_CONTROL, L'T') || !playback_wait(p, motion_between(&x->rng, 600, 1200))) return 0;
        ++x->tabs_open;
    }
    x->depth = 0;
    return open_opener(x, o, 0);
}
/* After an error: clear the stop unless the user asked for it, and wait out a locked desktop. */
static int recovery_allowed(Session *x) {
    Playback *p = x->p;
    if (InterlockedCompareExchange(&p->user_stop, 0, 0) || !session_left(x)) return 0;
    playback_release(p);
    while (!window_desktop_ready()) {
        if (InterlockedCompareExchange(&p->user_stop, 0, 0) || !session_left(x)) return 0;
        Sleep(1000);
    }
    InterlockedExchange(&p->stopped, 0);
    return 1;
}
/* Bring the browser back to the front: activate it, or Alt+Tab to it like a person would. */
static int refocus(Session *x) {
    Playback *p = x->p;
    HWND foreground;
    if (!x->window || !IsWindow(x->window)) return 0;
    if (IsIconic(x->window)) ShowWindow(x->window, SW_RESTORE);
    foreground = GetForegroundWindow();
    if (foreground != x->window) {
        SetForegroundWindow(x->window);
        if (!playback_wait(p, 300)) return 0;
    }
    if (GetForegroundWindow() != x->window && foreground) {
        playback_log(p, "recovery: Alt+Tab back to the browser");
        p->target = foreground;
        if (!shortcut(p, VK_MENU, VK_TAB) || !playback_wait(p, 500)) return 0;
    }
    if (GetForegroundWindow() != x->window) { playback_log(p, "recovery: the browser could not be brought to the front"); return 0; }
    p->target = x->window;
    /* Close any menu, bubble, or dialog the error left open. */
    return keyboard_press(p, VK_ESCAPE) && playback_wait(p, 400);
}
static void close_browser(Session *x) {
    Playback *p = x->p;
    unsigned waited;
    if (x->window && IsWindow(x->window)) {
        playback_log(p, "recovery: closing Chrome");
        if (GetForegroundWindow() == x->window) {
            WORD keys[2] = {VK_MENU, VK_F4};
            p->target = x->window;
            keyboard_shortcut(p, keys, 2);
        }
        if (IsWindow(x->window)) PostMessageW(x->window, WM_CLOSE, 0, 0);
        for (waited = 0; waited < 50 && IsWindow(x->window); ++waited) Sleep(200);
        if (IsWindow(x->window)) playback_log(p, "recovery: Chrome did not close; opening a fresh window instead");
    }
    InterlockedExchange(&p->stopped, 0);
    x->window = NULL; x->tabs_open = 0;
    p->target = GetForegroundWindow();
}
/* Level 1: a new tab with a random site or search. Level 2 and later: close Chrome and reopen it. */
static int recover(Session *x, unsigned level) {
    BrowseOpener o;
    if (!browse_pick_opener(x->s->catalog, x->s->profile, &x->rng, &o)) return 0;
    if (level == 1 && refocus(x)) {
        playback_log(x->p, "recovery: starting over in a new tab with a random %s", o.search ? "search" : "website");
        if (start_tab(x, &o)) return 1;
        if (!recovery_allowed(x)) return 0;
    }
    close_browser(x);
    playback_log(x->p, "recovery: reopening Chrome with a random %s", o.search ? "search" : "website");
    return start_tab(x, &o);
}

#define MAX_RECOVERIES 5
int browser_session_run(Playback *p, const BrowseSession *s) {
    Session *x = calloc(1, sizeof(*x));
    BrowsePlan plan;
    unsigned filled = 0, failures = 0, recoveries = 0, i;
    int ok = 0, stay = 0;
    if (!x || !(x->page = calloc(1, sizeof(*x->page)))) { free(x); playback_log(p, "out of memory"); return 0; }
    p->typing_speed = s->profile->typing_speed;
    x->p = p; x->s = s;
    browser_session_plan(s, &plan);
    browser_session_print_plan(s, &plan);
    motion_seed(&x->rng, s->seed * 2654435761u + 1);
    motion_scroll_init(&x->scroll, &x->rng);
    /* Screen coordinates throughout; the browser window is the guarded target. */
    p->client_coords = 0;
    /* Address-bar typing is quicker than code typing, without long breaks. */
    typing_pace_configure(&p->typing_pace, 70, 190, 400, 900, s->seed ^ 0x5bd1e995u);
    typing_pace_breaks(&p->typing_pace, 0, 0, 0, 0);
    /* Searches and addresses get the profile's typing mistakes too. */
    p->typo_percent = profile_mistype_percent(s->profile);
    p->typo_state = s->seed ^ 0x9e3779b9u;
    motion_hand_for(&p->hand, motion_hash(s->profile->local_id), s->profile->randomness_score);
    x->started = GetTickCount64(); x->paused = p->paused_ms;
    if (s->apps) x->has_apps = 1;
    else if (s->side_apps) { side_apps_timer_init(&x->apps, s->side_apps, s->profile, s->seed, p); x->has_apps = 1; }
    if (!plan.tab_count) { playback_log(p, "no tabs could be planned from the browsing catalog"); goto done; }
    while (session_left(x)) {
        DWORD left = session_left(x), budget;
        int step;
        if (stay) {
            /* Read the tab that recovery just opened before moving on. */
            stay = 0;
            budget = motion_between(&x->rng, s->catalog->tab_visit_seconds[0], s->catalog->tab_visit_seconds[1]) * 1000;
            step = visit(x, budget, NULL, 0);
        } else if (filled < plan.tab_count) {
            /* Counted before it runs, so a tab that keeps failing is not retried forever. */
            i = filled++;
            budget = plan.visit_seconds[i] * 1000;
            if (budget > left / (plan.tab_count - i)) budget = left / (plan.tab_count - i);
            if (i) playback_log(p, "filling tab %u of %u", i + 1, plan.tab_count);
            step = start_tab(x, &plan.openers[i]) &&
                visit(x, budget, plan.side_search[i] ? &plan.side[i] : NULL, plan.bookmark[i]);
        } else {
            BrowseOpener side;
            int with_side = motion_chance(&x->rng, s->catalog->new_tab_search_percent) &&
                browse_pick_search(s->catalog, s->profile, &x->rng, &side.kind, side.text, 300);
            budget = motion_between(&x->rng, s->catalog->tab_visit_seconds[0], s->catalog->tab_visit_seconds[1]) * 1000;
            step = (x->tabs_open <= 1 || switch_tab(x)) &&
                visit(x, budget, with_side ? &side : NULL, motion_chance(&x->rng, s->catalog->bookmark_percent));
        }
        if (step && x->has_apps) {
            int took;
            /* Maybe a side-app break; back in the browser, read the page again. */
            step = side_apps_tick(p, s->apps ? s->apps : &x->apps, &took) && (!took || snapshot(x));
        }
        if (step) { failures = 0; continue; }
        /* Something went wrong (a page that would not load, lost focus, a popup...). Recover unless
           the user stopped playback; give up only after repeated failures in a row. */
        while (!step) {
            if (!recovery_allowed(x)) goto finished;
            if (++failures > MAX_RECOVERIES) {
                playback_log(p, "recovery failed %u times in a row; stopping", MAX_RECOVERIES); goto done;
            }
            ++recoveries;
            playback_log(p, "error while browsing; recovery attempt %u of %u", failures, MAX_RECOVERIES);
            step = recover(x, failures);
        }
        stay = 1;
    }
finished:
    if (InterlockedCompareExchange(&p->user_stop, 0, 0) || InterlockedCompareExchange(&p->stopped, 0, 0)) goto done;
    /* How tabs are closed is still an open decision (TRACKER.md D8): close some, keep the browser open. */
    if (x->tabs_open > 1 && refocus(x)) {
        unsigned close = motion_between(&x->rng, 0, x->tabs_open - 1);
        playback_log(p, "time is up; closing %u of %u tab(s) and leaving the browser open", close, x->tabs_open);
        for (i = 0; i < close; ++i)
            if (!shortcut(p, VK_CONTROL, L'W') || !playback_wait(p, motion_between(&x->rng, 500, 1100))) goto done;
    } else playback_log(p, "time is up; leaving the browser open");
    ok = 1;
done:
    playback_log(p, "browsing summary: %u pages, %u clicks, %u scroll passes, %u idles, %u random mouse movements, "
        "%u side searches, %u bookmarks, %u recoveries", x->pages, x->clicks, x->scrolls, x->idles, x->wanders,
        x->side_searches, x->bookmarks, recoveries);
    free(x->page); free(x);
    return ok;
}

