-- Pass-through trips, run for real: the Lua `kinetic motion` writes, loaded into a
-- stand-in Hyprland (timers on a simulated clock, workspaces made and dropped as
-- Hyprland does, monitors, focus) with Omarchy's own per-monitor workspaces
-- (/usr/share/omarchy/default/hypr/monitor-workspaces.lua) and a model of the
-- wallpaper engine reading Hyprland's events in batches, as kinetic-wall does.
--
-- Every scenario presses keys at chosen or random moments (long trips, presses
-- mid-trip, during the last step, reversing, from another monitor, the mouse
-- moving to another monitor, other workspace keys) under timer lateness, and
-- checks that each monitor ends on the last workspace chosen for it, that the
-- focus is where the last action put it, that the usual animation is back, and
-- that the engine's trip ended where Hyprland did.
--
-- Usage: lua tests/pass_through_test.lua <generated kinetic-motion.lua> [monitor-workspaces.lua]

local LUA_FILE = assert(arg[1], "usage: pass_through_test.lua <kinetic-motion.lua> [monitor-workspaces.lua]")
local OMARCHY_WS = arg[2] or "/usr/share/omarchy/default/hypr/monitor-workspaces.lua"
local SCENARIOS = tonumber(os.getenv("PASS_THROUGH_SCENARIOS") or "") or 3000
local STRIDE = tonumber(os.getenv("PASS_THROUGH_STRIDE") or "") or 1 -- >1 samples the deliberate sweeps more sparsely

local W -- the simulated world, made afresh for each scenario

---------------------------------------------------------------- the clock

local function schedule(delay, fn, kind)
  W.seq = W.seq + 1
  local ev = { due = W.now + delay, seq = W.seq, fn = fn, kind = kind, on = true }
  W.queue[#W.queue + 1] = ev
  return ev
end

local function next_event()
  local best, bi = nil, nil
  for i, ev in ipairs(W.queue) do
    if not best or ev.due < best.due or (ev.due == best.due and ev.seq < best.seq) then
      best, bi = ev, i
    end
  end
  if bi then table.remove(W.queue, bi) end
  return best
end

---------------------------------------------------------------- Hyprland's state

local function mon(name)
  for _, m in ipairs(W.monitors) do
    if m.name == name then return m end
  end
end

local function focused_mon()
  for _, m in ipairs(W.monitors) do
    if m.focused then return m end
  end
end

local TRACE = os.getenv("PASS_THROUGH_TRACE") -- a scenario's name: print what happens in it

local function emit(line)
  if W.trace then print(string.format("%7.1f  %s", W.now, line)) end
  W.socket[#W.socket + 1] = line
  if not W.engine_wake then
    W.engine_wake = schedule(W.rand() * W.engine_delay, function() W.engine_read() end, "engine")
  end
end

local function ws_obj(id, shallow)
  local w = W.ws[id]
  if not w then return nil end
  local m = mon(w.monitor)
  local o = { id = id, name = tostring(id), special = false, windows = w.windows,
              active = m and m.active == id or false, visible = m and m.active == id or false }
  o.monitor = m and { name = m.name, id = m.id, focused = m.focused } or nil
  return o
end

local mon_obj

local function set_workspace(self, spec)
  local id = tonumber(spec.workspace)
  local m = mon(self.name)
  if id and m then W.show(m, id) end
end

function mon_obj(m)
  return { name = m.name, id = m.id, description = m.description, focused = m.focused, is_mirror = false,
           active_workspace = ws_obj(m.active), set_workspace = set_workspace }
end

-- Hyprland drops an empty workspace once nothing shows it.
local function sweep()
  for id, w in pairs(W.ws) do
    local shown = false
    for _, m in ipairs(W.monitors) do
      if m.active == id then shown = true end
    end
    if not shown and w.windows == 0 then
      W.ws[id] = nil
      emit("destroyworkspace>>" .. id)
    end
  end
end

-- Monitor m shows workspace id (made on m when it doesn't exist).
function W_show(m, id)
  local w = W.ws[id]
  if not w then
    W.ws[id] = { windows = 0, monitor = m.name }
    emit("createworkspace>>" .. id)
  elseif w.monitor ~= m.name then
    w.monitor = m.name
  end
  if m.active ~= id then
    m.active = id
    W.visits[m.name] = W.visits[m.name] or {}
    table.insert(W.visits[m.name], id)
    emit("workspace>>" .. id)
    for _, fn in ipairs(W.handlers["workspace.active"] or {}) do fn(ws_obj(id)) end -- at once, as Hyprland does
  end
  sweep()
end

local function focus_monitor(m)
  if not m.focused then
    for _, x in ipairs(W.monitors) do x.focused = (x == m) end
    emit("focusedmon>>" .. m.name)
  end
end

-- Hyprland's `workspace` dispatcher: a workspace shown elsewhere takes the focus
-- to its monitor; one that doesn't exist yet is made on the focused monitor.
local function focus_workspace(sel)
  local fm = focused_mon()
  local id = tonumber(sel)
  if not id then
    local d = sel == "m+1" and 1 or sel == "m-1" and -1 or nil
    assert(d, "unsupported workspace selector " .. tostring(sel))
    local ids = {}
    for wid, w in pairs(W.ws) do
      if w.monitor == fm.name then ids[#ids + 1] = wid end
    end
    table.sort(ids)
    for i, wid in ipairs(ids) do
      if wid == fm.active then id = ids[(i - 1 + d) % #ids + 1] end
    end
  end
  local w = W.ws[id]
  local m = w and mon(w.monitor) or fm
  focus_monitor(m)
  W.show(m, id)
end

local function move_workspace(id, to)
  local w = W.ws[id]
  local dest = mon(to)
  if not w or not dest or w.monitor == to then return end
  local from = mon(w.monitor)
  w.monitor = to
  if from and from.active == id then -- the monitor it left shows another, or a new one
    local best
    for wid, x in pairs(W.ws) do
      if x.monitor == from.name and (not best or wid < best) then best = wid end
    end
    if not best then
      best = 1
      while W.ws[best] do best = best + 1 end
    end
    W.show(from, best)
  end
  W.show(dest, id)
end

---------------------------------------------------------------- the engine (kinetic-wall's motion.c, in short)

local function engine_pos(e)
  local t = e.travel
  if not t then return e.ws_id end
  local x = math.min(1, (W.now - t.t0) / t.secs)
  return t.from + (t.to - t.from) * x
end

local function engine_tick()
  for _, e in pairs(W.engine) do
    if e.travel and W.now - e.travel.t0 >= e.travel.secs then
      e.arrived = e.travel.to -- the wallpaper's trip is over: it rests here
      if W.trace then print(string.format("%7.1f  engine arrives at %d (at %.1f)", W.now, e.travel.to, e.travel.t0 + e.travel.secs)) end
      e.travel = nil
    end
  end
end

local function engine_travel(args)
  local name, n, dir, ms, curve, to = args:match("^(%S+)%s+(%-?%d+)%s*(%-?%d*)%s*(%d*)%s*(%S*)%s*(%-?%d*)$")
  if not name then name, n = args:match("^(%S+)%s+(%-?%d+)$") end
  local e = name and W.engine[name]
  if not e then return end
  n, dir, ms = tonumber(n), tonumber(dir), tonumber(ms)
  W.travel_events[#W.travel_events + 1] = { name = name, n = n, to = tonumber(to), t = W.now }
  if not n or n < 2 or not ms or curve == "" then -- called off: eases on from where it is
    e.travel, e.arrived = nil, nil
    return
  end
  local dest = (to and to ~= "") and tonumber(to) or e.ws_id + dir * n
  e.travel = { t0 = W.now, from = engine_pos(e), to = dest, secs = math.max(10, ms) }
  if W.trace then print(string.format("%7.1f  engine travels %s from %.2f to %d", W.now, name, e.travel.from, dest)) end
  e.arrived = nil
end

local function engine_query()
  for _, m in ipairs(W.monitors) do
    local e = W.engine[m.name]
    if e and m.active ~= e.ws_id then
      local t = e.travel
      if t then
        local lo, hi = math.min(t.from, t.to) - 0.5, math.max(t.from, t.to) + 0.5
        if m.active < lo or m.active > hi then e.travel = nil end -- anything else ends the trip
      end
      if not e.travel then e.arrived = nil end
      e.ws_id = m.active
    end
  end
end

-- Reads everything Hyprland has written since the last read, then asks for the
-- state once, as mod_hyprland.c's on_event does.
local function engine_read()
  W.engine_wake = nil
  engine_tick()
  local lines = W.socket
  W.socket = {}
  local need = false
  for _, line in ipairs(lines) do
    local travel = line:match("^custom>>kinetic%-travel (.*)$")
    if travel then
      engine_travel(travel)
    elseif line:match("^workspace") or line:match("^focusedmon") or line:match("^destroyworkspace")
      or line:match("^createworkspace") or line:match("^moveworkspace") then
      need = true
    end
  end
  if need then engine_query() end
end

---------------------------------------------------------------- the stand-in hl

local function dispatch(d)
  if type(d) == "function" then return d() end
  if d.kind == "focus" then
    if d.a.workspace then
      focus_workspace(d.a.workspace)
    elseif d.a.monitor then
      local m = mon(d.a.monitor)
      if m then focus_monitor(m) end
    end
  elseif d.kind == "event" then
    emit("custom>>" .. d.s)
  elseif d.kind == "wsmove" then
    local id = tonumber(d.a.workspace) or focused_mon().active
    move_workspace(id, d.a.monitor)
  elseif d.kind == "change_id" then
    local old, new = tonumber(d.a.workspace), tonumber(d.a.id)
    W.ws[new], W.ws[old] = W.ws[old], nil
    for _, m in ipairs(W.monitors) do
      if m.active == old then m.active = new end
    end
  end
end

local function make_hl()
  local hl = {}
  hl.dsp = {
    focus = function(a) return { kind = "focus", a = a } end,
    event = function(s) return { kind = "event", s = s } end,
    exec_cmd = function(c) return { kind = "exec", c = c } end,
    workspace = {
      move = function(a) return { kind = "wsmove", a = a } end,
      change_id = function(a) return { kind = "change_id", a = a } end,
      toggle_special = function(a) return { kind = "noop" } end,
    },
    window = { move = function(a) return { kind = "noop" } end },
  }
  hl.dispatch = dispatch
  hl.timer = function(fn, opts)
    assert(type(opts.timeout) == "number" and opts.timeout > 0, "hl.timer: timeout must be > 0")
    assert(opts.type == "oneshot", "only oneshot timers are used")
    W.timers_made = W.timers_made + 1
    local ev = schedule(opts.timeout, fn, "timer")
    return { set_enabled = function(_, on) ev.on = on end, is_enabled = function() return ev.on end }
  end
  hl.animation = function(t)
    assert(type(t) == "table")
    if t.leaf == "workspaces" then
      if not W.normal then W.normal = t end
      if t.enabled ~= false then assert(t.speed > 0, "animation speed must be > 0") end
      if t.bezier then assert(W.curves[t.bezier], "no such bezier " .. tostring(t.bezier)) end
      W.anim = t
    end
  end
  hl.curve = function(name, spec) W.curves[name] = spec end
  hl.config = function() end
  hl.bind = function(keys, fn) W.binds[keys] = fn end
  hl.unbind = function(keys) W.binds[keys] = nil end
  hl.on = function(name, fn)
    W.handlers[name] = W.handlers[name] or {}
    table.insert(W.handlers[name], fn)
  end
  hl.get_config = function() return 1 end
  hl.get_active_window = function() return nil end
  hl.get_window = function() return nil end
  hl.get_active_monitor = function()
    local m = focused_mon()
    return m and mon_obj(m)
  end
  hl.get_monitor = function(sel)
    local m = type(sel) == "table" and mon(sel.name) or mon(sel)
    return m and mon_obj(m)
  end
  hl.get_monitors = function()
    local list = {}
    for _, m in ipairs(W.monitors) do list[#list + 1] = mon_obj(m) end
    return list
  end
  hl.get_active_workspace = function(sel)
    local m = sel and (type(sel) == "table" and mon(sel.name) or mon(sel)) or focused_mon()
    return m and ws_obj(m.active)
  end
  hl.get_workspace = function(sel)
    return ws_obj(tonumber(type(sel) == "table" and sel.id or sel))
  end
  hl.get_workspaces = function()
    local list = {}
    for id in pairs(W.ws) do list[#list + 1] = ws_obj(id) end
    table.sort(list, function(a, b) return a.id < b.id end)
    return list
  end
  return hl
end

---------------------------------------------------------------- Omarchy

local function make_o(state_dir)
  local o = { bind_expressions = {} }
  function o.shell_quote(s) return "'" .. tostring(s):gsub("'", "'\\''") .. "'" end
  function o.bind(keys, desc, fn) hl.bind(keys, fn, { description = desc }) end
  function o.rebind(keys, desc, fn) hl.unbind(keys); o.bind(keys, desc, fn) end
  package.loaded["default.hypr.paths"] = { state_home = state_dir }
  return o
end

local STATE = os.tmpname()
os.remove(STATE)
os.execute("mkdir -p '" .. STATE .. "/omarchy'")

local function write_state(lines)
  local f = assert(io.open(STATE .. "/omarchy/monitor-workspaces", "w"))
  f:write(lines)
  f:close()
  os.remove(STATE .. "/omarchy/monitor-workspaces-merged")
end

---------------------------------------------------------------- a scenario

local LAYOUTS = {
  -- the laptop on its own, on the first block
  { blocks = "0\teDP-1\tPanel A\n", monitors = { { name = "eDP-1", description = "Panel A" } } },
  -- the laptop on the second block (as after a headless start), workspaces 11-20
  { blocks = "0\t\tUnknown-1\n1\teDP-1\tPanel A\n", monitors = { { name = "eDP-1", description = "Panel A" } } },
  -- two monitors, 1-10 and 11-20
  { blocks = "0\teDP-1\tPanel A\n1\tHDMI-A-1\tScreen B\n",
    monitors = { { name = "eDP-1", description = "Panel A" }, { name = "HDMI-A-1", description = "Screen B" } } },
}

local function setup(layout, opts)
  W = { now = 0, seq = 0, queue = {}, monitors = {}, ws = {}, socket = {}, engine = {}, binds = {}, curves = {},
        visits = {}, travel_events = {}, handlers = {}, timers_made = 0, jitter = opts.jitter or 0,
        engine_delay = opts.engine_delay or 0, rand = opts.rand or math.random }
  W.show = W_show
  W.engine_read = engine_read
  write_state(layout.blocks)
  for i, spec in ipairs(layout.monitors) do
    W.monitors[i] = { name = spec.name, description = spec.description, id = i - 1, focused = i == 1 }
    W.engine[spec.name] = { ws_id = 0 }
  end
  hl = make_hl()
  o = make_o(STATE)
  local chunk = assert(loadfile(OMARCHY_WS))
  o.workspaces = chunk()
  -- each monitor starts on a workspace of its own block, some with windows
  for _, m in ipairs(W.monitors) do
    local base = o.workspaces.workspace_id(mon_obj(m), 1) - 1
    for slot = 1, 10 do
      if (opts.windows or {})[slot] then W.ws[base + slot] = { windows = 1, monitor = m.name } end
    end
    local start = base + (opts.start or 1)
    W.ws[start] = W.ws[start] or { windows = 0, monitor = m.name }
    m.active = start
    W.engine[m.name].ws_id = start
  end
  for _, g in ipairs(opts.guests or {}) do -- a workspace of one monitor's block living on another
    W.ws[g.id] = { windows = 1, monitor = g.on }
  end
  -- Omarchy's own binds (bindings/tiling.lua), then Kinetic's file over them
  for slot = 1, 10 do
    o.bind("SUPER + code:" .. tostring(slot + 9), "Switch to workspace " .. slot, o.workspaces.focus(slot))
  end
  local f = assert(loadfile(LUA_FILE))
  f()
  W.normal = W.anim
  W.visits = {}
  assert(W.binds["SUPER + code:10"], "no Super + 1 bind")
end

-- Runs the actions, then lets every timer and the engine finish.
local function run(actions)
  local expect, focus_on, last_press = {}, nil, nil
  for _, a in ipairs(actions) do
    schedule(a.t, function()
      local m = focused_mon()
      if a.press then
        local id = o.workspaces.workspace_id(mon_obj(m), a.press)
        expect[m.name] = id
        focus_on = m.name
        last_press = { slot = a.press, monitor = m.name, at = W.now }
        W.binds["SUPER + code:" .. tostring(a.press + 9)]()
      elseif a.mouse then -- the pointer crosses to another monitor (follow_mouse)
        local to = mon(a.mouse)
        focus_monitor(to)
        focus_on = to.name
      else
        -- Super + Tab (Hyprland's own next workspace on the monitor), or the bar
        -- (Omarchy's focus, not the key). One that changes nothing (the workspace
        -- a trip is passing just then, or the only one) leaves no trace in
        -- Hyprland, so the trip goes on and its choice stands.
        local before = m.active
        if a.tab then
          dispatch(hl.dsp.focus({ workspace = a.tab > 0 and "m+1" or "m-1" }))
        else
          o.workspaces.focus(a.click)()
        end
        local fm = focused_mon()
        if fm ~= m or fm.active ~= before then
          expect[fm.name] = fm.active
          focus_on = fm.name
        end
      end
    end, "action")
    W.now = W.now -- (actions are queued at absolute times from 0)
  end
  local guard = 0
  while true do
    local ev = next_event()
    if not ev then break end
    guard = guard + 1
    assert(guard < 100000, "runaway timers")
    if ev.kind ~= "timer" then
      W.now = math.max(W.now, ev.due)
      engine_tick()
      if ev.on then ev.fn() end
    else
      -- Hyprland's event loop, woken late under load, runs every timer that has
      -- passed by then, in the order they were made (EventLoopManager::onTimerFire)
      W.now = math.max(W.now, ev.due + W.rand() * W.jitter)
      local batch = { ev }
      for i = #W.queue, 1, -1 do
        if W.queue[i].kind == "timer" and W.queue[i].due <= W.now then
          batch[#batch + 1] = table.remove(W.queue, i)
        end
      end
      table.sort(batch, function(x, y) return x.seq < y.seq end)
      engine_tick()
      for _, t in ipairs(batch) do
        if t.on then t.fn() end
      end
    end
  end
  W.now = W.now + 30000 -- the engine's trips arrive
  engine_tick()
  if W.engine_wake or #W.socket > 0 then engine_read() end
  return expect, focus_on, last_press
end

local function describe(ctx)
  local parts = {}
  for k, v in pairs(ctx) do
    if type(v) ~= "table" then parts[#parts + 1] = k .. "=" .. tostring(v) end
  end
  table.sort(parts)
  local acts = {}
  for _, a in ipairs(ctx.actions or {}) do
    local what = a.press and ("press " .. a.press) or a.mouse and ("mouse->" .. a.mouse) or a.tab and "super+tab"
      or a.click and ("bar " .. a.click) or "?"
    acts[#acts + 1] = string.format("%dms %s", math.floor(a.t), what)
  end
  return table.concat(parts, " ") .. " | " .. table.concat(acts, ", ")
end

local failures, checked = {}, 0

local function check(ctx)
  local layout = LAYOUTS[ctx.layout]
  setup(layout, ctx)
  W.trace = TRACE and TRACE == ctx.name
  local expect, focus_on = run(ctx.actions)
  local problems = {}
  for name, id in pairs(expect) do
    local m = mon(name)
    if m.active ~= id then
      problems[#problems + 1] = string.format("%s ended on %d, chosen %d", name, m.active, id)
    end
    local e = W.engine[name]
    if e.arrived and e.arrived ~= m.active then
      problems[#problems + 1] = string.format("engine's trip on %s ended at %d, Hyprland at %d", name, e.arrived, m.active)
    end
    if e.ws_id ~= m.active then
      problems[#problems + 1] = string.format("engine on %s thinks %d, Hyprland %d", name, e.ws_id, m.active)
    end
  end
  if focus_on and not mon(focus_on).focused then
    problems[#problems + 1] = "focus ended on " .. focused_mon().name .. ", not " .. focus_on
  end
  if W.anim ~= W.normal and not (W.anim.bezier == W.normal.bezier and W.anim.speed == W.normal.speed
    and W.anim.spring == W.normal.spring) then
    problems[#problems + 1] = "the trip animation was left on (" .. tostring(W.anim.bezier) .. ")"
  end
  if ctx.path then -- an undisturbed trip passes every workspace between, in order
    local v = table.concat(W.visits[ctx.path.monitor] or {}, ",")
    if v ~= ctx.path.want then problems[#problems + 1] = "passed " .. v .. ", expected " .. ctx.path.want end
  end
  checked = checked + 1
  if #problems > 0 then
    failures[#failures + 1] = table.concat(problems, "; ") .. "\n    " .. describe(ctx)
  end
end

---------------------------------------------------------------- the scenarios

-- 1. Every trip on its own, from every workspace to every other, on each layout,
--    exact timers and late ones: it lands, and passes every workspace between.
for layout = 1, 3 do
  for from = 1, 10 do
    for to = 1, 10 do
      for _, jitter in ipairs({ 0, 25 }) do
        local base = layout == 2 and 10 or 0
        local want = {}
        if from ~= to then
          local d = to > from and 1 or -1
          for s = from + d, to, d do want[#want + 1] = base + s end
        end
        check({ layout = layout, start = from, jitter = jitter, engine_delay = 5, actions = { { t = 10, press = to } },
                path = { monitor = "eDP-1", want = table.concat(want, ",") }, name = "single" })
      end
    end
  end
end

-- 2. Deliberate cases: a second press at every millisecond of a long trip (so
--    one lands in each step, and during the last), same way, reversing, the
--    same key again, and back to where it started.
for _, second in ipairs({ 10, 5, 1, 3, 9, 2 }) do
  for t2 = 11, 420, 3 * STRIDE do
    for _, layout in ipairs({ 1, 2 }) do
      check({ layout = layout, start = 1, engine_delay = 12, actions = { { t = 10, press = 10 }, { t = t2, press = second } },
              name = "second press" })
    end
  end
end

-- 3. Another monitor: the pointer crossing over mid-trip, a press over there
--    mid-trip, and a workspace on the way that lives on the other monitor.
for t2 = 11, 400, 7 * STRIDE do
  check({ layout = 3, start = 1, actions = { { t = 10, press = 9 }, { t = t2, mouse = "HDMI-A-1" } }, name = "mouse away" })
  check({ layout = 3, start = 1, actions = { { t = 10, press = 9 }, { t = t2, mouse = "HDMI-A-1" }, { t = t2 + 5, press = 6 } },
          name = "press over there" })
  check({ layout = 3, start = 2, actions = { { t = 10, press = 10 }, { t = t2, mouse = "HDMI-A-1" }, { t = t2 + 40, mouse = "eDP-1" },
          { t = t2 + 60, press = 1 } }, name = "there and back" })
end
check({ layout = 3, start = 1, guests = { { id = 5, on = "HDMI-A-1" } }, actions = { { t = 10, press = 9 } }, name = "guest on the way" })
check({ layout = 3, start = 9, guests = { { id = 5, on = "HDMI-A-1" } }, actions = { { t = 10, press = 2 } }, name = "guest on the way back" })

-- 4. Other workspace keys and the bar, mid-trip: the last choice wins.
for t2 = 11, 400, 9 * STRIDE do
  check({ layout = 1, start = 1, windows = { [3] = true, [7] = true }, actions = { { t = 10, press = 10 }, { t = t2, tab = 1 } },
          name = "super+tab mid-trip" })
  check({ layout = 2, start = 9, actions = { { t = 10, press = 1 }, { t = t2, click = 4 } }, name = "bar mid-trip" })
end

-- 5. Random: bursts of presses (often far apart, often close), mouse and other
--    keys now and then, timers late by up to 40 ms, the engine slow to read.
local seed = tonumber(os.getenv("PASS_THROUGH_SEED") or "") or 20261119
math.randomseed(seed)
for i = 1, SCENARIOS do
  local layout = math.random(3)
  local actions, t = {}, 10
  for _ = 1, math.random(1, 7) do
    local r = math.random()
    t = t + (r < 0.35 and math.random(0, 25) or r < 0.8 and math.random(20, 260) or math.random(250, 700))
    local kind = math.random()
    if layout == 3 and kind < 0.12 then
      actions[#actions + 1] = { t = t, mouse = math.random(2) == 1 and "eDP-1" or "HDMI-A-1" }
    elseif kind < 0.17 then
      actions[#actions + 1] = { t = t, tab = math.random(2) == 1 and 1 or -1 }
    elseif kind < 0.2 then
      actions[#actions + 1] = { t = t, click = math.random(10) }
    else
      actions[#actions + 1] = { t = t, press = math.random(10) }
    end
  end
  local windows = {}
  for s = 1, 10 do windows[s] = math.random() < 0.3 end
  check({ layout = layout, start = math.random(10), windows = windows, jitter = ({ 0, 0, 8, 40 })[math.random(4)],
          engine_delay = ({ 0, 4, 16, 30 })[math.random(4)], actions = actions, name = "random " .. i })
end

os.execute("rm -rf '" .. STATE .. "'")
if #failures > 0 then
  for i = 1, math.min(#failures, tonumber(os.getenv("PASS_THROUGH_SHOW") or "") or 12) do print("  " .. failures[i]) end
  print(string.format("pass-through: %d of %d scenarios did not land where chosen", #failures, checked))
  os.exit(1)
end
print(string.format("pass-through trips land where chosen: %d scenarios (seed %d)", checked, seed))
