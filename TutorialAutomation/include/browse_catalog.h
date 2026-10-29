#ifndef BROWSE_CATALOG_H
#define BROWSE_CATALOG_H
#include "profile.h"
#include "human_motion.h"
/* Sites, search templates, and browsing settings from profiles/browsing.json. */
typedef enum {
    BROWSE_QUESTION, BROWSE_PHRASE, BROWSE_SONG_LYRICS, BROWSE_APPS, BROWSE_LOGGY3, BROWSE_SEARCH_KIND_COUNT
} BrowseSearchKind;
#define BROWSE_MAX_TOPICS 8
#define BROWSE_MAX_TEMPLATES 16
#define BROWSE_MAX_SONGS 64
typedef struct {
    wchar_t *url, *category;
    wchar_t *topics[BROWSE_MAX_TOPICS];
    size_t topic_count;
} BrowseSite;
typedef struct {
    unsigned tabs[2], direct_site_percent, new_tab_search_percent, bookmark_percent, idle_percent;
    unsigned idle_seconds[2], page_seconds[2], tab_visit_seconds[2], side_search_seconds[2];
    unsigned weights[BROWSE_SEARCH_KIND_COUNT];
    wchar_t *templates[BROWSE_SEARCH_KIND_COUNT][BROWSE_MAX_TEMPLATES];
    size_t template_count[BROWSE_SEARCH_KIND_COUNT];
    wchar_t *songs[BROWSE_MAX_SONGS];
    size_t song_count;
    BrowseSite *sites;
    size_t site_count;
} BrowseCatalog;
/* How a tab is filled: a site typed directly, or a Google search followed by a result. */
typedef struct {
    int search;
    BrowseSearchKind kind;
    const BrowseSite *site;
    wchar_t text[300];
} BrowseOpener;
int browse_catalog_load(const wchar_t *path, BrowseCatalog *catalog, char *error, size_t capacity);
int browse_catalog_parse(const wchar_t *text, BrowseCatalog *catalog, char *error, size_t capacity);
void browse_catalog_free(BrowseCatalog *catalog);
/* Sites whose topics match the profile's interests or hobbies are four times as likely per match. */
unsigned browse_site_weight(const BrowseSite *site, const Profile *profile);
const BrowseSite *browse_pick_site(const BrowseCatalog *catalog, const Profile *profile, MotionRng *rng);
int browse_pick_search(const BrowseCatalog *catalog, const Profile *profile, MotionRng *rng,
                       BrowseSearchKind *kind, wchar_t *query, size_t capacity);
int browse_pick_opener(const BrowseCatalog *catalog, const Profile *profile, MotionRng *rng, BrowseOpener *opener);
const char *browse_search_kind_name(BrowseSearchKind kind);
#endif
