#ifndef HUMAN_MOTION_H
#define HUMAN_MOTION_H
#include <windows.h>
#include <stdint.h>
/* Seeded plans for visible pointer movement, click points, and scrolling. Ported from
   experiments/browser-explorer/motion.py. Pure functions: no input is sent here. */
typedef struct { uint32_t state; } MotionRng;
void motion_seed(MotionRng *rng, uint32_t seed);
double motion_unit(MotionRng *rng);
double motion_uniform(MotionRng *rng, double low, double high);
double motion_triangular(MotionRng *rng, double low, double high, double mode);
unsigned motion_between(MotionRng *rng, unsigned low, unsigned high);
int motion_chance(MotionRng *rng, unsigned percent);

/* A person's hand: how often they miss and correct, how much the path wobbles, and how
   quick they are. Derived from the profile, so one person always moves the same way.
   All zero means plain paths: no overshoot, no wobble, the base speed. */
typedef struct { unsigned overshoot_percent; double overshoot_scale, wobble_px, speed; } MotionHand;
uint32_t motion_hash(const wchar_t *text);
/* identity fixes the person's traits; randomness (0-100) makes misses and wobble more common. */
void motion_hand_for(MotionHand *hand, uint32_t identity, unsigned randomness);

/* A cubic Bezier path with asymmetric acceleration and deceleration, plus a sideways wobble
   that fades out at both ends so the path still starts and stops exactly. */
typedef struct {
    double start[2], end[2], first[2], second[2], normal[2];
    double acceleration, deceleration;
    double wobble, wobble_cycles[2], wobble_phase[2];
    unsigned duration_ms;
} MotionPath;
enum { MOTION_AIM, MOTION_REST };
/* hand may be NULL. */
void motion_path(MotionRng *rng, const MotionHand *hand, POINT start, POINT end, int purpose, double target_width, MotionPath *path);
/* One visible move: a single path, or a path that overshoots (sometimes stops short of) the
   target followed by a short correction after a pause. The missed point stays inside bounds
   (NULL: unchecked); when it would not, the move is direct. */
typedef struct { MotionPath segments[2]; unsigned count, pause_ms; } MotionMove;
void motion_move(MotionRng *rng, const MotionHand *hand, POINT start, POINT end, int purpose, double target_width,
    const RECT *bounds, MotionMove *move);
/* Control-to-control aiming uses the configured move time for a long trip; nearby controls
   take proportionally less time. The complete plan, including a correction, fits that time. */
void motion_control_move(MotionRng *rng, const MotionHand *hand, POINT start, POINT end,
    double target_width, const RECT *bounds, unsigned reference_ms, MotionMove *move);
void motion_path_at(const MotionPath *path, double fraction, POINT *point);
/* A resting point in the content interior, different on each call. */
int motion_rest_point(MotionRng *rng, const RECT *region, POINT *point);
/* A click point inside a control, biased toward its center. */
int motion_click_point(MotionRng *rng, const RECT *region, POINT *point);

/* Scroll passes mix fast scans (9-20 notches) with slow bursts (1-3 notches). */
#define MOTION_MAX_STEPS 24
typedef struct { int notches; unsigned delay_ms; } MotionWheelStep;
typedef struct {
    int fast, direction; /* direction -1 scrolls down, +1 up */
    unsigned step_count, pause_after_ms;
    MotionWheelStep steps[MOTION_MAX_STEPS];
} MotionBurst;
typedef struct { unsigned count, reading_ms; MotionBurst bursts[4]; } MotionScrollPass;
typedef struct { MotionRng *rng; int last_fast; } MotionScroll;
void motion_scroll_init(MotionScroll *scroll, MotionRng *rng);
/* scroll_percent is the page's vertical position (0-100) or -1 when unknown. */
void motion_scroll_plan(MotionScroll *scroll, int scroll_percent, unsigned stalls, MotionScrollPass *pass);
unsigned motion_burst_notches(const MotionBurst *burst);
/* A small random walk around the pointer's position at the start of a scroll sequence.
   Each step is at most 2 px per axis and stays within 6 px of the anchor and bounds. */
int motion_scroll_nudge(MotionRng *rng, POINT anchor, POINT current, const RECT *bounds, POINT *next);
#endif
