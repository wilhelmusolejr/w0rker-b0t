#include "browser_scroll_plan.h"

static unsigned pick(BrowserScrollPlan *p, unsigned minimum, unsigned maximum) {
    uint32_t value = p->state ? p->state : UINT32_C(0x9e3779b9);
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    p->state = value;
    return minimum + value % (maximum - minimum + 1);
}

void browser_scroll_plan_init(BrowserScrollPlan *p, uint32_t seed) {
    p->state = seed; p->steps = 0; p->offset = 0;
}

BrowserScrollStep browser_scroll_plan_next(BrowserScrollPlan *p) {
    BrowserScrollStep step;
    if (p->steps >= 2 && p->offset <= -3 && pick(p, 0, 4) == 0)
        step.notches = (int)pick(p, 1, 2);
    else step.notches = -(int)pick(p, 2, 4);
    step.pause_ms = pick(p, 1600, 3800);
    p->offset += step.notches;
    ++p->steps;
    return step;
}

int browser_url_valid(const wchar_t *url) {
    size_t i, prefix;
    if (!_wcsnicmp(url, L"https://", 8)) prefix = 8;
    else if (!_wcsnicmp(url, L"http://", 7)) prefix = 7;
    else return 0;
    if (!url[prefix] || url[prefix] == L'/' || url[prefix] == L'?' || url[prefix] == L'#') return 0;
    for (i = 0; url[i]; ++i)
        if (i >= 2048 || url[i] <= L' ' || url[i] == L'"' || url[i] == L'\\') return 0;
    return 1;
}
