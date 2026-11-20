#!/bin/bash
# Every test Kinetic has that runs without touching the desktop.
set -euo pipefail
cd "$(dirname "$0")/.."
node tests/pill.test.js
node tests/schema.test.js
node shell/kinetic/tests/model.test.js
read -ra gl <<<"$(pkg-config --cflags glesv2 egl)"
out=$(mktemp)
cc -std=gnu11 -D_GNU_SOURCE -O1 -Wall -Wno-unused-function "${gl[@]}" tests/motion_test.c -lm -o "$out"
"$out"
rm -f "$out" /tmp/kinetic-motion-test-*
out=$(mktemp)
cc -std=gnu11 -D_GNU_SOURCE -O1 -Wall -Wno-unused-function "${gl[@]}" tests/layers_test.c -o "$out"
"$out"
cc -std=gnu11 -D_GNU_SOURCE -O1 -Wall -Wno-unused-function "${gl[@]}" tests/run_test.c -lm -o "$out"
"$out"
rm -f "$out"
python3 tests/trip_test.py
python3 tests/pass_through_test.py
tests/modules_test.sh
tests/wallpapers_test.sh
python3 tests/versions_test.py
for m in modules/*/; do python3 lib/modman.py validate "$m" >/dev/null || exit 1; done
echo "every built-in module's manifest is valid"
python3 modules/bar/build.py --check
m=modules/motion/schema.json
for p in $(jq -r '.presets | keys[]' "$m"); do for pt in False True; do
  KINETIC_HOME=$PWD python3 -c "
import sys; sys.path[:0] = ['modules/motion', 'lib']
import motion, schema as S
o = S.normalize(dict(motion.SCHEMA['presets']['$p'], passThrough=$pt, passSameTime=$pt), motion.SCHEMA)
err = motion.verify(motion.render_lua(o))
if err: sys.exit('motion preset $p (pass-through $pt): ' + err)
"
done; done
echo "motion presets accepted by Hyprland, with and without pass-through"
KINETIC_WALL_PATH=modules/wall/wallpapers modules/wall/build/kinetic-wall validate >/dev/null && echo "all wallpapers compile"
# Wallpapers that move with workspaces (the engine pans them, or they read uParallax / lw_pan)
# must still show a picture at the furthest pan there is: 9 workspaces along at parallax 0.5.
moving=$(for d in modules/wall/wallpapers/*/; do
  if grep -q '^parallax *= *1' "$d/wallpaper.toml" || grep -qE 'uParallax|lw_pan' "$d"/*.frag "$d"/*.glsl "$d"/*.vert 2>/dev/null; then basename "$d"; fi
done)
for w in $moving; do
  for pan in 4.5,0 0,-4.5; do
    out=$(mktemp --suffix=.png)
    KINETIC_WALL_PATH=modules/wall/wallpapers modules/wall/build/kinetic-wall thumbnail "$w" "$out" 240x150 8 --parallax "$pan" >/dev/null 2>&1
    sd=$(magick "$out" -format '%[fx:standard_deviation]' info:); rm -f "$out"
    awk -v s="$sd" 'BEGIN { exit !(s > 0.01) }' || { echo "wallpaper $w goes flat when panned $pan screens"; exit 1; }
  done
done
echo "moving wallpapers hold up at the furthest pan: $(wc -w <<<"$moving") of them"
# One motion system: how far a wallpaper moves comes from uParallax (the motion settings), never
# from the raw workspace position. uWorkspace.x may only appear in a switch's remaining distance.
python3 - <<'PY' || exit 1
import glob, re, sys
bad = []
for f in glob.glob("modules/wall/wallpapers/*/*.[fgv][rle][ras][gl]*"):
    src = re.sub(r"uWorkspace\.([xz])\s*-\s*uWorkspace\.([xz])", "", open(f).read())
    if "uWorkspace.x" in src:
        bad.append(f)
if bad:
    sys.exit("these read the workspace position for motion; use uParallax or lw_pan: " + ", ".join(bad))
PY
echo "no wallpaper moves on its own: all follow the motion settings"
# Kinetic's own Hyprland file (the app's window rule, the drag-end event) must pass Hyprland's check.
if command -v Hyprland >/dev/null; then
  Hyprland --verify-config -c integration/kinetic-app.lua 2>&1 | grep -q "config ok" || { echo "integration/kinetic-app.lua: Hyprland rejects it"; exit 1; }
  echo "kinetic-app.lua accepted by Hyprland"
fi
# Glass previews: the bar's capsules with the new optics, over a live wallpaper and the Still background.
for args in "topo|--glass|6|1|0.3|8,6,300,30,15|bevel=8,curve=1.5,sat=1.2,bright=0.05,spec=0.6,light=90" \
            "still|--glass|10|2|0.2|40,40,300,180,10;200,120,260,160,10"; do
  IFS='|' read -r -a a <<< "$args"
  out=$(mktemp --suffix=.png)
  KINETIC_WALL_PATH=modules/wall/wallpapers modules/wall/build/kinetic-wall thumbnail "${a[0]}" "$out" 480x300 2 "${a[@]:1}" >/dev/null 2>&1 \
    || { echo "glass preview failed: ${a[*]}"; exit 1; }
  sd=$(magick "$out" -format '%[fx:standard_deviation]' info:); rm -f "$out"
  awk -v s="$sd" 'BEGIN { exit !(s > 0.01) }' || { echo "glass preview came out flat: ${a[*]}"; exit 1; }
done
echo "glass previews render (bar optics, on a live wallpaper and the Still background)"
# Kinetic's Hyprland plugin (window glass and the light) builds against the installed
# Hyprland's headers, and its shaders compile.
if command -v g++ >/dev/null && pkg-config --exists hyprland; then
  tmp=$(mktemp -d)
  modules/glass/plugin/build.sh "$tmp/kinetic-glass.so" >/dev/null 2>"$tmp/err" || { cat "$tmp/err"; echo "the Hyprland plugin does not build"; rm -rf "$tmp"; exit 1; }
  [[ -s $tmp/kinetic-glass.so.hyprland ]] || { echo "the plugin's build did not record its Hyprland"; rm -rf "$tmp"; exit 1; }
  if command -v glslangValidator >/dev/null; then
    python3 - modules/glass/plugin/kinetic-glass.cpp "$tmp" <<'PY' || { rm -rf "$tmp"; exit 1; }
import re, subprocess, sys
src = open(sys.argv[1]).read()
for name, ext in (("VERT", "vert"), ("GLASS_FRAG", "frag"), ("LIGHT_FRAG", "frag")):
    m = re.search(r'static const char\* ' + name + r' = R"\((.*?)\)";', src, re.S)
    if not m: sys.exit(f"plugin shader {name} not found")
    path = f"{sys.argv[2]}/{name}.{ext}"
    open(path, "w").write(m.group(1))
    r = subprocess.run(["glslangValidator", path], capture_output=True, text=True)
    if r.returncode: sys.exit(f"plugin shader {name}:\n{r.stdout}{r.stderr}")
PY
  fi
  rm -rf "$tmp"
  echo "the Hyprland plugin builds and its shaders compile"
fi
# Every QML file parses.
for f in shell/kinetic/*.qml shell/kinetic/pages/*.qml shell/kinetic/components/*.qml modules/bar/qml/*.qml; do
  /usr/lib/qt6/bin/qmlformat "$f" >/dev/null 2>&1 || { echo "QML does not parse: $f"; exit 1; }
done
echo "all QML parses"
# kinetic-window-blur writes a Hyprland rule Hyprland accepts (run in a scratch state dir,
# with stand-ins for the reload commands so the live Hyprland is left alone).
if command -v Hyprland >/dev/null; then
  tmp=$(mktemp -d)
  mkdir -p "$tmp/bin"
  printf '#!/bin/sh\nexit 0\n' > "$tmp/bin/omarchy-restart-hyprctl"; cp "$tmp/bin/omarchy-restart-hyprctl" "$tmp/bin/hyprctl"
  chmod +x "$tmp/bin/"*
  for mode in on see-through; do
    XDG_STATE_HOME="$tmp/state" PATH="$tmp/bin:$PATH" modules/wall/integration/window-blur "$mode"
    Hyprland --verify-config -c "$tmp/state/omarchy/toggles/hypr/zz-kinetic-windows.lua" 2>&1 | grep -q "config ok" \
      || { echo "kinetic-window-blur $mode: Hyprland rejects the rule"; rm -rf "$tmp"; exit 1; }
    grep -q "blur = { enabled = false }" "$tmp/state/omarchy/toggles/hypr/zz-kinetic-windows.lua" \
      || { echo "kinetic-window-blur $mode: does not turn Hyprland's blur off"; rm -rf "$tmp"; exit 1; }
  done
  XDG_STATE_HOME="$tmp/state" PATH="$tmp/bin:$PATH" modules/wall/integration/window-blur off
  [[ ! -e $tmp/state/omarchy/toggles/hypr/zz-kinetic-windows.lua ]] || { echo "kinetic-window-blur off left the rule"; rm -rf "$tmp"; exit 1; }
  rm -rf "$tmp"
  echo "kinetic-window-blur rules accepted by Hyprland"
fi
# Every module answers `active` (true/false), so the app's Modules page can show it and switch it.
mods=(modules/*/module.sh)
n=${#mods[@]}
KINETIC_HOME=$PWD ./kinetic modules --json | jq -e --argjson n "$n" 'length == $n and all(.[]; .active == true or .active == false)' >/dev/null \
  || { echo "kinetic modules --json: a module does not answer 'active' (each module.sh needs active/enable/disable)"; exit 1; }
for m in modules/*/module.sh; do
  grep -qE '(^|[^a-z])(active|enable|disable)\)|"active"|enable", "disable"' "$m" "$(dirname "$m")"/*.py 2>/dev/null \
    || { echo "$m: no active/enable/disable"; exit 1; }
done
echo "every module answers active/enable/disable ($n modules)"
# The bundled presets name only bar presets, motion presets, wallpapers and wallpaper presets that exist.
python3 - <<'PY' || exit 1
import glob, json, os, re, sys
bar = json.load(open("modules/bar/schema.json"))["presets"]
motion = json.load(open("modules/motion/schema.json"))["presets"]
bad = []
for f in sorted(glob.glob("modules/preset/presets/*.json")):
    p, n = json.load(open(f)), os.path.basename(f)
    w = p.get("wall", {})
    if "wallpaper" in w:
        toml = f"modules/wall/wallpapers/{w['wallpaper']}/wallpaper.toml"
        if not os.path.isfile(toml):
            bad.append(f"{n}: no wallpaper {w['wallpaper']}")
        elif "preset" in w and not re.search(r'^\[preset\.(?:"%s"|%s)\]' % (re.escape(w["preset"]), re.escape(w["preset"])), open(toml).read(), re.M):
            bad.append(f"{n}: {w['wallpaper']} has no preset {w['preset']}")
    if "preset" in p.get("bar", {}) and p["bar"]["preset"] not in bar:
        bad.append(f"{n}: no bar preset {p['bar']['preset']}")
    if "preset" in p.get("motion", {}) and p["motion"]["preset"] not in motion:
        bad.append(f"{n}: no motion preset {p['motion']['preset']}")
if bad:
    print("\n".join(bad)); sys.exit(1)
print(f"bundled presets name only what exists ({len(glob.glob('modules/preset/presets/*.json'))} presets)")
PY
