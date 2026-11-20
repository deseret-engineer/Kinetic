#!/usr/bin/env python3
"""Versions follow Semantic Versioning (semver.org): lib/modman.py's ordering and
checks, and Kinetic's own version being one. Run by tests/run.sh."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "lib"))
import modman  # noqa: E402

# semver.org's own example of precedence, lowest first
ORDER = ["1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
         "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1", "1.1.0", "2.0.0"]
for lo, hi in zip(ORDER, ORDER[1:]):
    assert modman.vkey(lo) < modman.vkey(hi), f"{lo} should come before {hi}"
assert modman.vkey("1.0.0+build.5") == modman.vkey("1.0.0"), "build metadata does not count"
assert modman.vkey("1.2") == modman.vkey("1.2.0"), "a missing PATCH is 0"

cases = [("1.2.0", ">=1.2", True), ("1.10.0", ">1.9", True), ("1.2.0-rc.1", ">=1.2", False), ("0.1.0", ">=0.1", True),
         ("2.0.0", "<2", False), ("1.4.2", "==1.4.2+meta", True), ("1.4.2", "1.4", True), ("0.9.9", "1.0", False)]
for have, want, ok in cases:
    assert modman.satisfies(have, want) == ok, f"{have} {want} should be {ok}"

for good in ["0.1.0", "1.2", "10.20.30", "1.0.0-alpha.1", "1.0.0+20130313144700", "1.0.0-beta+exp.sha.5114f85"]:
    assert modman.VERSION.match(good), f"{good} is a valid version"
for bad in ["D0.1", "01.2.3", "1.2.3.4", "1.2.3-", "v1.2.3", "1.2.3-beta..1", ""]:
    assert not modman.VERSION.match(bad), f"{bad} is not a valid version"

own = re.search(r"^KINETIC_VERSION=(\S+)$", open(os.path.join(ROOT, "lib/kinetic.sh")).read(), re.M).group(1)
assert re.match(r"^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$", own), f"Kinetic's own version {own} is MAJOR.MINOR.PATCH"
meson = re.search(r"version: '([^']+)'", open(os.path.join(ROOT, "modules/wall/meson.build")).read()).group(1)
assert meson == own, f"the engine says {meson}, Kinetic {own}"
for f in ["shell/app-manifest.json", "shell/kinetic/manifest.json"]:
    v = re.search(r'"version":\s*"([^"]+)"', open(os.path.join(ROOT, f)).read()).group(1)
    assert v == own, f"{f} says {v}, Kinetic {own}"
plugin = open(os.path.join(ROOT, "modules/glass/plugin/kinetic-glass.cpp")).read()
assert plugin.count(f'"{own}"') + plugin.count(f'\\"{own}\\"') >= 2, f"the Hyprland plugin does not say {own} (twice)"
print(f"versions follow Semantic Versioning (Kinetic {own})")
