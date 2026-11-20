#!/usr/bin/env python3
"""kinetic motion: window and workspace animations, and wallpaper parallax.

Options live in ~/.config/kinetic/motion.json. Applying them writes one
Hyprland toggle file (~/.local/state/omarchy/toggles/hypr/kinetic-motion.lua),
checked first with Hyprland's own `--verify-config`, and tells the wallpaper
engine the workspace duration and curve, so the wallpaper slides in step with
the windows at a fraction of their distance: parallax.

The toggle file sorts before Omarchy's no-animations.lua, so `omarchy toggle
animations` still turns every animation off.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
KINETIC = os.environ.get("KINETIC_HOME") or os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(KINETIC, "lib"))
import schema as S  # noqa: E402
sys.path.insert(0, HERE)
import trip  # noqa: E402

HOME = os.path.expanduser("~")
CONFIG = os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.join(HOME, ".config"), "kinetic", "motion.json")
STATE = os.environ.get("XDG_STATE_HOME") or os.path.join(HOME, ".local/state")
TOGGLE = os.path.join(STATE, "omarchy/toggles/hypr/kinetic-motion.lua")
SIGNAL = os.path.join(STATE, "kinetic/signals/motion")  # the engine reads this at start
SCHEMA = S.load(os.path.join(HERE, "schema.json"))
BIN = os.environ.get("KINETIC_BIN") or os.path.join(HOME, ".local/bin")

USAGE = """Usage: kinetic motion <command> [args]

  on                      Use Kinetic's animations (and wallpaper parallax)
  off                     Back to Omarchy's own animations; the wallpaper stops panning
  preset <name>           {presets}
  set <key> <value>       Set an option (checked against the schema)
  unset <key>             Back to its default
  get                     The options you have set, and whether motion is on
  options                 Every option with its range and default
  reset                   Clear every option
  show                    The Hyprland config and engine setting that would be applied
  apply                   Write them again (after editing motion.json by hand)
"""


def fail(msg):
    print(f"kinetic motion: {msg}", file=sys.stderr)
    sys.exit(1)


# ---------- the stored options ----------

def read_config():
    try:
        with open(CONFIG, encoding="utf-8") as f:
            c = json.load(f)
        if isinstance(c, dict):
            return {"enabled": c.get("enabled") is not False, "options": c.get("options") if isinstance(c.get("options"), dict) else {}}
    except (OSError, ValueError):
        pass
    return {"enabled": False, "options": {}}


def write_config(c):
    os.makedirs(os.path.dirname(CONFIG), exist_ok=True)
    tmp = CONFIG + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(c, f, indent=2, sort_keys=True)
        f.write("\n")
    os.replace(tmp, CONFIG)


# ---------- what it becomes ----------

def num(v):
    return ("%.3f" % v).rstrip("0").rstrip(".")


def curve_lua(name):
    c = SCHEMA["curves"][name]
    if c["type"] == "spring":
        return f'hl.curve("kinetic_{name}", {{ type = "spring", mass = {num(c["mass"])}, stiffness = {num(c["stiffness"])}, dampening = {num(c["dampening"])} }})'
    p = c["points"]
    return f'hl.curve("kinetic_{name}", {{ type = "bezier", points = {{ {{ {num(p[0])}, {num(p[1])} }}, {{ {num(p[2])}, {num(p[3])} }} }} }})'


def anim_table(leaf, ms=None, curve=None, style=None, enabled=True):
    if not enabled:
        return f'{{ leaf = "{leaf}", enabled = false }}'
    kind = "spring" if SCHEMA["curves"][curve]["type"] == "spring" else "bezier"
    s = f'{{ leaf = "{leaf}", enabled = true, speed = {num(ms / 100)}, {kind} = "kinetic_{curve}"'
    return s + (f', style = "{style}" }}' if style else " }")


def animation(leaf, ms=None, curve=None, style=None, enabled=True):
    return f"hl.animation({anim_table(leaf, ms, curve, style, enabled)})"


def pts(p, digits=4):
    return ",".join(("%.*f" % (digits, v)).rstrip("0").rstrip(".") or "0" for v in p)


def lua_curve(name, p):
    v = pts(p).split(",")
    return f'hl.curve("{name}", {{ type = "bezier", points = {{ {{ {v[0]}, {v[1]} }}, {{ {v[2]}, {v[3]} }} }} }})'


def pass_through_lua(o, style):
    """Super + 1-0 steps through every workspace between, on the switch curve, then
    the usual animation comes back. See trip.py for how a trip is cut into steps."""
    curve = SCHEMA["curves"][o["workspaceCurve"]]
    normal = anim_table("workspaces", o["workspaceMs"], o["workspaceCurve"], style)
    base = trip.base_ms(curve, o["workspaceMs"])
    curves, ends, shapes = [], [], []
    for n in range(2, trip.MAX_STEPS + 1):
        e, cs = trip.steps(curve, n)
        curves += [lua_curve(f"kinetic_trip_{n}_{k + 1}", c) for k, c in enumerate(cs)]
        ends.append(f"[{n}] = {{ {', '.join(pts([v], 5) for v in e)} }}")
        shapes.append(f'[{n}] = "{pts(trip.trip_points(curve, n))}"')
    return f"""
-- Pass-through: Super + 1-0 slides past every workspace between. The whole trip
-- is one run of the switch curve: Hyprland plays it a workspace at a time, each
-- step on its own slice of the curve (kinetic_trip_<n>_<step>), so the slices
-- join into the curve. The wallpaper engine is told the trip first (a custom
-- event, with the curve and the workspace it ends on) and traces the same path.
-- Needs Omarchy's per-monitor workspaces.
--
-- A trip always ends on the workspace chosen: its last step is Omarchy's own
-- focus of that slot on the trip's monitor, checked afterward. A newer press
-- takes over from wherever the trip has got to; another workspace key, the
-- bar or an app switching meanwhile wins, and the trip gives way.
{chr(10).join(curves)}
if o and o.workspaces and o.workspaces.workspace_id and o.workspaces.focus and o.rebind then
  local BASE, STEP, SAME, STYLE = {base}, {o["passMs"]}, {"true" if o["passSameTime"] else "false"}, "{style}"
  local CURVES = {{ {", ".join(shapes)} }}  -- the curve of each trip length, for the engine
  local ENDS = {{ {", ".join(ends)} }}  -- when each step ends, as fractions of the trip
  local NORMAL = {normal}
  local trips = {{}}  -- each monitor's latest trip, until it settles; an older one's timers find it isn't and stop
  local lands = {{}}  -- Omarchy's focus of a slot on a named monitor, made once for each
  local ours = false  -- true while a trip itself switches workspaces

  local function animate(curve, ms)
    hl.animation({{ leaf = "workspaces", enabled = true, speed = ms / 100, bezier = curve, style = STYLE }})
  end

  local function shown(name)  -- the workspace a monitor shows (0 for none)
    local ws = hl.get_active_workspace(name)
    return ws and not ws.special and ws.id or 0
  end

  local function focused(name)
    local m = hl.get_active_monitor()
    return m ~= nil and m.name == name
  end

  local function block(id) return math.floor((id - 1) / 10) end

  -- A workspace between that another monitor shows or holds: stepping onto it
  -- would take the focus over there.
  local function elsewhere(name, from, to)
    local d = to > from and 1 or -1
    for id = from + d, to - d, d do
      local ws = hl.get_workspace(id)
      if ws and ws.monitor and ws.monitor.name ~= name then return true end
    end
    return false
  end

  -- The trip is over: the usual animation is back, and the engine is called off
  -- when it would travel somewhere Hyprland isn't going.
  local function stop(t, cancel)
    if trips[t.monitor] ~= t then return end
    trips[t.monitor] = nil
    local busy = false  -- another monitor's trip still stepping keeps its slice
    for _, x in pairs(trips) do busy = busy or not x.landed end
    if not busy then hl.animation(NORMAL) end
    if cancel then hl.dispatch(hl.dsp.event("kinetic-travel " .. t.monitor .. " 0")) end
  end

  -- The last step: the chosen slot on the trip's own monitor, the way Omarchy
  -- focuses it (bringing the workspace back from another monitor if need be),
  -- and checked. A trip cut short lands at once, and the engine, still on its
  -- way, is called off to ease on to the same workspace.
  local function land(t, early)
    t.landed, t.pos = true, t.target
    local key = t.slot .. "@" .. t.monitor
    lands[key] = lands[key] or o.workspaces.focus(t.slot, t.monitor)
    ours = true
    local ok, err = pcall(function()
      lands[key]()
      if shown(t.monitor) ~= t.target and focused(t.monitor) then
        hl.dispatch(hl.dsp.focus({{ workspace = tostring(t.target) }}))
      end
    end)
    ours = false
    if early then hl.dispatch(hl.dsp.event("kinetic-travel " .. t.monitor .. " 0")) end
    if not ok then error(err) end
  end

  -- Hyprland tells Lua of every switch as it happens, so one the trip didn't
  -- make on its monitor (another key, the bar, an app asking for focus) is
  -- known, even one away and straight back between two steps.
  if hl.on then
    hl.on("workspace.active", function(ws)
      local t = ws and ws.monitor and trips[ws.monitor.name]
      if t and not ours then t.strayed = true end
    end)
  end

  local function strayed(t)
    return t.strayed or shown(t.monitor) ~= t.pos
  end

  local function step(t, k)
    if trips[t.monitor] ~= t or t.landed or k <= t.k then return end
    local ok = pcall(function()
      if strayed(t) then return stop(t, true) end  -- something else switched: it wins
      if not focused(t.monitor) then
        -- the focus went to another monitor (the pointer, a click): the trip
        -- ends where it was going, and the focus goes back where it went
        local away = hl.get_active_monitor()
        land(t, true)
        if away and away.name ~= t.monitor then hl.dispatch(hl.dsp.focus({{ monitor = away.name }})) end
        return
      end
      t.k = k
      animate("kinetic_trip_" .. t.n .. "_" .. k, t.ms[k])
      local id = t.from + t.dir * k
      local ws = hl.get_workspace(id)
      if k == t.n then return land(t) end
      if ws and ws.monitor and ws.monitor.name ~= t.monitor then return land(t, true) end  -- now held elsewhere
      t.pos = id
      ours = true
      hl.dispatch(hl.dsp.focus({{ workspace = tostring(id) }}))
      ours = false
    end)
    ours = false
    if not ok and trips[t.monitor] == t and not t.landed then pcall(land, t, true) end  -- a step that fails still lands
  end

  -- When the last step's slide is over: back to the usual animation, and a last
  -- look. A trip that never landed (a step lost) lands now. A switch since the
  -- landing (another key, the bar) stands, and the engine is called off.
  local function settle(t)
    if trips[t.monitor] ~= t then return end
    if not t.landed then pcall(land, t, true) end
    stop(t, t.strayed or shown(t.monitor) ~= t.target)
  end

  local function travel(slot)
    local focus = o.workspaces.focus(slot)
    local fn = function()
      local monitor = hl.get_active_monitor()
      if not monitor then return focus() end
      local name = monitor.name
      local target = o.workspaces.workspace_id(monitor, slot)
      for other, x in pairs(trips) do
        if strayed(x) then  -- something else switched since its last step: that trip is over
          stop(x, true)
        elseif other ~= name and not x.landed then
          -- a trip still stepping on another monitor ends where it was going; the focus stays here
          pcall(land, x, true)
          hl.dispatch(hl.dsp.focus({{ monitor = name }}))
        end
      end
      local old = trips[name]
      if old and old.target == target and not old.landed then return end  -- on its way there
      -- from wherever this monitor is now, a trip's step or not
      local from = shown(name)
      local n = math.abs(target - from)
      if from <= 0 or n < 2 or not ENDS[n] or block(from) ~= block(target) or elsewhere(name, from, target) then
        -- the same workspace again leaves the trip to finish (the engine is still on its way)
        if old and target ~= old.target then stop(old, true) end
        return focus()
      end
      local dir = target > from and 1 or -1
      -- one switch's time, plus the time per workspace passed for each one beyond the first
      local total = SAME and BASE or BASE + (n - 1) * STEP
      local t = {{ monitor = name, slot = slot, from = from, target = target, dir = dir, n = n, k = 0, pos = from, ms = {{}} }}
      local ends = ENDS[n]
      for k = 1, n do t.ms[k] = math.max(10, math.floor(total * (ends[k] - (ends[k - 1] or 0)) + 0.5)) end
      trips[name] = t
      hl.dispatch(hl.dsp.event(string.format("kinetic-travel %s %d %d %d %s %d", name, n, dir, total, CURVES[n], target)))
      -- every step's timer is set now, at its time from the press, so late timers don't add up
      local at = 0
      for k = 2, n do
        at = at + t.ms[k - 1]
        hl.timer(function() step(t, k) end, {{ type = "oneshot", timeout = at }})
      end
      hl.timer(function() settle(t) end, {{ type = "oneshot", timeout = at + t.ms[n] + 50 }})
      step(t, 1)
    end
    if o.bind_expressions then o.bind_expressions[fn] = "o.workspaces.focus(" .. slot .. ")" end
    return fn
  end

  for slot = 1, 10 do
    o.rebind("SUPER + code:" .. tostring(slot + 9), "Switch to workspace " .. slot, travel(slot))
  end
end
"""


def window_style(kind, popin):
    return {"popin": f"popin {popin}%", "slide": "slide", "gnomed": "gnomed"}[kind]


def render_lua(o):
    used, lines = [], []

    def anim(leaf, ms=None, curve=None, style=None, enabled=True):
        if enabled and curve not in used:
            used.append(curve)
        lines.append(animation(leaf, ms, curve, style, enabled))

    ws = o["workspaces"]
    if ws == "off":
        anim("workspaces", enabled=False)
    else:
        ws_style = {"slide": "slide", "slidevert": "slidevert", "fade": "fade",
                    "slidefade": f"slidefade {o['slideFade']}%", "slidefadevert": f"slidefadevert {o['slideFade']}%"}[ws]
        anim("workspaces", o["workspaceMs"], o["workspaceCurve"], ws_style)
    for leaf, key in (("windowsIn", "windowsIn"), ("windowsOut", "windowsOut")):
        if o[key] == "off":
            anim(leaf, enabled=False)
        elif leaf == "windowsIn":
            anim(leaf, o["windowMs"], o["windowCurve"], window_style(o[key], o["popin"]))
        else:
            anim(leaf, o["closeMs"], "linear", window_style(o[key], o["popin"]))
    anim("windowsMove", o["moveMs"], o["windowCurve"], "slide")
    if not o["fade"]:
        anim("fadeIn", enabled=False)
        anim("fadeOut", enabled=False)
    if o["layers"] == "off":
        anim("layersIn", enabled=False)
        anim("layersOut", enabled=False)
    else:
        style = {"fade": "fade", "slide": "slide", "popin": "popin 90%"}[o["layers"]]
        anim("layersIn", o["layerMs"], o["windowCurve"], style)
        anim("layersOut", max(50, round(o["layerMs"] * 0.4)), "linear", style)

    head = ["-- Kinetic motion: Hyprland animations.",
            "-- Written by `kinetic motion` from ~/.config/kinetic/motion.json; do not edit.",
            "-- Loads before Omarchy's no-animations.lua, which still turns everything off.", ""]
    curves = [curve_lua(c) for c in used]
    cfg = ["", f"hl.config({{ animations = {{ workspace_wraparound = {'true' if o['wraparound'] else 'false'} }} }})"]
    if o["passThrough"] and ws not in ("off", "fade"):
        cfg.append(pass_through_lua(o, ws_style).rstrip())
    return "\n".join(head + curves + [""] + lines + cfg) + "\n"


def curve_spec(name, sep=" "):
    """A curve as the engine reads it: `bezier x1,y1,x2,y2` or `spring mass,stiffness,dampening`."""
    c = SCHEMA["curves"][name]
    if c["type"] == "spring":
        return "spring" + sep + ",".join(num(c[k]) for k in ("mass", "stiffness", "dampening"))
    return "bezier" + sep + ",".join(num(p) for p in c["points"])


def window_spec(o):
    """The window animations, so the glass behind a window opens, closes and moves with it:
    open=<ms>/<curve>/<style> move=<ms>/<curve> close=<ms>/<curve>/<style> fade=on|off,
    the same durations, curves and styles render_lua gives Hyprland."""
    def style(kind):
        return {"popin": f"popin:{o['popin']}", "slide": "slide", "gnomed": "gnomed"}[kind]
    out = []
    out.append("open=off" if o["windowsIn"] == "off" else
               f"open={o['windowMs']}/{curve_spec(o['windowCurve'], '/')}/{style(o['windowsIn'])}")
    out.append(f"move={o['moveMs']}/{curve_spec(o['windowCurve'], '/')}")
    out.append("close=off" if o["windowsOut"] == "off" else
               f"close={o['closeMs']}/{curve_spec('linear', '/')}/{style(o['windowsOut'])}")
    out.append("fade=" + ("on" if o["fade"] else "off"))
    return " ".join(out)


def engine_spec(o):
    """What the wallpaper engine is told: `motion off`, or duration, curve, parallax, axis,
    then the window animations either way."""
    ws = o["workspaces"]
    if ws == "off":
        return "off " + window_spec(o)
    curve = curve_spec(o["workspaceCurve"])
    parallax = 0 if ws == "fade" else o["parallax"]
    axis = "v" if ws.endswith("vert") else "h"
    # how Hyprland moves the workspaces, so window glass can move with them
    style = "fade" if ws == "fade" else f"slidefade:{o['slideFade']}" if ws.startswith("slidefade") else "slide"
    return f"{o['workspaceMs']} {curve} {num(parallax)} {axis} {style} {window_spec(o)}"


# ---------- putting it in place ----------

# Checked alone, the file has no Omarchy helpers (`o`); this stand-in lets the
# pass-through block run, so its binds are checked by Hyprland's own code.
VERIFY_STUB = """o = o or { bind_expressions = {},
  workspaces = { workspace_id = function(m, s) return s end, focus = function(s) return function() end end },
  rebind = function(keys, desc, fn) hl.unbind(keys); hl.bind(keys, fn, { description = desc }) end }
"""


def verify(lua):
    """Hyprland's own checker, on the file alone; returns an error or ''."""
    hypr = shutil.which("Hyprland")
    if not hypr:
        return ""
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as f:
        f.write(VERIFY_STUB + lua)
        path = f.name
    try:
        r = subprocess.run([hypr, "--verify-config", "-c", path], capture_output=True, text=True, timeout=20)
        out = (r.stdout + r.stderr).replace("\x1b", "")
        if "config ok" in out:
            return ""
        last = [l for l in out.splitlines() if l.strip()]
        return last[-1] if last else "Hyprland rejected the config"
    except (OSError, subprocess.TimeoutExpired) as e:
        return f"could not check the config: {e}"
    finally:
        os.unlink(path)


def write_if_changed(path, text):
    try:
        with open(path, encoding="utf-8") as f:
            if f.read() == text:
                return False
    except OSError:
        pass
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path + ".tmp", "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(path + ".tmp", path)
    return True


def remove(path):
    try:
        os.unlink(path)
        return True
    except FileNotFoundError:
        return False


def reload_hyprland():
    subprocess.run(["omarchy-restart-hyprctl"], capture_output=True)
    r = subprocess.run(["hyprctl", "configerrors"], capture_output=True, text=True)
    errors = [l for l in r.stdout.splitlines() if "kinetic-motion" in l]
    if errors:
        remove(TOGGLE)
        subprocess.run(["omarchy-restart-hyprctl"], capture_output=True)
        fail("Hyprland reported " + errors[0] + "; Kinetic's animations were taken out again")


def tell_engine(spec):
    write_if_changed(SIGNAL, spec + "\n")
    wall = os.path.join(BIN, "kinetic-wall")
    if not os.access(wall, os.X_OK):
        wall = os.path.join(KINETIC, "modules/wall/build/kinetic-wall")
    if os.access(wall, os.X_OK):
        subprocess.run([wall, "motion"] + spec.split(), capture_output=True)


def apply(c):
    if not c["enabled"]:
        changed = remove(TOGGLE)
        tell_engine("off")
        if changed:
            reload_hyprland()
        return
    o = S.normalize(c["options"], SCHEMA)
    lua = render_lua(o)
    err = verify(lua)
    if err:
        fail(f"Hyprland would not accept these animations: {err}")
    if write_if_changed(TOGGLE, lua):
        reload_hyprland()
    tell_engine(engine_spec(o))


# ---------- commands ----------

def main(argv):
    cmd = argv[1] if len(argv) > 1 else "help"
    args = argv[2:]
    c = read_config()
    if cmd in ("help", "-h", "--help"):
        print(USAGE.format(presets=" | ".join(SCHEMA["presets"])), end="")
        return 0
    if cmd == "active":
        print("true" if c["enabled"] else "false")
        return 0
    if cmd in ("enable", "disable"):
        cmd = "on" if cmd == "enable" else "off"
    if cmd == "on":
        c["enabled"] = True
    elif cmd == "off":
        c["enabled"] = False
    elif cmd == "preset":
        if not args or args[0] not in SCHEMA["presets"]:
            fail("presets: " + ", ".join(SCHEMA["presets"]))
        c = {"enabled": True, "options": dict(SCHEMA["presets"][args[0]])}
    elif cmd == "set":
        if len(args) != 2:
            fail("set <key> <value>")
        try:
            c["options"][args[0]] = S.check(SCHEMA, args[0], args[1])
        except ValueError as e:
            fail(str(e))
        c["enabled"] = True
    elif cmd == "unset":
        if not args or not S.option(SCHEMA, args[0]):
            fail("unset <key> (see: kinetic motion options)")
        c["options"].pop(args[0], None)
    elif cmd == "reset":
        c["options"] = {}
    elif cmd == "get":
        print(json.dumps(c, indent=2, sort_keys=True))
        return 0
    elif cmd == "options":
        for o in SCHEMA["options"]:
            rng = "|".join(o["options"]) if o["type"] == "choice" else ("true|false" if o["type"] == "bool" else f"{o['min']}..{o['max']}")
            print(f"{o['key']:<15} {rng:<48} {str(o['default']):<7} {o.get('description') or o['label']}")
        return 0
    elif cmd == "show":
        o = S.normalize(c["options"], SCHEMA)
        print(f"-- motion is {'on' if c['enabled'] else 'off'}\n")
        print(render_lua(o))
        print(f"-- engine: motion {engine_spec(o)}")
        return 0
    elif cmd == "apply":
        pass
    else:
        print(USAGE.format(presets=" | ".join(SCHEMA["presets"])), end="", file=sys.stderr)
        return 2
    if cmd not in ("apply",):
        write_config(c)
    apply(c)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
