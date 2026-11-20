#!/usr/bin/env python3
"""Runs tests/pass_through_test.lua on the Lua `kinetic motion` writes with pass-through
on: every preset that slides, with and without the same time for every switch, and
a short switch with many steps at the shortest time (where step times hit their 10 ms
floor). Needs Lua 5.3 or later and Omarchy's monitor-workspaces.lua; says so and
passes when either is missing."""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path[:0] = [os.path.join(ROOT, "modules/motion"), os.path.join(ROOT, "lib")]
os.environ.setdefault("KINETIC_HOME", ROOT)
import motion  # noqa: E402
import schema as S  # noqa: E402

OMARCHY = "/usr/share/omarchy/default/hypr/monitor-workspaces.lua"


def lua():
    for name in ("lua5.4", "lua5.5", "lua"):
        exe = shutil.which(name)
        if exe:
            v = subprocess.run([exe, "-e", "print(_VERSION)"], capture_output=True, text=True).stdout
            if v.startswith("Lua 5.") and int(v.split(".")[1]) >= 3:
                return exe
    return None


def main():
    exe = lua()
    if not exe or not os.path.exists(OMARCHY):
        print("pass-through trips: skipped (" + ("no Lua 5.3+" if not exe else "no " + OMARCHY) + ")")
        return 0
    configs = [("glide 200 ms, same time", dict(workspaces="slide", workspaceCurve="glide", workspaceMs=200, passSameTime=True), None)]
    for name, preset in motion.SCHEMA["presets"].items():
        if preset.get("workspaces", "slide") in ("off", "fade"):
            continue
        for same in (False, True):
            configs.append((f"{name}{', same time' if same else ''}", dict(preset, passSameTime=same), "500"))
    configs.append(("shortest", dict(workspaces="slide", workspaceCurve="snappy", workspaceMs=80, passSameTime=True), "1500"))
    bad, total = 0, 0
    with tempfile.TemporaryDirectory() as tmp:
        for label, opts, scenarios in configs:
            o = S.normalize(dict(opts, passThrough=True), motion.SCHEMA)
            path = os.path.join(tmp, "kinetic-motion.lua")
            with open(path, "w") as f:
                f.write(motion.render_lua(o))
            env = dict(os.environ)
            if scenarios:  # the presets: fewer random runs, the deliberate sweeps sampled
                env["PASS_THROUGH_SCENARIOS"] = scenarios
                env["PASS_THROUGH_STRIDE"] = "4"
            r = subprocess.run([exe, os.path.join(ROOT, "tests/pass_through_test.lua"), path, OMARCHY],
                               capture_output=True, text=True, env=env)
            out = (r.stdout + r.stderr).strip()
            if r.returncode:
                bad += 1
                print(f"{label}:\n{out}")
            else:
                total += int(out.splitlines()[-1].split(": ")[1].split()[0])
    if not bad:
        print(f"pass-through trips land where chosen: {len(configs)} motion settings, {total} scenarios")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
