"""Pass-through trips on the workspace curve.

A trip past n workspaces is one run of the switch curve C over the trip's time:
position = n * C(t / T). Hyprland can only animate one workspace at a time, so
the trip is played as n steps, each with a curve of its own: the slice of C
between the moments it reaches k-1 and k workspaces, rescaled to a unit square.
Played back to back for the right times, the slices are C again, seam for seam
(a slice ends at the same speed the next one starts with).

A bezier's slice is itself a bezier (de Casteljau), so the cut is exact. A
spring is not a bezier: its trips run on the bezier closest to it (the curve's
"trip" points in schema.json), cut the same way. The engine is sent that bezier
with the trip, so the wallpaper traces exactly what the windows do.

Pure functions: the Lua writer in motion.py and tests/trip_test.py use them.
"""
import functools
import math

MAX_STEPS = 9  # Omarchy gives each monitor ten workspaces: a trip passes at most nine


# ---------- the curves ----------

def bezier_point(p, u):
    """(x, y) at parameter u on the cubic (0,0) (x1,y1) (x2,y2) (1,1)."""
    a = 1 - u
    return (3 * a * a * u * p[0] + 3 * a * u * u * p[2] + u ** 3,
            3 * a * a * u * p[1] + 3 * a * u * u * p[3] + u ** 3)


def bezier_y_at_x(p, x):
    """What Hyprland evaluates: y where the curve's x is `x`."""
    lo, hi = 0.0, 1.0
    for _ in range(60):
        mid = (lo + hi) / 2
        if bezier_point(p, mid)[0] < x:
            lo = mid
        else:
            hi = mid
    return bezier_point(p, (lo + hi) / 2)[1]


def spring_response(m, k, c, t):
    """A damped spring released from 0 toward 1, `t` seconds in (as the engine's motion.c)."""
    w0, z = math.sqrt(k / m), c / (2 * math.sqrt(k * m))
    if z < 1:
        wd = w0 * math.sqrt(1 - z * z)
        return 1 - math.exp(-z * w0 * t) * (math.cos(wd * t) + z * w0 / wd * math.sin(wd * t))
    if z == 1:
        return 1 - math.exp(-w0 * t) * (1 + w0 * t)
    s = math.sqrt(z * z - 1)
    r1, r2 = -w0 * (z - s), -w0 * (z + s)
    return 1 - (r2 * math.exp(r1 * t) - r1 * math.exp(r2 * t)) / (r2 - r1)


def spring_settle(m, k, c):
    """Seconds until the spring is within 1/500 of rest (as the engine's spring_settle)."""
    w0, z = math.sqrt(k / m), c / (2 * math.sqrt(k * m))
    rate = z * w0 if z <= 1 else w0 * (z - math.sqrt(z * z - 1))
    return min(10.0, math.log(500) / rate) if rate > 0 else 10.0


def base_ms(curve, ms):
    """How long one switch takes: the set duration, or a spring's own settling time."""
    if curve["type"] == "spring":
        return round(spring_settle(curve["mass"], curve["stiffness"], curve["dampening"]) * 1000)
    return ms


# ---------- slicing ----------

def _first(f, target, lo=0.0, hi=1.0, samples=4000):
    """The first u in [lo, hi] where f(u) reaches target."""
    prev = lo
    for i in range(1, samples + 1):
        u = lo + (hi - lo) * i / samples
        if f(u) >= target:
            a, b = prev, u
            for _ in range(60):
                mid = (a + b) / 2
                if f(mid) >= target:
                    b = mid
                else:
                    a = mid
            return b
        prev = u
    return hi


def _lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def _split(P, t):
    """de Casteljau: the two halves of cubic P at parameter t."""
    a, b, c = _lerp(P[0], P[1], t), _lerp(P[1], P[2], t), _lerp(P[2], P[3], t)
    d, e = _lerp(a, b, t), _lerp(b, c, t)
    f = _lerp(d, e, t)
    return [P[0], a, d, f], [f, e, c, P[3]]


def _normalise(Q):
    """A slice's control points, rescaled so it runs (0,0) to (1,1): Hyprland's form."""
    (x0, y0), (x3, y3) = Q[0], Q[3]
    sx, sy = (x3 - x0) or 1.0, (y3 - y0) or 1.0
    pts = []
    for x, y in (Q[1], Q[2]):
        pts += [min(1.0, max(0.0, (x - x0) / sx)), (y - y0) / sy]
    return pts


def _bezier_steps(p, n):
    P = [(0.0, 0.0), (p[0], p[1]), (p[2], p[3]), (1.0, 1.0)]
    y_of = lambda u: bezier_point(p, u)[1]
    # where the trip reaches each workspace: the first time, so an overshooting
    # curve overshoots in the last step only, past the target and back
    us = [0.0] + [_first(y_of, k / n) for k in range(1, n)] + [1.0]
    ends, curves = [], []
    for k in range(n):
        ua, ub = us[k], us[k + 1]
        right = _split(P, ua)[1] if ua > 0 else P
        seg = _split(right, (ub - ua) / (1 - ua))[0] if ub < 1 else right
        curves.append(_normalise(seg))
        ends.append(bezier_point(p, ub)[0])
    ends[-1] = 1.0
    return ends, curves


def _extent(p):
    """How far the curve goes past 1 and below 0."""
    ys = [bezier_point(p, i / 200)[1] for i in range(201)]
    return max(0.0, max(ys) - 1), max(0.0, -min(ys))


def _squeeze(p, f):
    """The curve with its control points' reach beyond 0 and 1 scaled by f."""
    q = list(p)
    for i in (1, 3):
        q[i] = 1 + (q[i] - 1) * f if q[i] > 1 else q[i] * f if q[i] < 0 else q[i]
    return q


def trip_points(curve, n):
    return list(_trip_points(tuple(curve["trip"] if curve["type"] == "spring" else curve["points"]), n))


@functools.lru_cache(maxsize=None)
def _trip_points(points, n):
    """The bezier a trip past n workspaces runs on: the curve itself (or the one
    closest to a spring). A trip is n workspaces long, so an overshoot as a
    share of it would carry the windows n times further past the target than a
    switch does. Whatever reaches beyond 0 and 1 is squeezed until the trip
    overshoots by as many workspaces as a switch does screens."""
    p = list(points)
    over, under = _extent(p)
    if over <= 0 and under <= 0:
        return tuple(p)
    lo, hi = 0.0, 1.0
    for _ in range(40):  # the overshoot grows with f: find where n times it is one switch's
        f = (lo + hi) / 2
        o2, u2 = _extent(_squeeze(p, f))
        if n * max(o2, u2) > max(over, under):
            hi = f
        else:
            lo = f
    return tuple(_squeeze(p, lo))


def steps(curve, n):
    """For a trip past n workspaces: when each step ends (fractions of the trip's
    time, the last 1) and each step's curve (x1, y1, x2, y2)."""
    return _bezier_steps(trip_points(curve, n), n)


def trip_position(curve, n, x):
    """Workspaces passed, x (0..1) into a trip, as the engine computes it."""
    return n * bezier_y_at_x(trip_points(curve, n), x)


def played_position(curve, n, x, cut=None):
    """Workspaces passed, x into a trip, as Hyprland plays the steps (`cut`: steps(curve, n), if at hand)."""
    ends, curves = cut or steps(curve, n)
    start = 0.0
    for k, (end, p) in enumerate(zip(ends, curves)):
        if x <= end or k == n - 1:
            local = (x - start) / ((end - start) or 1.0)
            return k + bezier_y_at_x(p, min(1.0, max(0.0, local)))
        start = end
    return float(n)
