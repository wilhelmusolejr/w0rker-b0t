"""Seeded movement and scrolling plans for the visible browsing demonstration."""

from dataclasses import dataclass
import math


@dataclass
class PointerPath:
    start: tuple
    end: tuple
    first: tuple
    second: tuple
    duration: float
    acceleration: float
    deceleration: float

    def at(self, fraction):
        t = max(0.0, min(1.0, fraction))
        up, down = t ** self.acceleration, (1 - t) ** self.deceleration
        u = up / (up + down)
        v = 1 - u
        return tuple(v ** 3 * self.start[i] + 3 * v * v * u * self.first[i]
                     + 3 * v * u * u * self.second[i] + u ** 3 * self.end[i] for i in (0, 1))


def pointer_path(start, end, rng, purpose='aim', target_width=40):
    dx, dy = end[0] - start[0], end[1] - start[1]
    distance = math.hypot(dx, dy)
    normal = (-dy / distance, dx / distance) if distance else (0, 0)
    bend = min(65, distance * 0.10)
    first_fraction, second_fraction = rng.uniform(0.18, 0.35), rng.uniform(0.65, 0.86)
    first_bend, second_bend = rng.uniform(-bend, bend), rng.uniform(-bend, bend)
    first = (start[0] + dx * first_fraction + normal[0] * first_bend,
             start[1] + dy * first_fraction + normal[1] * first_bend)
    second = (start[0] + dx * second_fraction + normal[0] * second_bend,
              start[1] + dy * second_fraction + normal[1] * second_bend)
    speed = rng.uniform(1100, 2400) if purpose == 'aim' else rng.uniform(800, 1600)
    precision = 0.035 * math.log2(1 + distance / max(8, target_width))
    duration = min(1.65, max(0.10, (0.07 + distance / speed + precision) * rng.uniform(0.85, 1.20)))
    return PointerPath(start, end, first, second, duration,
                       rng.uniform(1.3, 2.2), rng.uniform(1.3, 2.4))


def rest_point(region, rng):
    """Prefer the content interior, with a different resting point on each visit."""
    width, height = region['right'] - region['left'], region['bottom'] - region['top']
    x = rng.triangular(0.25, 0.82, rng.uniform(0.45, 0.66))
    y = rng.triangular(0.22, 0.80, rng.uniform(0.40, 0.62))
    return round(region['left'] + width * x), round(region['top'] + height * y)


def click_point(region, rng):
    width, height = region['right'] - region['left'], region['bottom'] - region['top']
    x = rng.triangular(0.20, 0.80, rng.uniform(0.40, 0.60))
    y = rng.triangular(0.22, 0.78, 0.50)
    return (min(math.ceil(region['right']) - 1, max(math.ceil(region['left']), round(region['left'] + width * x))),
            min(math.ceil(region['bottom']) - 1, max(math.ceil(region['top']), round(region['top'] + height * y))))


@dataclass
class WheelStep:
    delta: int
    delay: float


@dataclass
class ScrollBurst:
    style: str
    direction: int
    steps: list
    pause_after: float

    @property
    def notches(self):
        return sum(abs(step.delta) for step in self.steps) // 120


@dataclass
class ScrollPass:
    bursts: list
    reading_pause: float


class ScrollRhythm:
    def __init__(self, rng):
        self.rng = rng
        self.last_style = 'fast' if rng.random() < 0.65 else 'slow'

    def plan(self, scroll_percent=-1, stalls=0):
        rng = self.rng
        if scroll_percent >= 97:
            direction = 1
        elif 0 <= scroll_percent <= 2:
            direction = -1
        elif stalls >= 2:
            direction = 1
        else:
            direction = 1 if rng.random() < 0.12 else -1
        count = rng.choices((2, 3, 4), weights=(50, 35, 15), k=1)[0]
        first = self.last_style if rng.random() < 0.65 else ('slow' if self.last_style == 'fast' else 'fast')
        styles = [first, 'slow' if first == 'fast' else 'fast']
        styles.extend(rng.choice(('fast', 'slow')) for _ in range(count - 2))
        bursts = []
        for index, style in enumerate(styles):
            burst_direction = direction
            if index == count - 1 and style == 'slow' and direction == -1 and rng.random() < 0.12:
                burst_direction = 1  # A short look back after scanning downward.
            remaining = rng.randint(9, 20) if style == 'fast' else rng.randint(1, 3)
            steps = []
            while remaining:
                amount = 1 if style == 'slow' or not steps or remaining <= 2 else rng.randint(1, min(3, remaining))
                remaining -= amount
                delay = rng.uniform(0.025, 0.065) if style == 'fast' else rng.uniform(0.12, 0.30)
                if not steps or not remaining:
                    delay *= rng.uniform(1.15, 1.55)
                steps.append(WheelStep(burst_direction * amount * 120, delay))
            bursts.append(ScrollBurst(style, burst_direction, steps,
                                      rng.uniform(0.08, 0.35) if style == 'fast' else rng.uniform(0.25, 0.90)))
        self.last_style = styles[-1]
        reading = rng.uniform(0.65, 2.4) if self.last_style == 'fast' else rng.uniform(2.0, 7.0)
        return ScrollPass(bursts, reading)
