#include "human_motion.h"
#include "web_policy.h"
#include "browse_catalog.h"
#include "browser_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wchar.h>
static int checks, failures;
#define CHECK(t) do { ++checks; if (!(t)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#t); } } while (0)

static void path_tests(void) {
    MotionRng rng;
    POINT a = {100, 100}, b = {900, 500}, nearby = {110, 104}, at;
    MotionPath path, small;
    unsigned i;
    motion_seed(&rng, 7);
    motion_path(&rng, NULL, a, b, MOTION_AIM, 40, &path);
    motion_path_at(&path, 0, &at); CHECK(at.x == a.x && at.y == a.y);
    motion_path_at(&path, 1, &at); CHECK(at.x == b.x && at.y == b.y);
    CHECK(path.duration_ms >= 100 && path.duration_ms <= 1650);
    /* Both Bezier handles bend to the same side, so a trip between controls
       cannot make an unintentional S turn. */
    CHECK(((path.first[0] - a.x) * (b.y - a.y) -
           (path.first[1] - a.y) * (b.x - a.x)) *
          ((path.second[0] - a.x) * (b.y - a.y) -
           (path.second[1] - a.y) * (b.x - a.x)) >= 0);
    motion_path(&rng, NULL, a, nearby, MOTION_AIM, 40, &small);
    CHECK(small.duration_ms < path.duration_ms); /* short moves are quick */
    /* Curved, but never far off the straight line. */
    for (i = 1; i < 10; ++i) {
        double t = i / 10.0, cross;
        motion_path_at(&path, t, &at);
        cross = fabs((double)(b.x - a.x) * (at.y - a.y) - (double)(b.y - a.y) * (at.x - a.x)) / hypot(b.x - a.x, b.y - a.y);
        CHECK(cross <= 70);
    }
}
static unsigned move_time(const MotionMove *move) {
    unsigned i, total = move->pause_ms;
    for (i = 0; i < move->count; ++i) total += move->segments[i].duration_ms;
    return total;
}
static void control_move_tests(void) {
    MotionRng rng;
    MotionHand hand;
    MotionMove move;
    POINT start = {100, 100}, nearby_target = {120, 105}, middle = {250, 130}, far_target = {900, 500}, at;
    RECT screen = {0, 0, 1920, 1080};
    unsigned near_ms, middle_ms, far_ms, i, corrected = 0;
    motion_hand_for(&hand, motion_hash(L"WO-0001"), 35);
    motion_seed(&rng, 71);
    motion_control_move(&rng, &hand, start, nearby_target, 30, &screen, 1400, &move);
    near_ms = move_time(&move);
    CHECK(move.count == 1 && near_ms >= 100 && near_ms < 500);
    motion_path_at(&move.segments[0], 1, &at); CHECK(at.x == nearby_target.x && at.y == nearby_target.y);
    motion_control_move(&rng, &hand, nearby_target, middle, 30, &screen, 1400, &move);
    middle_ms = move_time(&move);
    CHECK(move.count == 1 && middle_ms > near_ms && middle_ms < 1400);
    motion_path_at(&move.segments[0], 0, &at); CHECK(at.x == nearby_target.x && at.y == nearby_target.y);
    motion_control_move(&rng, &hand, middle, far_target, 30, &screen, 1400, &move);
    far_ms = move_time(&move);
    CHECK(far_ms > middle_ms && far_ms <= 1400);
    for (i = 0; i < 300; ++i) {
        motion_control_move(&rng, &hand, middle, far_target, 30, &screen, 1400, &move);
        CHECK(move_time(&move) == far_ms);
        CHECK(move.count == 1 || move.count == 2);
        motion_path_at(&move.segments[0], 0, &at); CHECK(at.x == middle.x && at.y == middle.y);
        motion_path_at(&move.segments[move.count - 1], 1, &at); CHECK(at.x == far_target.x && at.y == far_target.y);
        if (move.count == 2) {
            ++corrected;
            CHECK(move.segments[0].duration_ms > move.segments[1].duration_ms);
            CHECK(move.segments[1].duration_ms >= 80 && move.pause_ms <= 1400 / 5);
        }
    }
    CHECK(corrected > 15);
    {
        MotionHand slow = hand, quick = hand;
        unsigned slow_ms;
        slow.speed = 0.9; quick.speed = 1.1;
        motion_control_move(&rng, &slow, middle, far_target, 30, &screen, 1400, &move);
        slow_ms = move_time(&move);
        motion_control_move(&rng, &quick, middle, far_target, 30, &screen, 1400, &move);
        CHECK(slow_ms > move_time(&move));
    }
}
static void hand_tests(void) {
    MotionRng rng;
    MotionHand one, again, two, plain = {0};
    POINT a = {100, 100}, b = {900, 500}, at;
    RECT screen = {0, 0, 1920, 1080}, edge = {0, 0, 902, 502};
    MotionPath path;
    MotionMove move;
    unsigned i, misses = 0, overshoots = 0, wobbly = 0;
    /* Each profile has its own steady hand; more randomness, more misses and wobble. */
    motion_hand_for(&one, motion_hash(L"WO-0001"), 35);
    motion_hand_for(&again, motion_hash(L"WO-0001"), 35);
    motion_hand_for(&two, motion_hash(L"WO-0002"), 55);
    CHECK(one.overshoot_percent == again.overshoot_percent &&
          one.overshoot_scale == again.overshoot_scale && one.wobble_px == again.wobble_px &&
          one.speed == again.speed);
    CHECK(one.overshoot_percent != two.overshoot_percent ||
          one.overshoot_scale != two.overshoot_scale || one.wobble_px != two.wobble_px ||
          one.speed != two.speed);
    CHECK(one.overshoot_percent >= 12 && one.overshoot_percent <= 36 && one.wobble_px > 0.7 && one.wobble_px < 3.3);
    CHECK(one.speed >= 0.9 && one.speed <= 1.12);
    motion_seed(&rng, 21);
    for (i = 0; i < 300; ++i) {
        unsigned k;
        double worst = 0;
        /* Wobble never moves the ends and stays a few pixels off the curve. */
        motion_path(&rng, &one, a, b, MOTION_AIM, 40, &path);
        motion_path_at(&path, 0, &at); CHECK(at.x == a.x && at.y == a.y);
        motion_path_at(&path, 1, &at); CHECK(at.x == b.x && at.y == b.y);
        CHECK(path.wobble > 0 && path.wobble <= one.wobble_px * 1.4 + 0.01);
        for (k = 1; k < 20; ++k) {
            MotionPath smooth = path;
            POINT plain_at;
            double off;
            smooth.wobble = 0;
            motion_path_at(&path, k / 20.0, &at); motion_path_at(&smooth, k / 20.0, &plain_at);
            off = hypot(at.x - plain_at.x, at.y - plain_at.y);
            if (off > worst) worst = off;
        }
        CHECK(worst <= one.wobble_px * 1.4 + 1.5);
        if (worst >= 1) ++wobbly;
        /* Overshoot: a miss near the target, then a short correction that ends on it. */
        motion_move(&rng, &one, a, b, MOTION_AIM, 40, &screen, &move);
        CHECK(move.count == 1 || move.count == 2);
        motion_path_at(&move.segments[move.count - 1], 1, &at); CHECK(at.x == b.x && at.y == b.y);
        if (move.count == 2) {
            POINT missed;
            double dist;
            ++misses;
            motion_path_at(&move.segments[0], 1, &missed);
            dist = hypot(missed.x - b.x, missed.y - b.y);
            CHECK(dist >= 5 && dist <= 60 && move.pause_ms >= 40 && move.pause_ms <= 180);
            CHECK(move.segments[1].duration_ms < move.segments[0].duration_ms);
            if ((missed.x - a.x) * (b.x - a.x) + (missed.y - a.y) * (b.y - a.y) > (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)) ++overshoots;
        }
        /* Resting moves and nearby targets are never corrected; a miss never leaves the bounds. */
        motion_move(&rng, &one, a, b, MOTION_REST, 160, &screen, &move); CHECK(move.count == 1);
        motion_move(&rng, &one, a, (POINT){150, 120}, MOTION_AIM, 40, &screen, &move); CHECK(move.count == 1);
        motion_move(&rng, &one, a, (POINT){290, 100}, MOTION_AIM, 40, &screen, &move); CHECK(move.count == 1);
        motion_move(&rng, &one, a, b, MOTION_AIM, 40, &edge, &move);
        if (move.count == 2) { motion_path_at(&move.segments[0], 1, &at); CHECK(PtInRect(&edge, at)); }
        motion_move(&rng, &plain, a, b, MOTION_AIM, 40, &screen, &move);
        CHECK(move.count == 1 && move.segments[0].wobble == 0);
    }
    CHECK(wobbly > 150);
    CHECK(misses > 300 * one.overshoot_percent / 200 && misses < 300 * one.overshoot_percent * 2 / 100);
    CHECK(overshoots > misses / 2);
}
static void point_tests(void) {
    MotionRng rng;
    RECT region = {200, 300, 1200, 900}, button = {10, 10, 50, 30}, tiny = {0, 0, 3, 3};
    POINT a, b;
    unsigned i, different = 0;
    motion_seed(&rng, 11);
    for (i = 0; i < 200; ++i) {
        CHECK(motion_click_point(&rng, &button, &a) && PtInRect(&button, a));
        CHECK(motion_rest_point(&rng, &region, &a));
        CHECK(a.x >= region.left + 250 && a.x <= region.left + 820 && a.y >= region.top + 132 && a.y <= region.top + 480);
        CHECK(motion_rest_point(&rng, &region, &b));
        different += a.x != b.x || a.y != b.y;
    }
    CHECK(different > 190);
    CHECK(!motion_click_point(&rng, &tiny, &a));
}
static void scroll_tests(void) {
    MotionRng rng;
    MotionScroll scroll;
    MotionScrollPass pass;
    unsigned i, b, fast = 0, slow = 0, up_at_end = 1;
    motion_seed(&rng, 3);
    motion_scroll_init(&scroll, &rng);
    for (i = 0; i < 300; ++i) {
        motion_scroll_plan(&scroll, 50, 0, &pass);
        CHECK(pass.count >= 2 && pass.count <= 4);
        for (b = 0; b < pass.count; ++b) {
            unsigned n = motion_burst_notches(&pass.bursts[b]);
            if (pass.bursts[b].fast) { ++fast; CHECK(n >= 9 && n <= 20); }
            else { ++slow; CHECK(n >= 1 && n <= 3); }
        }
        CHECK(pass.bursts[0].fast != pass.bursts[1].fast); /* every pass mixes speeds */
    }
    CHECK(fast > 100 && slow > 100);
    for (i = 0; i < 50; ++i) {
        motion_scroll_plan(&scroll, 99, 0, &pass);
        for (b = 0; b < pass.count; ++b) up_at_end &= pass.bursts[b].direction == 1;
    }
    CHECK(up_at_end); /* at the page end, scroll back up */
}
static void scroll_nudge_tests(void) {
    MotionRng rng, same;
    RECT page = {100, 200, 500, 700}, edge = {0, 0, 20, 20};
    POINT anchor = {300, 400}, current = anchor, duplicate = anchor, next, again;
    unsigned i, moved = 0;
    motion_seed(&rng, 99); motion_seed(&same, 99);
    for (i = 0; i < 500; ++i) {
        int did_move = motion_scroll_nudge(&rng, anchor, current, &page, &next);
        int same_move = motion_scroll_nudge(&same, anchor, duplicate, &page, &again);
        CHECK(did_move == same_move && next.x == again.x && next.y == again.y);
        CHECK(labs(next.x - current.x) <= 2 && labs(next.y - current.y) <= 2);
        CHECK(labs(next.x - anchor.x) <= 6 && labs(next.y - anchor.y) <= 6);
        CHECK(PtInRect(&page, next));
        moved += did_move;
        current = next; duplicate = again;
    }
    CHECK(moved > 300);
    anchor = (POINT){1, 1}; current = anchor;
    for (i = 0; i < 100; ++i) {
        motion_scroll_nudge(&rng, anchor, current, &edge, &next);
        CHECK(PtInRect(&edge, next));
        current = next;
    }
    CHECK(!motion_scroll_nudge(&rng, (POINT){30, 30}, current, &edge, &next));
}
static void policy_tests(void) {
    CHECK(web_same_site(L"https://docs.python.org/3/", L"https://docs.python.org/3/library/json.html"));
    CHECK(web_same_site(L"https://www.reddit.com/r/Baking/", L"https://reddit.com/r/Baking/comments/x"));
    CHECK(!web_same_site(L"https://docs.python.org/", L"https://evil.example/docs.python.org"));
    CHECK(!web_same_site(L"https://docs.python.org/", L"https://python.org/"));
    CHECK(!web_same_site(L"https://github.com/", L"https://user@github.com/"));
    CHECK(web_url_allowed(L"https://github.com/rust-lang/rust"));
    CHECK(web_url_allowed(L"https://example.com/a%20b"));
    CHECK(!web_url_allowed(L"https://github.com/login?return_to=x"));
    CHECK(!web_url_allowed(L"https://example.com/account/settings"));
    CHECK(!web_url_allowed(L"https://example.com/files/report.pdf"));
    CHECK(!web_url_allowed(L"https://example.com/a/%2e%2e/login"));
    CHECK(!web_url_allowed(L"https://example.com/item?download=1"));
    CHECK(!web_url_allowed(L"ftp://example.com/"));
    CHECK(web_name_safe(L"Data Structures"));
    CHECK(web_name_safe(L"Starting out"));      /* "star" only as a whole word */
    CHECK(!web_name_safe(L"Sign in"));
    CHECK(!web_name_safe(L"Star this repository"));
    CHECK(!web_name_safe(L"Download PDF"));
    CHECK(web_same_page(L"https://a.com/x/#top", L"https://a.com/x"));
    CHECK(!web_same_page(L"https://a.com/x", L"https://a.com/y"));
}
static const Profile *find(const ProfileSet *set, unsigned id) { return profile_find(set, id); }
static void catalog_tests(const wchar_t *profiles_path, const wchar_t *browsing_path) {
    ProfileSet profiles;
    BrowseCatalog catalog;
    char error[256];
    const Profile *wilhelmus, *williana;
    MotionRng rng;
    unsigned i, tech_w = 0, tech_x = 0, kinds[BROWSE_SEARCH_KIND_COUNT] = {0};
    wchar_t query[300];
    BrowseSearchKind kind;
    BrowseSession session = {0};
    BrowsePlan a, b;
    CHECK(profile_load(profiles_path, &profiles, error, sizeof(error)));
    CHECK(browse_catalog_load(browsing_path, &catalog, error, sizeof(error)));
    if (!profiles.count || !catalog.site_count) { fprintf(stderr, "  %s\n", error); return; }
    wilhelmus = find(&profiles, 1); williana = find(&profiles, 2);
    CHECK(wilhelmus && williana);
    if (!wilhelmus || !williana) return;
    CHECK(catalog.tabs[0] == 1 && catalog.tabs[1] == 5 && catalog.new_tab_search_percent == 50);
    /* Tech sites are much more likely for the profile that is into tech. */
    motion_seed(&rng, 5);
    for (i = 0; i < 2000; ++i) {
        const BrowseSite *s = browse_pick_site(&catalog, wilhelmus, &rng);
        tech_w += !wcscmp(s->category, L"tech_news") || !wcscmp(s->category, L"github");
        s = browse_pick_site(&catalog, williana, &rng);
        tech_x += !wcscmp(s->category, L"tech_news") || !wcscmp(s->category, L"github");
    }
    CHECK(tech_w > tech_x * 2);
    for (i = 0; i < 500; ++i) {
        CHECK(browse_pick_search(&catalog, williana, &rng, &kind, query, 300));
        CHECK(!wcschr(query, L'{') && !wcschr(query, L'}'));
        ++kinds[kind];
    }
    for (i = 0; i < BROWSE_SEARCH_KIND_COUNT; ++i) CHECK(kinds[i] > 0);
    /* The same seed gives the same plan; it fills 1-5 tabs. */
    session.profile = williana; session.catalog = &catalog; session.duration_ms = 600000; session.seed = 42;
    browser_session_plan(&session, &a); browser_session_plan(&session, &b);
    CHECK(a.tab_count >= 1 && a.tab_count <= 5 && a.tab_count == b.tab_count);
    CHECK(!wcscmp(a.openers[0].text, b.openers[0].text));
    profile_free(&profiles); browse_catalog_free(&catalog);
}
static void catalog_parse_tests(void) {
    BrowseCatalog c;
    char error[256];
    const wchar_t *bad_template = L"{\"version\":1,\"settings\":{\"tabs\":[1,5],\"direct_site_percent\":50,\"new_tab_search_percent\":50,"
        L"\"bookmark_percent\":10,\"idle_percent\":5,\"idle_seconds\":[5,9],\"page_seconds\":[5,9],\"tab_visit_seconds\":[20,30],"
        L"\"side_search_seconds\":[5,9]},\"search_weights\":{\"question\":1},\"searches\":{\"question\":[\"what is {topic}\"]},"
        L"\"sites\":[{\"url\":\"https://a.com/\",\"category\":\"lifestyle\",\"topics\":[\"x\"]}]}";
    const wchar_t *bad_site = L"{\"version\":1,\"settings\":{\"tabs\":[1,5],\"direct_site_percent\":50,\"new_tab_search_percent\":50,"
        L"\"bookmark_percent\":10,\"idle_percent\":5,\"idle_seconds\":[5,9],\"page_seconds\":[5,9],\"tab_visit_seconds\":[20,30],"
        L"\"side_search_seconds\":[5,9]},\"search_weights\":{\"question\":1},\"searches\":{\"question\":[\"what is {interest}\"]},"
        L"\"sites\":[{\"url\":\"https://a.com/login\",\"category\":\"lifestyle\",\"topics\":[\"x\"]}]}";
    CHECK(!browse_catalog_parse(bad_template, &c, error, sizeof(error)));
    CHECK(!browse_catalog_parse(bad_site, &c, error, sizeof(error)));
}
int main(int argc, char **argv) {
    path_tests(); control_move_tests(); hand_tests(); point_tests(); scroll_tests(); scroll_nudge_tests(); policy_tests(); catalog_parse_tests();
    if (argc > 2) {
        wchar_t profiles[MAX_PATH], browsing[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, profiles, MAX_PATH);
        MultiByteToWideChar(CP_ACP, 0, argv[2], -1, browsing, MAX_PATH);
        catalog_tests(profiles, browsing);
    } else { ++failures; fprintf(stderr, "FAIL: pass profiles.json and browsing.json\n"); }
    printf("%d checks, %d failures (no desktop input generated)\n", checks, failures);
    return failures ? 1 : 0;
}
