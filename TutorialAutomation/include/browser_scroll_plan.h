#ifndef BROWSER_SCROLL_PLAN_H
#define BROWSER_SCROLL_PLAN_H
#include <stdint.h>
#include <wchar.h>
typedef struct { uint32_t state; unsigned steps; int offset; } BrowserScrollPlan;
typedef struct { int notches; unsigned pause_ms; } BrowserScrollStep;
void browser_scroll_plan_init(BrowserScrollPlan *plan, uint32_t seed);
BrowserScrollStep browser_scroll_plan_next(BrowserScrollPlan *plan);
int browser_url_valid(const wchar_t *url);
#endif
