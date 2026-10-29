#include "browse_catalog.h"
#include "json_config.h"
#include "script_engine.h"
#include "web_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

static const wchar_t *const kind_keys[BROWSE_SEARCH_KIND_COUNT] = {
    L"question", L"phrase", L"song_lyrics", L"apps", L"loggy3"
};
static const wchar_t *const categories[] = {
    L"programming_docs", L"programming_blog", L"github", L"tech_news", L"food", L"lifestyle", L"travel",
    L"books", L"entertainment", L"news", L"education", L"social_media"
};

static int fail(char *error, size_t cap, const char *message) { snprintf(error, cap, "%s", message); return 0; }
static int line_text(const JsonValue *v, size_t max) {
    return v && v->kind == JSON_STRING && *v->string && wcslen(v->string) <= max && !wcspbrk(v->string, L"\r\n\t");
}
static int range(const JsonValue *parent, const wchar_t *key, unsigned lo, unsigned hi, unsigned out[2]) {
    const JsonValue *v = json_get(parent, key), *a;
    if (!v || v->kind != JSON_ARRAY || !(a = v->child) || !a->next || a->next->next ||
        a->kind != JSON_NUMBER || a->next->kind != JSON_NUMBER) return 0;
    out[0] = a->number; out[1] = a->next->number;
    return out[0] >= lo && out[1] <= hi && out[0] <= out[1];
}
static int percent(const JsonValue *parent, const wchar_t *key, unsigned *out) {
    const JsonValue *v = json_get(parent, key);
    if (!v || v->kind != JSON_NUMBER || v->number > 100) return 0;
    *out = v->number; return 1;
}
/* Templates may use {interest} and {song} only. */
static int template_valid(const wchar_t *text, int *needs_song) {
    const wchar_t *at = text;
    while (*at) {
        if (!wcsncmp(at, L"{interest}", 10)) at += 10;
        else if (!wcsncmp(at, L"{song}", 6)) { *needs_song = 1; at += 6; }
        else if (*at == L'{' || *at == L'}') return 0;
        else ++at;
    }
    return 1;
}
static int load_settings(BrowseCatalog *c, const JsonValue *s) {
    static const wchar_t *const keys[] = {L"tabs", L"direct_site_percent", L"new_tab_search_percent", L"bookmark_percent",
        L"idle_percent", L"idle_seconds", L"page_seconds", L"tab_visit_seconds", L"side_search_seconds"};
    return s && json_keys(s, keys, sizeof(keys)/sizeof(*keys)) &&
        range(s, L"tabs", 1, 8, c->tabs) && percent(s, L"direct_site_percent", &c->direct_site_percent) &&
        percent(s, L"new_tab_search_percent", &c->new_tab_search_percent) &&
        percent(s, L"bookmark_percent", &c->bookmark_percent) && percent(s, L"idle_percent", &c->idle_percent) &&
        range(s, L"idle_seconds", 1, 600, c->idle_seconds) && range(s, L"page_seconds", 3, 600, c->page_seconds) &&
        range(s, L"tab_visit_seconds", 10, 3600, c->tab_visit_seconds) &&
        range(s, L"side_search_seconds", 5, 600, c->side_search_seconds);
}
static int load_site(BrowseSite *site, const JsonValue *v, char *error, size_t cap, size_t index) {
    static const wchar_t *const keys[] = {L"url", L"category", L"topics"};
    const JsonValue *url = json_get(v, L"url"), *category = json_get(v, L"category"), *topics = json_get(v, L"topics"), *t;
    size_t k;
    int known = 0;
    if (!json_keys(v, keys, 3) || !line_text(url, 219) || !web_url_allowed(url->string)) {
        snprintf(error, cap, "site %zu: url must be an allowed HTTP(S) address under 220 characters", index+1); return 0;
    }
    if (!line_text(category, 40)) { snprintf(error, cap, "site %zu: category is required", index+1); return 0; }
    for (k = 0; k < sizeof(categories)/sizeof(*categories); ++k) known |= !wcscmp(category->string, categories[k]);
    if (!known) {
        snprintf(error, cap, "site %zu: category must be programming_docs, programming_blog, github, tech_news, food, "
            "lifestyle, travel, books, entertainment, news, education or social_media", index+1);
        return 0;
    }
    if (!(site->url = _wcsdup(url->string)) || !(site->category = _wcsdup(category->string))) return fail(error, cap, "out of memory");
    if (!topics || topics->kind != JSON_ARRAY || !topics->child) {
        snprintf(error, cap, "site %zu: topics must list 1-%d entries", index+1, BROWSE_MAX_TOPICS); return 0;
    }
    for (t = topics->child; t; t = t->next) {
        if (site->topic_count == BROWSE_MAX_TOPICS || !line_text(t, 60)) {
            snprintf(error, cap, "site %zu: topics must list 1-%d short entries", index+1, BROWSE_MAX_TOPICS); return 0;
        }
        if (!(site->topics[site->topic_count++] = _wcsdup(t->string))) return fail(error, cap, "out of memory");
    }
    return 1;
}
int browse_catalog_parse(const wchar_t *text, BrowseCatalog *c, char *error, size_t cap) {
    static const wchar_t *const keys[] = {L"version", L"settings", L"search_weights", L"searches", L"songs", L"sites"};
    JsonValue *root = NULL;
    const JsonValue *version, *weights, *searches, *songs, *sites, *v;
    size_t i, count = 0;
    unsigned total = 0;
    int needs_song = 0;
    memset(c, 0, sizeof(*c));
    if (!cap) return 0;
    *error = 0;
    if (!json_parse(text, &root, error, cap)) return 0;
    version = json_get(root, L"version"); weights = json_get(root, L"search_weights");
    searches = json_get(root, L"searches"); songs = json_get(root, L"songs"); sites = json_get(root, L"sites");
    if (!json_keys(root, keys, sizeof(keys)/sizeof(*keys)) || !version || version->kind != JSON_NUMBER || version->number != 1) {
        fail(error, cap, "browsing file needs \"version\": 1 and only documented sections"); goto fail;
    }
    if (!load_settings(c, json_get(root, L"settings"))) {
        fail(error, cap, "settings: tabs 1-8, percents 0-100, and [min, max] second ranges are required"); goto fail;
    }
    if (!json_keys(weights, kind_keys, BROWSE_SEARCH_KIND_COUNT) || !json_keys(searches, kind_keys, BROWSE_SEARCH_KIND_COUNT)) {
        fail(error, cap, "search kinds are question, phrase, song_lyrics, apps and loggy3"); goto fail;
    }
    for (i = 0; i < BROWSE_SEARCH_KIND_COUNT; ++i) {
        const JsonValue *w = json_get(weights, kind_keys[i]), *list = json_get(searches, kind_keys[i]);
        if (w && (w->kind != JSON_NUMBER || w->number > 100)) { fail(error, cap, "search weights must be 0-100"); goto fail; }
        c->weights[i] = w ? w->number : 0;
        if (!c->weights[i]) continue;
        if (!list || list->kind != JSON_ARRAY || !list->child) {
            fail(error, cap, "every search kind with a weight needs at least one template"); goto fail;
        }
        for (v = list->child; v; v = v->next) {
            if (c->template_count[i] == BROWSE_MAX_TEMPLATES || !line_text(v, 150) || !template_valid(v->string, &needs_song)) {
                fail(error, cap, "search templates are short lines using only {interest} and {song}"); goto fail;
            }
            if (!(c->templates[i][c->template_count[i]++] = _wcsdup(v->string))) { fail(error, cap, "out of memory"); goto fail; }
        }
        total += c->weights[i];
    }
    if (!total) { fail(error, cap, "at least one search kind needs a weight above 0"); goto fail; }
    if (songs) {
        if (songs->kind != JSON_ARRAY) { fail(error, cap, "songs must be a list"); goto fail; }
        for (v = songs->child; v; v = v->next) {
            if (c->song_count == BROWSE_MAX_SONGS || !line_text(v, 80)) { fail(error, cap, "songs must list up to 64 short titles"); goto fail; }
            if (!(c->songs[c->song_count++] = _wcsdup(v->string))) { fail(error, cap, "out of memory"); goto fail; }
        }
    }
    if (needs_song && !c->song_count) { fail(error, cap, "a {song} template needs a songs list"); goto fail; }
    if (!sites || sites->kind != JSON_ARRAY || !sites->child) { fail(error, cap, "sites must be a non-empty list"); goto fail; }
    for (v = sites->child; v; v = v->next) if (++count > 500) { fail(error, cap, "at most 500 sites are supported"); goto fail; }
    if (!(c->sites = calloc(count, sizeof(*c->sites)))) { fail(error, cap, "out of memory"); goto fail; }
    for (v = sites->child, i = 0; v; v = v->next, ++i) {
        c->site_count = i + 1;
        if (!load_site(&c->sites[i], v, error, cap, i)) goto fail;
    }
    json_free(root); return 1;
fail:
    json_free(root); browse_catalog_free(c); return 0;
}
int browse_catalog_load(const wchar_t *path, BrowseCatalog *c, char *error, size_t cap) {
    wchar_t *text = NULL;
    int ok;
    memset(c, 0, sizeof(*c));
    if (!read_utf8(path, &text, error, cap)) return 0;
    ok = browse_catalog_parse(text, c, error, cap);
    free(text); return ok;
}
void browse_catalog_free(BrowseCatalog *c) {
    size_t i, j;
    if (!c) return;
    for (i = 0; i < BROWSE_SEARCH_KIND_COUNT; ++i) for (j = 0; j < c->template_count[i]; ++j) free(c->templates[i][j]);
    for (i = 0; i < c->song_count; ++i) free(c->songs[i]);
    for (i = 0; i < c->site_count; ++i) {
        free(c->sites[i].url); free(c->sites[i].category);
        for (j = 0; j < c->sites[i].topic_count; ++j) free(c->sites[i].topics[j]);
    }
    free(c->sites);
    memset(c, 0, sizeof(*c));
}

static int contains_ci(const wchar_t *text, const wchar_t *needle) {
    size_t n = wcslen(needle);
    for (; *text; ++text) if (!_wcsnicmp(text, needle, n)) return 1;
    return 0;
}
/* A topic matches an interest when either contains the other: "pc hardware" matches "PC hardware". */
static int topic_matches(const wchar_t *topic, const Profile *p) {
    size_t i;
    for (i = 0; i < p->interest_count; ++i)
        if (contains_ci(p->interests[i], topic) || contains_ci(topic, p->interests[i])) return 1;
    for (i = 0; i < p->hobby_count; ++i)
        if (contains_ci(p->hobbies[i], topic) || contains_ci(topic, p->hobbies[i])) return 1;
    return 0;
}
unsigned browse_site_weight(const BrowseSite *site, const Profile *profile) {
    unsigned weight = 1;
    size_t i;
    for (i = 0; i < site->topic_count; ++i) if (topic_matches(site->topics[i], profile)) weight += 4;
    /* Programmers still visit programming documentation even without listing it as an interest. */
    if (profile->main_task == PROFILE_TASK_PROGRAMMING &&
        (!wcscmp(site->category, L"programming_docs") || !wcscmp(site->category, L"programming_blog"))) weight += 2;
    return weight;
}
const BrowseSite *browse_pick_site(const BrowseCatalog *c, const Profile *p, MotionRng *rng) {
    unsigned total = 0, roll;
    size_t i;
    for (i = 0; i < c->site_count; ++i) total += browse_site_weight(&c->sites[i], p);
    if (!total) return NULL;
    roll = motion_between(rng, 0, total - 1);
    for (i = 0; i < c->site_count; ++i) {
        unsigned w = browse_site_weight(&c->sites[i], p);
        if (roll < w) return &c->sites[i];
        roll -= w;
    }
    return &c->sites[c->site_count - 1];
}
static const wchar_t *pick_interest(const Profile *p, MotionRng *rng) {
    size_t total = p->interest_count + p->hobby_count, pick;
    if (!total) return L"technology";
    pick = motion_between(rng, 0, (unsigned)total - 1);
    return pick < p->interest_count ? p->interests[pick] : p->hobbies[pick - p->interest_count];
}
int browse_pick_search(const BrowseCatalog *c, const Profile *p, MotionRng *rng, BrowseSearchKind *kind,
                       wchar_t *query, size_t cap) {
    unsigned total = 0, roll, i;
    const wchar_t *template, *at;
    size_t used = 0;
    for (i = 0; i < BROWSE_SEARCH_KIND_COUNT; ++i) total += c->weights[i];
    if (!total || !cap) return 0;
    roll = motion_between(rng, 0, total - 1);
    for (i = 0; i < BROWSE_SEARCH_KIND_COUNT - 1 && roll >= c->weights[i]; ++i) roll -= c->weights[i];
    *kind = (BrowseSearchKind)i;
    template = c->templates[i][motion_between(rng, 0, (unsigned)c->template_count[i] - 1)];
    for (at = template; *at; ) {
        const wchar_t *insert = NULL;
        size_t skip = 1, n;
        if (!wcsncmp(at, L"{interest}", 10)) { insert = pick_interest(p, rng); skip = 10; }
        else if (!wcsncmp(at, L"{song}", 6)) { insert = c->songs[motion_between(rng, 0, (unsigned)c->song_count - 1)]; skip = 6; }
        if (insert) {
            n = wcslen(insert);
            if (used + n >= cap) return 0;
            wmemcpy(query + used, insert, n); used += n;
        } else {
            if (used + 1 >= cap) return 0;
            query[used++] = *at;
        }
        at += skip;
    }
    query[used] = 0;
    return used > 0;
}
int browse_pick_opener(const BrowseCatalog *c, const Profile *p, MotionRng *rng, BrowseOpener *o) {
    memset(o, 0, sizeof(*o));
    if (motion_chance(rng, c->direct_site_percent)) {
        o->site = browse_pick_site(c, p, rng);
        if (!o->site) return 0;
        wcsncpy(o->text, o->site->url, 299);
        return 1;
    }
    o->search = 1;
    return browse_pick_search(c, p, rng, &o->kind, o->text, 300);
}
const char *browse_search_kind_name(BrowseSearchKind kind) {
    static const char *const names[BROWSE_SEARCH_KIND_COUNT] = {"question", "phrase", "song lyrics", "apps", "Loggy3"};
    return (unsigned)kind < BROWSE_SEARCH_KIND_COUNT ? names[kind] : "unknown";
}
