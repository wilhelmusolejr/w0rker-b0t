#include "action_pace.h"
#include <stdio.h>

static int checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)
int main(void) {
    ActionPace a = {0}, b, other;
    RECT control = {-1840, 110, -1640, 145}, tiny = {1, 1, 4, 8};
    POINT point;
    unsigned i, variation = 0;
    CHECK(action_pause_next(&a) == 0);
    CHECK(action_point_next(&a, &control, &point) && point.x == -1740 && point.y == 127);
    CHECK(!action_point_next(&a, &tiny, &point));
    action_pace_configure(&a, 900, 2200, 650, 1400, 42);
    b = a;
    action_pace_configure(&other, 900, 2200, 650, 1400, 43);
    for (i = 0; i < 1000; ++i) {
        unsigned pause = action_pause_next(&a), same_pause = action_pause_next(&b);
        unsigned move = action_move_next(&a), same_move = action_move_next(&b);
        double curve = action_curve_next(&a), same_curve = action_curve_next(&b);
        POINT same;
        CHECK(pause >= 900 && pause <= 2200 && pause == same_pause);
        CHECK(move >= 650 && move <= 1400 && move == same_move);
        CHECK(curve >= -0.08 && curve <= 0.08 && curve == same_curve);
        CHECK(action_point_next(&a, &control, &point) && PtInRect(&control, point));
        CHECK(action_point_next(&b, &control, &same) && same.x == point.x && same.y == point.y);
        CHECK(point.x >= -1752 && point.x <= -1728 && point.y >= 123 && point.y <= 131);
        variation += pause != action_pause_next(&other);
    }
    CHECK(variation > 0);
    action_pace_configure(&a, 0, 0, 100, 100, 1);
    CHECK(action_pause_next(&a) == 0 && action_move_next(&a) == 100);
    printf("Action presentation pacing: %d checks, %d failures; no input or sleep\n", checks, failures);
    return failures ? 1 : 0;
}
