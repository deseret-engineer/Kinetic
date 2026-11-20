# Kinetic wallpapers

The `wall` module's reference: engine options, the bundled wallpapers, how they
follow Omarchy, and how to write your own. Everything here is also reachable
as `kinetic wall <command>` or the `kinetic-wall` binary.

## Configuration

```bash
kinetic wall config                      # every global option, grouped, with ranges
kinetic wall config dim 0.3              # set one live
kinetic wall config dim default          # back to wall.toml / built-in default
kinetic wall params                      # the running wallpaper's parameters and presets
kinetic wall param birth 72              # set one (remembered per wallpaper)
kinetic wall param @fps 15               # per-wallpaper frame rate / @scale resolution
kinetic wall preset "Day & Night"
kinetic wall param-reset
kinetic wall restart                     # start the wallpaper over (replants a garden)
```

| Group | Options |
|-------|---------|
| Performance | `fps`, `fps_occupied`, `render_scale`, `adaptive`, `fps_monitor`, `gpu_budget_ms` |
| Appearance | `time_scale`, `dim`, `transition_ms`, `theme_fade_ms` (how far wallpapers move with workspaces is `kinetic motion set parallax`) |
| Sound | `sound_react`, `sound_strength`, `audio_gain` |
| Behavior | `pause_fullscreen`, `pause_occupied`, `pointer`, `suspend_shell`, `hot_reload`, `layer` |
| Battery | `battery.mode`, `battery.fps`, `battery.scale`, `battery.pause_below` |
| Rotation | `shuffle_minutes`, `shuffle_order` |

### Reacting to sound

Turn on **React to sound** (`kinetic wall config sound_react true`, or the Sound group in
the panel's Engine tab) and wallpapers respond to whatever the system plays: music,
video, notification sounds. Every wallpaper's clock surges with loudness and kicks on
each onset; many also react in their own way: Pond drops a stone, Fireworks fires a
shell, Smoke puffs, Swarm sends a shock wave, Reaction-Diffusion and Petri seed new
growth, Life sparks, Particle Life scatters, Galaxy flares. Each wallpaper has a
**React to sound** setting (`global` follows the switch, or force `off` / `on`):

```bash
kinetic wall param lw_sound on       # just this wallpaper, whatever the global switch says
```

The capture reads PipeWire's monitor of your output, which carries the signal before
volume and mute, so it reacts even when your speakers are muted. It only runs while a
visible wallpaper is listening.

### When the screen fills

Simulations that can fill the screen (Game of Life, Reaction-Diffusion, Petri, Sandbox) get a
**When the screen fills** setting: `continue`, `restart` (crossfade into a fresh random
start), or `reverse` (the default: run it back until most of it is gone, then let it grow
again from what is left; Sandbox opens its floor and drains like an hourglass). "Full" means past a coverage threshold, or no longer spreading
for a while, since a finished coral pattern only covers a fifth of the pixels. Life also
restarts if it dies out. `kinetic wall stats` shows the current fill level.

### Wallpapers that keep growing

Garden and Sandbox save their world to `~/.local/state/kinetic/wall/<id>/<monitor>/` every
five minutes and whenever you switch away or log out, and carry on from there next time:
the garden you leave on Friday is still growing on Monday. `kinetic wall restart` (or a
fresh start from the fill monitor) wipes the saved world and starts over.

### Measuring speed

```bash
kinetic wall fps                 # toggle the on-screen FPS monitor (or: kinetic wall fps top-right / off)
kinetic wall stats               # one line per monitor: fps, avg/worst frame time, GPU ms, CPU %, size
kinetic wall stats -w            # the same, refreshed every second
kinetic wall bench               # offscreen GPU cost of every wallpaper on this machine
```

The monitor (`fps_monitor` in the Engine tab) shows measured frame rate against the
cap, average and worst frame interval, the GPU time of the last frames (timer query),
the daemon's CPU use, and the render size. It sits behind your windows like the rest
of the wallpaper, so look at an empty workspace.

Plus `[themes]` (theme → wallpaper), `[monitors]` (pin per output) and
`[params.<id>]` defaults in `~/.config/kinetic/wall.toml`; see
`integration/wall.toml.example`.

## Wallpapers

| | | |
|---|---|---|
| **Game of Life** — editable birth/survival rules, 8 rule presets, draw with the mouse | **Fluid** — a real incompressible solver on an endless wrapping sheet: jets of ink fold and mix, glossy shading; stir and splash with the pointer | **Pond** — rain ripples refracting your background |
| **Flow Field** — comets along a curl-noise field | **Lava Lamp** — merging metaballs | **Plexus** — linked drifting points |
| **Topography** — a randomly generated shaded relief map: rolling, ridged, billowy, mesas, islands, craters, dunes, canyons or glacial terrain; a new map every start, or pin a seed | **Skyline** — city whose windows follow the clock | **Truchet** — light flowing through tiles |
| **Snowfall** — layered snow with wind | **Wormhole** — audio-reactive tunnel | **Aurora**, **Mesh**, **Starfield**, **Orbit** |
| **Tide** — sea level is your battery | **Spectrum**, **Synthwave** — audio | **Glyphs**, **Petri**, **Daylight** |
| **Rain on Glass**, **Living Still** — animate your own Omarchy background | **Smoke** — a real incompressible fluid solver: buoyant smoke, pressure projection, vorticity; push it with the pointer | **Swarm** — up to 262k GPU particles riding a curl-noise current |
| **Particle Life** — species with secret attraction rules self-organize into cells and hunters (O(n²) on the GPU) | **Galaxy Collision** — two spiral galaxies, 131k stars, tidal tails then a merger; a new encounter every few minutes | **Reaction-Diffusion** — Gray-Scott chemistry: coral, mitosis, worms, mazes |
| **Fireworks** — closed-form sparks over water; a volley on every workspace switch, a burst where you click | **Low Poly** — a real 3D triangle landscape with a depth buffer, sun from your clock, wireframe mode | **Garden** — a persistent garden: wildflowers, grasses, shrubs and trees grow, bloom, seed their neighbors and wilt, each generation inheriting its parents' genes; day and night follow your clock; click to plant |
| **Sandbox** — a persistent falling-sand world: spouts pour colored strata, rain pools into lakes, seeds sprout on wet sand, lightning starts fires; click to pour | **Ant Colony** — up to 16k ants of rival colonies forage by scent and dead reckoning, and roads emerge; click to drop food | |

## Omarchy integration

| What | How |
|------|-----|
| Theme colors | `colors.toml` of the active theme becomes `uBackground`, `uForeground`, `uAccent`, `uPalette[16]`. Theme switches are picked up by inotify and cross-fade over 0.8 s |
| Theme → wallpaper | `[themes]` in the config maps a theme to a live wallpaper |
| Your stills | `"@background"` hands a shader the current Omarchy background (`glass`, `living`) |
| Shell background | The shell's own background layer is suspended while a live wallpaper shows and restored on `kinetic wall off` or exit |
| Workspaces | Hyprland's event socket feeds `uWorkspace` (switch reactions), `uParallax` (how far wallpapers move; see `kinetic motion`) and the window count |
| Fullscreen | Rendering stops while a fullscreen window covers the output |
| Battery | Level and charging state (`uBattery`, see `tide`); on battery the engine throttles or pauses |
| Audio | PipeWire capture of what is playing, as `uAudio` and a spectrum texture; opened only while an audio wallpaper is visible |
| Pointer | `uPointer` / `iMouse` for wallpapers that ask for it |
| Time | `uDayTime`, local hours (`daylight`) |
| Menu, picker, service | Menu entries, `kinetic-wall-pick`, and a systemd user unit |

Keybindings are not installed. To add some, in `~/.config/hypr/bindings.lua`:

```lua
o.bind("SUPER + CTRL + W", "Live wallpaper picker", hl.dsp.exec_cmd("kinetic-wall-pick"))
o.bind("SUPER + CTRL + ALT + W", "Next live wallpaper", hl.dsp.exec_cmd("kinetic-wall next"))
```

Omarchy's `owe` daemon (video backgrounds) is left alone. Kinetic draws over it,
so pick a still background in Omarchy while the live wallpaper is on.

## Why it is cheap

- Event driven end to end: no polling timers. With a static wallpaper the process sleeps in `epoll`.
- Frames follow the compositor's frame callbacks, so nothing is drawn for a screen that is off, locked, or under a fullscreen window.
- Each wallpaper declares the frame rate and resolution it needs (`mesh` runs at quarter resolution, `daylight` draws one frame every two seconds). Lower resolutions go straight to the swapchain and the compositor scales them with `wp_viewporter`; there is no blit pass.
- A GPU timer query drives adaptive resolution: a frame over `gpu_budget_ms` shrinks the render size until it fits.
- The frame cap drops while windows cover the desktop and on battery.
- Value noise, the hot spot of most shaders, is one `textureGather` from a random texture on GLES 3.1+ instead of four ALU hashes per octave (about 2x faster for noise-heavy wallpapers), with full-precision interpolation so it never bands.
- Simulation buffers choose their format (`rg32f` carries two exact floats at half the bandwidth of `rgba32f`), can update every Nth frame (`every`), and solvers iterate inside one frame (`iterations`) without leaving the GPU.
- Particles are GPU points drawn straight from a state texture: no CPU work and no readback, whatever the count. Trails come from a blend that fades the target, not an extra pass.
- Targets that are about to be fully overwritten are invalidated first, so tiled GPUs (Adreno, Mali) skip loading them into tile memory; depth buffers never leave it.
- Theme and parameter uniforms are uploaded only when they change.
- Run `kinetic wall bench` for the per-frame GPU cost of every wallpaper on your machine.

## Writing a wallpaper

```bash
kinetic wall new mine       # scaffolds ~/.config/kinetic/wallpapers/mine
kinetic wall set mine       # saving main.frag now hot-reloads it
kinetic wall validate mine
```

`wallpaper.toml`:

```toml
name = "Mine"
fps = 30            # 0 = redraw only when an input changes
scale = 0.5         # fraction of native resolution
pointer = false
adaptive = true

[params]            # short form: each becomes `uniform float <name>`
speed = [1.0, 0.0, 4.0]     # default, min, max

[param.rule]        # long form, for richer controls in the panel
type = "bits"       # float | int | bool | choice | bits | color
bits = 9            # bits: a mask, drawn as one chip per bit
label = "Born with N neighbors"
description = "Shown under the control"
default = 8

[param.palette]
type = "choice"     # uniform float holding the option's index
options = ["warm", "cool", "custom"]
default = "warm"

[param.tint]
type = "color"      # uniform vec3
default = "#7aa2f7"

[preset.Busy]       # one click in the panel sets several parameters
speed = 3.0
palette = "cool"

[[buffer]]          # optional passes, run in order every frame (up to 8)
name = "A"
shader = "a.frag"
scale = 0.25        # fraction of the output size...
# size = [512, 512] # ...or a fixed size, e.g. one texel per particle
format = "rgba16f"  # rgba8 rg8 r8 | rgba16f rg16f r16f | rgba32f rg32f r32f (32-bit: nearest only)
wrap = "repeat"     # or clamp
filter = "linear"   # or nearest
mipmap = false      # true: textureLod() gives cheap blur and bloom
iterations = 1      # run N times per frame (a solver); a number or a parameter's name
every = 1           # update every Nth frame only (slow fields)
persist = false     # true: saved to disk and restored on the next load (worlds that keep growing)
channels = ["A"]    # a buffer may read itself: that is the previous step

[image]
shader = "main.frag"
channels = ["A", "noise.png", "@background", "@audio"]
```

Top-level keys also include `parallax = 1` (opt in to workspace parallax:
with Kinetic motion on, the engine pans the image pass by `uParallax` screens.
Only for planes that are endless in every direction. A centered scene would
slide away, and buffers have edges. The pan reaches 4.5 screens (9 workspaces
at parallax 0.5), so anything tied to the screen, like a vignette, uses
`fragCoord - lw_Shift` instead), `common = "common.glsl"` (prepended to every shader
stage) and `preview_warmup` (seconds simulated before a thumbnail is taken).
A restored `persist` buffer resumes at its saved frame, so `iFrame < 2` seeding is
skipped; buffers that are not persistent start from zero alongside it.
A parameter with `restart = true` restarts the simulation when it changes (seeds).

A simulation that can fill the screen declares how to measure it, and gets the
**When the screen fills** setting for free:

```toml
[fill]
buffer = "rd"        # the buffer to watch (sampled on the GPU, read back without stalling)
channel = 1          # which channel holds the "stuff"
above = 0.2          # a texel counts as filled above this
valid = -1           # optional: only texels with this channel non-zero count
full = 0.6           # full above this coverage...
settle = 15          # ...or when it has not spread for this many seconds...
settle_min = 0.08    # ...while covering at least this much
empty = 0.001        # optional: restart when it dies out
grace = 5            # seconds after a (re)start before anything happens
reverse = true       # the shaders handle uReverse, so "reverse" is offered
ramp = 8             # seconds for uReverse to build from 0 to 1
low = 0.4            # run back to this fraction of the coverage it reversed at
default = "reverse"
```

When reversing, `uReverse` rises from 0 to 1 and the shader runs the simulation back
(Life stops births and erodes, Gray-Scott raises its kill rate). A restart reseeds every
buffer on frame 0 and increments `uEpoch`, which seeds should mix in so each start differs.
The daemon also starts `uEpoch` at a random value on every load (reloading the same
wallpaper keeps it), so a wallpaper that seeds from it looks different each time.

### Geometry passes: particles, lines, meshes

A buffer can draw geometry instead of a fullscreen pass:

```toml
[[buffer]]
name = "dots"
draw = "points"     # points | lines | line_strip | triangles | triangle_strip
source = "state"    # one vertex per texel of this buffer (particle state)...
count = "particles" # ...capped by this (a number or a parameter), or alone for procedural geometry
vertex = "dots.vert"
shader = "dots.frag"
blend = "add"       # add | alpha (premultiplied) | multiply | max | min | none
fade = 0.9          # keep the image and fade it each frame (trails); 0 clears
depth = false       # a depth buffer, for 3D meshes
```

The vertex file defines
`void mainVertex(int id, vec4 state, inout vec4 pos, inout float size, inout vec4 color)`:
`state` is the vertex's texel from `source`, `pos.xy` is in uv space (0..1) with depth
in `z` and a homogeneous `w` (`lw_perspective(p, focal, aspect)` builds one from a
camera-space point), `size` is the point size in pixels (0 hides the vertex), and
anything written to the global `lw_data` reaches the fragment stage. The fragment
shader is an ordinary `mainImage` that can read `lw_Color`, `lw_State`, `lw_Data` and
`gl_PointCoord`. Such a pass draws onto its own persistent target, so other passes
read it but it cannot read itself. `swarm`, `galaxy`, `fireworks` (no state at all)
and `terrain` (triangles with depth) are worked examples.

Shaders use the Shadertoy entry point, `void mainImage(out vec4 fragColor, in vec2 fragCoord)`,
with `iResolution`, `iTime`, `iTimeDelta` (time since this pass last ran), `iFrame`,
`iMouse`, `iDate`, `iChannel0..7` and `iChannelResolution`, so most Shadertoy shaders
paste in unchanged. `iPass` is the iteration index and `iCount` the vertex count. On top of that:

| Uniform | Meaning |
|---------|---------|
| `vec3 uBackground, uForeground, uAccent` | theme colors |
| `vec3 uPalette[16]` | ANSI order: 0 darkest background, 1 red, 2 green, 3 yellow, 4 blue, 5 magenta, 6 cyan, 7 foreground, 8 muted, 9–14 bright, 15 bright foreground |
| `float uDark` | 1 on dark themes, 0 on light |
| `vec4 uWorkspace` | smoothed index, seconds since switch, index, window count. Indexes count 1-10 on every monitor (Omarchy's blocks), and the smoothed one eases on Hyprland's curve. For reactions to a switch (`z - x` is the distance still to go), not for moving the picture: that is `uParallax` |
| `float uFocus` | seconds since the active window changed |
| `vec4 uBattery` | level 0..1, charging, on battery, battery present |
| `vec4 uAudio` | bass, mid, treble, overall level |
| `float uDayTime` | local time in hours |
| `vec4 uPointer` | smoothed x, y (0..1), presence, seconds since click |
| `vec4 uSound` | loudness, seconds since the last onset, its strength, onset count (0 / 1e4 when sound reactions are off) |
| `float uReverse` | 0..1 while the fill monitor asks the simulation to run back |
| `float uEpoch` | random on each load, +1 on every restart: mix it into seeds |
| `vec2 uParallax` | how far the view has panned with workspace switches, in screens (eased on Hyprland's curve, through pass-through trips; x for sideways slides, y for vertical, 0 with motion off or parallax 0). Wallpapers with `parallax = 1` are already shifted by it. The one source of workspace motion: a wallpaper never moves by `uWorkspace.x` itself (`tests/run.sh` checks) |

Layers: `lw_pan(depth)` is the pan for a layer at `depth` (0 still, 1 moving with the
view), in screen heights, to add to a position like `vec2(uv.x * aspect, uv.y)`. Far
stars at 0.05, near hills at 0.8: that is the depth. Use `.x` alone for a
landscape, so a vertical slide does not lift the horizon off the screen.

Helpers: `lw_hash`, `lw_hash2`, `lw_rand(id, salt)` (exact integer hash, for particles),
`lw_uhash`, `lw_noise`, `lw_fbm`, `lw_palette(t)` (gradient through theme colors),
`lw_dither`, `lw_luma`, `lw_blend(base, color, amount)` (glow on dark themes, ink on
light), `lw_cover(uv, texSize, res)` and `lw_perspective`.

