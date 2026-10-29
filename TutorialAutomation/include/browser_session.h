#ifndef BROWSER_SESSION_H
#define BROWSER_SESSION_H
#include "playback.h"
#include "browse_catalog.h"
/* A profile-driven browsing session: 1-5 tabs filled by direct sites or Google searches,
   then reading, scrolling, same-site link clicks, random idle, random mouse movement,
   side searches in a new tab, and bookmarks. */
typedef struct BrowseSession {
    const Profile *profile;
    const BrowseCatalog *catalog;
    DWORD duration_ms;
    uint32_t seed;
    int via_taskbar;
    const struct SideAppsConfig *side_apps; /* optional side-app breaks; NULL for none */
    struct SideAppsTimer *apps;             /* optional: a timer shared with other tasks */
} BrowseSession;
#define BROWSE_MAX_TABS 8
/* Decisions drawn up front from the seed, so a dry run shows what a live run will open. */
typedef struct {
    unsigned tab_count;
    BrowseOpener openers[BROWSE_MAX_TABS];
    unsigned visit_seconds[BROWSE_MAX_TABS];
    int side_search[BROWSE_MAX_TABS], bookmark[BROWSE_MAX_TABS];
    BrowseOpener side[BROWSE_MAX_TABS];
} BrowsePlan;
void browser_session_plan(const BrowseSession *session, BrowsePlan *plan);
void browser_session_print_plan(const BrowseSession *session, const BrowsePlan *plan);
int browser_session_run(Playback *p, const BrowseSession *session);
#endif
