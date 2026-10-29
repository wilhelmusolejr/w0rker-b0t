#include "work_session.h"
#include <stdio.h>
#include <string.h>
static int checks, failures;
#define CHECK(t) do { ++checks; if (!(t)) { ++failures; fprintf(stderr,"FAIL %d: %s\n",__LINE__,#t); } } while (0)

static void parse_tests(void) {
    SessionConfig c;
    char error[256];
    CHECK(!session_config_parse(L"{\"version\":1}", &c, error, sizeof(error)) && error[0]);
    CHECK(!session_config_parse(L"{\"version\":2}", &c, error, sizeof(error)));
    CHECK(!session_config_parse(L"not json", &c, error, sizeof(error)));
}
static void shipped_tests(const wchar_t *path) {
    SessionConfig c;
    SessionPlan a, b;
    MotionRng rng;
    char error[256];
    unsigned seed, i, first_coding = 0, counts[3] = {0}, zero[3] = {0, 5, 0}, starts[START_LOOK_COUNT] = {0};
    unsigned long long idle_total = 0, day_total = 0;
    CHECK(session_config_load(path, &c, error, sizeof(error)));
    if (!c.target_minutes[1]) { fprintf(stderr, "  %s\n", error); return; }
    /* The agreed defaults. */
    CHECK(c.target_minutes[0] == 615 && c.target_minutes[1] == 645);
    CHECK(c.coding_minutes[0] == 40 && c.coding_minutes[1] == 80 && c.browsing_minutes[0] == 10 && c.browsing_minutes[1] == 20);
    CHECK(c.break_minutes[0] == 5 && c.break_minutes[1] == 20);
    /* The same seed plans the same day; a dry run shows what a live run does. */
    session_plan(&c, 42, 0, 35, &a); session_plan(&c, 42, 0, 35, &b);
    CHECK(!memcmp(&a, &b, sizeof(a)));
    for (seed = 1; seed <= 400; ++seed) {
        unsigned total = 0;
        session_plan(&c, seed, 0, 35, &a);
        /* Target 10h15-10h45. */
        CHECK(a.target_seconds >= 615 * 60 && a.target_seconds <= 645 * 60);
        CHECK(a.start < START_LOOK_COUNT && a.end < END_LOOK_COUNT && a.after < AFTER_STOP_COUNT);
        CHECK(a.close_count >= 1 && a.close_count <= 2 && a.start_app <= APP_CALCULATOR);
        ++starts[a.start];
        first_coding += a.slots[0].coding;
        /* Coding and browsing take turns, each within its range, and cover the whole target. */
        for (i = 0; i < a.slot_count; ++i) {
            const SessionSlot *s = &a.slots[i];
            if (i) CHECK(s->coding != a.slots[i - 1].coding);
            CHECK(s->coding ? s->minutes >= 40 && s->minutes <= 80 : s->minutes >= 10 && s->minutes <= 20);
            total += s->minutes * 60;
        }
        CHECK(total >= a.target_seconds);
        /* Idles: 1-6 min each, in order, before the end of the day. */
        for (i = 0; i < a.idle_count; ++i) {
            CHECK(a.idles[i].seconds >= 60 && a.idles[i].seconds <= 360);
            CHECK(a.idles[i].at < a.target_seconds);
            if (i) CHECK(a.idles[i].at >= a.idles[i - 1].at + a.idles[i - 1].seconds + 20 * 60);
            idle_total += a.idles[i].seconds;
        }
        day_total += a.target_seconds;
    }
    /* About 5% of the day idling (profile randomness 35 scales it by 0.94). */
    CHECK(idle_total * 1000 / day_total > 35 && idle_total * 1000 / day_total < 62);
    { unsigned calm = 0, random = 0;
      for (seed = 1; seed <= 200; ++seed) {
          session_plan(&c, seed, 0, 0, &a); for (i = 0; i < a.idle_count; ++i) calm += a.idles[i].seconds;
          session_plan(&c, seed, 0, 100, &a); for (i = 0; i < a.idle_count; ++i) random += a.idles[i].seconds;
      }
      CHECK(random > calm); }
    /* Coding first about 80% of days; every start look comes up. */
    CHECK(first_coding > 280 && first_coding < 360);
    for (i = 0; i < START_LOOK_COUNT; ++i) CHECK(starts[i] > 30);
    /* Already recorded most of the day: only what remains is planned. */
    session_plan(&c, 9, 600 * 60, 35, &a);
    { unsigned total = 0; for (i = 0; i < a.slot_count; ++i) total += a.slots[i].minutes * 60;
      CHECK(total < 3 * 3600); }
    /* Weighted picks; weight 0 is never picked. */
    motion_seed(&rng, 5);
    for (i = 0; i < 3000; ++i) ++counts[session_pick(c.end_looks, END_LOOK_COUNT, &rng)];
    CHECK(counts[END_DIRECT] > 850 && counts[END_DIRECT] < 1250 && counts[END_CLOSE_ALL] > 650 && counts[END_CLOSE_ALL] < 1100);
    for (i = 0; i < 200; ++i) CHECK(session_pick(zero, 3, &rng) == 1);
}
int main(int argc, char **argv) {
    parse_tests();
    if (argc > 1) {
        wchar_t path[MAX_PATH];
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, MAX_PATH);
        shipped_tests(path);
    } else { ++failures; fprintf(stderr, "FAIL: pass session.json\n"); }
    printf("%d checks, %d failures (no desktop input generated)\n", checks, failures);
    return failures ? 1 : 0;
}
