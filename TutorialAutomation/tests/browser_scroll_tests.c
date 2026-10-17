#include "browser_scroll_plan.h"
#include "browser_action.h"
#include <stdio.h>

static int failures, checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

int main(void) {
    BrowserScrollPlan a, b, different;
    unsigned i, upward = 0, changed = 0;
    Playback p = {0};
    browser_scroll_plan_init(&a, 42);
    browser_scroll_plan_init(&b, 42);
    browser_scroll_plan_init(&different, 43);
    for (i = 0; i < 1000; ++i) {
        BrowserScrollStep step = browser_scroll_plan_next(&a);
        BrowserScrollStep same = browser_scroll_plan_next(&b);
        BrowserScrollStep other = browser_scroll_plan_next(&different);
        CHECK(step.notches == same.notches && step.pause_ms == same.pause_ms);
        CHECK(step.pause_ms >= 1600 && step.pause_ms <= 3800);
        CHECK((step.notches >= -4 && step.notches <= -2) ||
              (step.notches >= 1 && step.notches <= 2));
        CHECK(a.offset < 0);
        if (i < 2) CHECK(step.notches < 0);
        upward += step.notches > 0;
        changed += step.notches != other.notches || step.pause_ms != other.pause_ms;
    }
    CHECK(upward > 0 && upward < 400);
    CHECK(changed > 0);
    CHECK(browser_url_valid(L"https://docs.python.org/3/tutorial/datastructures.html"));
    CHECK(browser_url_valid(L"http://127.0.0.1:8715/reading?x=1#section"));
    CHECK(browser_url_valid(L"HTTPS://example.com/"));
    CHECK(!browser_url_valid(L"javascript:alert(1)"));
    CHECK(!browser_url_valid(L"file:///C:/example.html"));
    CHECK(!browser_url_valid(L"https://"));
    CHECK(!browser_url_valid(L"https:///example.com"));
    CHECK(!browser_url_valid(L"https://example.com/\" --other-option"));
    CHECK(!browser_url_valid(L"https://example.com/\n"));
    CHECK(!browser_url_valid(L"https://example.com/path with spaces"));
    CHECK(!browser_checkpoint_due(&p, 40, 1));
    p.browser.every_lines = p.browser.next_line = 40;
    CHECK(!browser_checkpoint_due(&p, 20, 1));
    CHECK(browser_checkpoint_due(&p, 40, 1));
    CHECK(!browser_checkpoint_due(&p, 40, 0));
    p.browser.next_line = 80;
    CHECK(!browser_checkpoint_due(&p, 60, 1));
    CHECK(browser_checkpoint_due(&p, 80, 1));
    printf("Browser scrolling: %d checks, %d failures; no input generated\n", checks, failures);
    return failures ? 1 : 0;
}
