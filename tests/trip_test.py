"""Pass-through trips follow the switch curve: Hyprland's steps, each on its own
slice, add up to the curve the engine traces; Hyprland accepts every slice; an
overshoot carries a trip as far past the target as it does one switch. Run: tests/run.sh"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "modules/motion"))
import trip  # noqa: E402

schema = json.load(open(os.path.join(ROOT, "modules/motion/schema.json")))
for name, curve in schema["curves"].items():
    one = trip._extent(trip.trip_points(curve, 1))
    for n in range(2, trip.MAX_STEPS + 1):
        cut = trip.steps(curve, n)
        ends, slices = cut
        assert len(ends) == n and abs(ends[-1] - 1) < 1e-9, (name, n)
        assert all(b > a for a, b in zip([0.0] + ends, ends)), f"{name}: steps must take time ({n})"
        for p in slices:  # what Hyprland accepts in a bezier
            assert 0 <= p[0] <= 1 and 0 <= p[2] <= 1, (name, n, p)
            assert -1 <= p[1] <= 2 and -1 <= p[3] <= 2, (name, n, p)
        gap = max(abs(trip.played_position(curve, n, i / 120, cut) - trip.trip_position(curve, n, i / 120))
                  for i in range(121))
        assert gap < 1e-3, f"{name}: the steps drift {gap:.4f} workspaces from the curve ({n})"
        over = trip._extent(trip.trip_points(curve, n))
        assert abs(n * max(over) - max(one)) < 0.005, f"{name}: a {n}-workspace trip overshoots {n * max(over):.3f}"

# a spring's trips run on the bezier closest to it
c = schema["curves"]["spring"]
S = trip.spring_settle(c["mass"], c["stiffness"], c["dampening"])
err = max(abs(trip.bezier_y_at_x(c["trip"], i / 100) - trip.spring_response(c["mass"], c["stiffness"], c["dampening"], i / 100 * S))
          for i in range(101))
assert err < 0.06, f"the spring's trip curve is {err:.3f} off the spring"
print(f"trip tests passed ({len(schema['curves'])} curves, trips of 2 to {trip.MAX_STEPS} workspaces)")
