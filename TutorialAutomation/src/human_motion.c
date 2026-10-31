#include "human_motion.h"
#include <math.h>
#include <stdlib.h>

void motion_seed(MotionRng *r, uint32_t seed) { r->state = seed ? seed : UINT32_C(0x9e3779b9); }
static uint32_t next(MotionRng *r) {
    uint32_t value = r->state ? r->state : UINT32_C(0x9e3779b9);
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    return r->state = value;
}
double motion_unit(MotionRng *r) { return (next(r) >> 8) / 16777216.0; }
double motion_uniform(MotionRng *r, double low, double high) { return low + (high - low) * motion_unit(r); }
double motion_triangular(MotionRng *r, double low, double high, double mode) {
    double u = motion_unit(r), c = high > low ? (mode - low) / (high - low) : 0.5;
    return u < c ? low + sqrt(u * (high - low) * (mode - low)) : high - sqrt((1 - u) * (high - low) * (high - mode));
}
unsigned motion_between(MotionRng *r, unsigned low, unsigned high) {
    return high <= low ? low : low + next(r) % (high - low + 1);
}
int motion_chance(MotionRng *r, unsigned percent) { return motion_between(r, 0, 99) < percent; }

uint32_t motion_hash(const wchar_t *text) {
    uint32_t value = UINT32_C(2166136261);
    for (; text && *text; ++text) { value ^= (uint32_t)*text; value *= UINT32_C(16777619); }
    return value;
}
void motion_hand_for(MotionHand *h, uint32_t identity, unsigned randomness) {
    MotionRng r;
    double extra = (randomness > 100 ? 100 : randomness) / 100.0;
    motion_seed(&r, identity);
    h->overshoot_percent = motion_between(&r, 12, 24) + (unsigned)lround(extra * 12);
    h->overshoot_scale = motion_uniform(&r, 0.8, 1.3);
    h->wobble_px = motion_uniform(&r, 0.8, 2.0) + extra * 1.2;
    h->speed = motion_uniform(&r, 0.9, 1.12);
}

void motion_path(MotionRng *r, const MotionHand *hand, POINT start, POINT end, int purpose, double target_width, MotionPath *p) {
    double dx = end.x - start.x, dy = end.y - start.y, distance = sqrt(dx * dx + dy * dy);
    double nx = distance ? -dy / distance : 0, ny = distance ? dx / distance : 0;
    double bend = distance * 0.10 < 65 ? distance * 0.10 : 65;
    double f1 = motion_uniform(r, 0.18, 0.35), f2 = motion_uniform(r, 0.65, 0.86);
    /* Keep both control points on the same side of the route. Independent signs made
       consecutive clicks sometimes weave in an S shape before reaching the target. */
    double side = motion_uniform(r, -bend, bend);
    double b1 = side * motion_uniform(r, 0.65, 1.0), b2 = side * motion_uniform(r, 0.65, 1.0);
    double speed = purpose == MOTION_AIM ? motion_uniform(r, 1100, 2400) : motion_uniform(r, 800, 1600);
    if (hand && hand->speed > 0) speed *= hand->speed;
    /* Fitts-style precision term: small, distant targets take a little longer. */
    double precision = 0.035 * log2(1 + distance / (target_width > 8 ? target_width : 8));
    double seconds = (0.07 + distance / speed + precision) * motion_uniform(r, 0.85, 1.20);
    p->start[0] = start.x; p->start[1] = start.y; p->end[0] = end.x; p->end[1] = end.y;
    p->first[0] = start.x + dx * f1 + nx * b1; p->first[1] = start.y + dy * f1 + ny * b1;
    p->second[0] = start.x + dx * f2 + nx * b2; p->second[1] = start.y + dy * f2 + ny * b2;
    if (seconds < 0.10) seconds = 0.10;
    if (seconds > 1.65) seconds = 1.65;
    p->duration_ms = (unsigned)(seconds * 1000);
    p->acceleration = motion_uniform(r, 1.3, 2.2);
    p->deceleration = motion_uniform(r, 1.3, 2.4);
    p->normal[0] = nx; p->normal[1] = ny;
    p->wobble = 0;
    if (hand && hand->wobble_px > 0) {
        /* Short moves barely wobble; a slow sway plus a faster tremor. */
        p->wobble = hand->wobble_px * (distance < 300 ? distance / 300 : 1) * motion_uniform(r, 0.6, 1.4);
        p->wobble_cycles[0] = motion_uniform(r, 1.0, 2.2); p->wobble_cycles[1] = motion_uniform(r, 3.5, 6.5);
        p->wobble_phase[0] = motion_uniform(r, 0, 6.2831853); p->wobble_phase[1] = motion_uniform(r, 0, 6.2831853);
    }
}
void motion_move(MotionRng *r, const MotionHand *h, POINT start, POINT end, int purpose, double width,
    const RECT *bounds, MotionMove *m) {
    double dx = end.x - start.x, dy = end.y - start.y, distance = sqrt(dx * dx + dy * dy);
    m->count = 1; m->pause_ms = 0;
    if (h && h->overshoot_percent && purpose == MOTION_AIM && distance >= 240 && motion_chance(r, h->overshoot_percent)) {
        double miss = distance * motion_uniform(r, 0.03, 0.08) * (h->overshoot_scale > 0 ? h->overshoot_scale : 1);
        double side = motion_uniform(r, -0.35, 0.35), along;
        POINT missed;
        if (miss < 6) miss = 6;
        if (miss > 45) miss = 45;
        /* Mostly past the target; sometimes short of it. */
        along = motion_chance(r, 80) ? miss : -miss;
        missed.x = end.x + (LONG)lround((dx * along - dy * miss * side) / distance);
        missed.y = end.y + (LONG)lround((dy * along + dx * miss * side) / distance);
        if (!bounds || PtInRect(bounds, missed)) {
            motion_path(r, h, start, missed, purpose, width, &m->segments[0]);
            motion_path(r, h, missed, end, MOTION_AIM, width, &m->segments[1]);
            m->pause_ms = motion_between(r, 40, 180);
            m->count = 2;
            return;
        }
    }
    motion_path(r, h, start, end, purpose, width, &m->segments[0]);
}
void motion_control_move(MotionRng *r, const MotionHand *h, POINT start, POINT end,
    double width, const RECT *bounds, unsigned reference_ms, MotionMove *m) {
    double distance = hypot((double)end.x - start.x, (double)end.y - start.y);
    double fraction = sqrt(distance / 650.0);
    unsigned wanted, travel;
    MotionHand direct;
    if (h && h->speed > 0) fraction /= h->speed;
    if (fraction < 0.25) fraction = 0.25;
    if (fraction > 1.0) fraction = 1.0;
    wanted = (unsigned)lround(reference_ms * fraction);
    if (wanted < 100) wanted = 100;
    /* A short hop should not turn into a miss, a pause, and a sudden correction. */
    if (h && wanted < 400) {
        direct = *h; direct.overshoot_percent = 0; h = &direct;
    }
    motion_move(r, h, start, end, MOTION_AIM, width, bounds, m);
    if (m->count == 1) { m->segments[0].duration_ms = wanted; return; }
    if (m->pause_ms > wanted / 5) m->pause_ms = wanted / 5;
    travel = wanted - m->pause_ms;
    {
        unsigned first = m->segments[0].duration_ms, second = m->segments[1].duration_ms;
        unsigned correction = (unsigned)lround((double)travel * second / (first + second));
        if (correction < 80) correction = 80;
        if (correction > travel / 3) correction = travel / 3;
        m->segments[0].duration_ms = travel - correction;
        m->segments[1].duration_ms = correction;
    }
}
void motion_path_at(const MotionPath *p, double t, POINT *point) {
    double up, down, u, v;
    int i;
    double out[2];
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    up = pow(t, p->acceleration); down = pow(1 - t, p->deceleration);
    u = up + down > 0 ? up / (up + down) : t; v = 1 - u;
    double sway = 0;
    if (p->wobble > 0)
        sway = p->wobble * sin(3.14159265 * u) * (0.75 * sin(6.2831853 * p->wobble_cycles[0] * u + p->wobble_phase[0]) +
            0.25 * sin(6.2831853 * p->wobble_cycles[1] * u + p->wobble_phase[1]));
    for (i = 0; i < 2; ++i)
        out[i] = v * v * v * p->start[i] + 3 * v * v * u * p->first[i] + 3 * v * u * u * p->second[i] + u * u * u * p->end[i] +
            p->normal[i] * sway;
    point->x = (LONG)lround(out[0]); point->y = (LONG)lround(out[1]);
}
int motion_rest_point(MotionRng *r, const RECT *region, POINT *point) {
    double width = region->right - region->left, height = region->bottom - region->top;
    double x, y;
    if (width < 20 || height < 20) return 0;
    x = motion_triangular(r, 0.25, 0.82, motion_uniform(r, 0.45, 0.66));
    y = motion_triangular(r, 0.22, 0.80, motion_uniform(r, 0.40, 0.62));
    point->x = region->left + (LONG)lround(width * x);
    point->y = region->top + (LONG)lround(height * y);
    return 1;
}
int motion_click_point(MotionRng *r, const RECT *region, POINT *point) {
    double width = region->right - region->left, height = region->bottom - region->top;
    LONG x, y;
    if (width < 4 || height < 4) return 0;
    x = region->left + (LONG)lround(width * motion_triangular(r, 0.20, 0.80, motion_uniform(r, 0.40, 0.60)));
    y = region->top + (LONG)lround(height * motion_triangular(r, 0.22, 0.78, 0.50));
    point->x = x < region->left ? region->left : x >= region->right ? region->right - 1 : x;
    point->y = y < region->top ? region->top : y >= region->bottom ? region->bottom - 1 : y;
    return 1;
}

void motion_scroll_init(MotionScroll *s, MotionRng *rng) { s->rng = rng; s->last_fast = motion_chance(rng, 65); }
int motion_scroll_nudge(MotionRng *r, POINT anchor, POINT current, const RECT *bounds, POINT *point) {
    LONG left, right, top, bottom;
    LONG dx, dy;
    if (!bounds || !point || !PtInRect(bounds, anchor) ||
        labs(current.x - anchor.x) > 6 || labs(current.y - anchor.y) > 6) return 0;
    left = anchor.x - 6 > bounds->left ? anchor.x - 6 : bounds->left;
    right = anchor.x + 6 < bounds->right - 1 ? anchor.x + 6 : bounds->right - 1;
    top = anchor.y - 6 > bounds->top ? anchor.y - 6 : bounds->top;
    bottom = anchor.y + 6 < bounds->bottom - 1 ? anchor.y + 6 : bounds->bottom - 1;
    dx = (LONG)motion_between(r, 0, 4) - 2;
    dy = (LONG)motion_between(r, 0, 4) - 2;
    if (!dx && !dy) dx = motion_chance(r, 50) ? 1 : -1;
    point->x = current.x + dx;
    point->y = current.y + dy;
    if (point->x < left) point->x = left;
    if (point->x > right) point->x = right;
    if (point->y < top) point->y = top;
    if (point->y > bottom) point->y = bottom;
    return point->x != current.x || point->y != current.y;
}
unsigned motion_burst_notches(const MotionBurst *b) {
    unsigned i, total = 0;
    for (i = 0; i < b->step_count; ++i) total += (unsigned)(b->steps[i].notches < 0 ? -b->steps[i].notches : b->steps[i].notches);
    return total;
}
void motion_scroll_plan(MotionScroll *s, int percent, unsigned stalls, MotionScrollPass *pass) {
    MotionRng *r = s->rng;
    int direction, styles[4];
    unsigned count, roll = motion_between(r, 0, 99), i;
    /* Page ends reverse the direction; repeated stalls mean the page did not move. */
    if (percent >= 97) direction = 1;
    else if (percent >= 0 && percent <= 2) direction = -1;
    else if (stalls >= 2) direction = 1;
    else direction = motion_chance(r, 12) ? 1 : -1;
    count = roll < 50 ? 2 : roll < 85 ? 3 : 4;
    styles[0] = motion_chance(r, 65) ? s->last_fast : !s->last_fast;
    styles[1] = !styles[0];
    for (i = 2; i < count; ++i) styles[i] = motion_chance(r, 50);
    pass->count = count;
    for (i = 0; i < count; ++i) {
        MotionBurst *b = &pass->bursts[i];
        unsigned remaining;
        b->fast = styles[i]; b->direction = direction; b->step_count = 0;
        /* A short look back after scanning downward. */
        if (i == count - 1 && !b->fast && direction == -1 && motion_chance(r, 12)) b->direction = 1;
        remaining = b->fast ? motion_between(r, 9, 20) : motion_between(r, 1, 3);
        while (remaining && b->step_count < MOTION_MAX_STEPS) {
            unsigned amount = !b->fast || !b->step_count || remaining <= 2 ? 1 :
                motion_between(r, 1, remaining < 3 ? remaining : 3);
            double delay = b->fast ? motion_uniform(r, 0.025, 0.065) : motion_uniform(r, 0.12, 0.30);
            remaining -= amount;
            if (!b->step_count || !remaining) delay *= motion_uniform(r, 1.15, 1.55);
            b->steps[b->step_count].notches = b->direction * (int)amount;
            b->steps[b->step_count++].delay_ms = (unsigned)(delay * 1000);
        }
        b->pause_after_ms = (unsigned)(1000 * (b->fast ? motion_uniform(r, 0.08, 0.35) : motion_uniform(r, 0.25, 0.90)));
    }
    s->last_fast = styles[count - 1];
    pass->reading_ms = (unsigned)(1000 * (s->last_fast ? motion_uniform(r, 0.65, 2.4) : motion_uniform(r, 2.0, 7.0)));
}
