# Kinetic

A look-and-feel suite for [Omarchy](https://omarchy.org). Kinetic puts live
wallpapers, a floating glass bar, liquid glass behind windows and window
animations under one command and one app. Because the parts know about each
other, the glass bends the real wallpaper behind it, and on a workspace switch
the wallpaper slides in step with the windows at its own depth (parallax).

```bash
./install.sh                    # build and install for your user
kinetic app                     # the app: Wallpaper (Library, Engine) · Bar · Glass · Light · Motion · Looks
kinetic panel                   # the bar widget: the wallpaper and its library
kinetic look apply contours     # a wallpaper and a bar together
kinetic bar preset lens         # a strongly bending glass bar
kinetic motion preset parallax  # workspaces slide; the wallpaper follows at a quarter of the distance
kinetic wall set fluid          # just the wallpaper
```

The app's sidebar lists the modules that are on, each with its pages: a module
with several pages is a group (Library and Engine sit under Wallpaper), which the
arrow beside it folds. Ctrl + 1–9 and Ctrl + Tab go through the rows showing.

The app is also in the launcher (Kinetic) and under **Omarchy menu → Appearance →
Kinetic**, which opens each page of the app and has a **Wallpaper** submenu
(choose, next, random, pause, live wallpaper on or off, and its pages). The 󰸉
widget on the bar is for the wallpaper alone: what shows, its presets and
parameters, the library. Its gear opens the app (so does `o`).

## Modules

Every module is its own folder: a `module.sh` (its commands) and a
`module.json` (what it is, what it needs, its pages in the app). Kinetic's own
are in `modules/`; you can add more:

```bash
kinetic module install https://github.com/someone/kinetic-thing   # or a folder, or an archive
kinetic modules                                                    # every module, on or off, its version
```

Kinetic fetches the module and whatever it needs, shows you all of it, and
installs nothing until you say yes (in the app: the Modules page, or drop a
folder, archive or link on its window). Switching a module on switches on what
it needs first; switching one off asks before stopping what needs it.
[MODULES.md](MODULES.md) is the guide to writing one.

| Module | What | Commands |
|---|---|---|
| `wall` | Living wallpapers: GLSL scenes on a native C engine (EGL/GLES 3, Wayland layer shell). 33 bundled | `kinetic wall help` (also `kinetic-wall`) |
| `bar` | The pill: Omarchy's bar floating as a capsule, or as islands, in liquid, glass, frosted, solid or outline | `kinetic bar help` |
| `glass` | Liquid glass: the wallpaper bent behind the bar and windows (one look for both) | `kinetic glass help` |
| `light` | Selection light: a soft glow around windows, drawn under the glass (by Kinetic's Hyprland plugin) | `kinetic light help` |
| `motion` | Hyprland's window, workspace and menu animations, and wallpaper parallax | `kinetic motion help` |
| `look` | Looks: a wallpaper (with preset and parameters), a bar and motion, saved and applied together | `kinetic look help` |

`kinetic status` prints all of it as one JSON object. `kinetic modules` lists the
modules and whether each is on; `kinetic <module> enable|disable` switches one
(the app's Modules page does the same). A module switched off stops and leaves
the app's list: the wallpaper engine's service, Kinetic's bar (back to
Omarchy's), the glass, the light, the animations. Looks run nothing, so theirs
only hides them.

## Liquid glass

The wallpaper engine has the real pixels behind the bar and the windows, so it
can bend them. After each frame it copies what lies behind each glass shape and
draws it back through a rounded-rectangle lens: bent most at the rim on an edge
profile you choose, frosted (mip blur), split into color fringes, with
saturation, brightness, a tint and a rim light. It costs one small copy and one
draw per frame the wallpaper draws anyway, and adds no frames.

**Under the bar.** The bar tells the engine where its capsules are; the bar
draws its own tint and highlight on top, crisp. Options (`kinetic bar set`, or
the app's Glass page):

| Option | Meaning |
|---|---|
| `glass` | `auto` (the wallpaper when the engine shows one, else Hyprland's blur), `wallpaper`, `blur`, `none` |
| `refraction` | How far the rim bends the wallpaper, px |
| `frost` | Blur of the bent wallpaper, 0 = clear |
| `dispersion` | Color fringes where it bends most |
| `bevel` | How far in from the rim the glass curves, px (0 follows the capsule) |
| `curvature` | Edge profile: under 1 a soft slope, 1 rounded, over 1 a sharp rim and flat middle |
| `saturation`, `brightness` | What shows through |
| `specular`, `lightAngle` | Light catching the rim, and where it comes from |

**Behind windows.** `kinetic glass windows see-through` (what `kinetic glass
enable` and the app's switch choose; `floating` and `all` are there too) puts
the same glass behind the windows you can see through, and nowhere else: an
opaque window would hide it, and drawing it there would only cost GPU time. A
window counts as see-through when

- Hyprland draws it at 95% opacity or less right now (its opacity rules times
  `decoration:active_opacity`/`inactive_opacity`, as focus has it). Omarchy's
  glassy windows make Nautilus 75%; Omarchy's usual 98.5%/96% does not count; or
- it is on `window_glass_apps`: apps whose own config makes them see-through,
  which Hyprland cannot see. Words separated by spaces: a class, a class prefix
  ending in `*`, or a Hyprland tag. The default, `terminal`, is the tag Omarchy
  gives every terminal (Alacritty, Ghostty, Kitty, foot, its TUIs), which
  Omarchy's glassy windows (`omarchy-theme-set-glass`) give a see-through
  background. `kinetic glass apps terminal org.gnome.Nautilus`, or `none`. If
  your terminals are opaque (glassy windows off), `none` saves the GPU.

The glass is drawn inside Hyprland, by Kinetic's Hyprland plugin
(`modules/glass/plugin`), in the same frame as the window and from where
Hyprland draws it on that frame: it opens, closes, moves, re-tiles, drags and
switches workspaces with the window, at the display's rate, with nothing to
catch up. It sits just under the window, as Hyprland's blur would, and bends
what is behind it on that frame: the wallpaper, the selection light, and any
window below (a see-through floating window over another shows that window
through the glass). Its corners follow Hyprland's own rounding, curve included
(`decoration:rounding_power`), so the glass ends exactly where the window does.
Only the area behind covered windows is drawn, and only when something there
changed; the wallpaper engine does nothing for it.

The engine keeps the options and sends them to the plugin whenever they or the
theme change (`hyprctl kinetic-glass status` says what the plugin is doing).
Its optics are the engine's Glass options: `window_refract`, `window_bevel`,
`window_curve`, `window_frost`, `window_dispersion`, `window_saturation`,
`window_brightness`, `window_tint` (toward the theme's background, for
legible text), `window_specular`, `window_light`. The glass brings its own
frost, so while window glass is on Hyprland's blur is switched off altogether
(`zz-kinetic-windows.lua`, written by `kinetic-window-blur`); the bar has
Kinetic's glass too. Set `window_hypr_blur 1` to keep Hyprland's blur as well.

**The plugin.** `install.sh` builds it against the installed Hyprland's headers
(`gcc` and the `hyprland` package's headers) into `~/.local/lib/kinetic/` and
adds a Hyprland toggle (`kinetic-plugin.lua`) that loads it at every login.
Hyprland refuses a plugin built for another version, so Kinetic's post-update
hook rebuilds it whenever an update brings a new Hyprland; it loads at the next
login. `kinetic glass plugin` says where it stands, `kinetic glass plugin build`
builds and loads it by hand, `kinetic glass plugin remove` takes it out. Without
it, window glass and the selection light are off; the bar's glass is the
engine's and does not need it.

**On Omarchy's own background.** Glass needs pixels to bend. With the live
wallpaper off, `glass_still` (on by default) keeps the engine showing your
Omarchy background itself, exactly as Omarchy would, with the new **Still**
wallpaper: it never animates and draws only when something changes. A video
background goes back to Omarchy's own player. `still` is also in the library.

Try the bar's glass without touching the desktop:

```bash
kinetic-wall thumbnail topo /tmp/t.png 1920x1200 10 --glass 12 0.6 0.4 "8,6,500,36,18;700,6,520,36,18" "bevel=10,spec=0.6"
```

## The bar

`kinetic.bar` is not a copy of Omarchy's bar. `kinetic bar build` takes the bar
that ships with your Omarchy and applies 16 small, named hooks to it. The pill
itself lives in separate files. An Omarchy update hook rebuilds it, so the
bar keeps every upstream change. If Omarchy ever rewrites a part the pill
hooks into, the rebuild stops, notifies you, and keeps the previous build.

Every option, its default (some follow the style), its range, its group and
its label live in one file, `modules/bar/schema.json`. The bar, the CLI and
the panel all read it. `kinetic bar options` lists them all.

```bash
kinetic bar on liquid           # pill background, liquid style
kinetic bar shape islands
kinetic bar set radius full
kinetic bar set tint accent
kinetic bar off glassy          # back to one of Omarchy's backgrounds
```

Options are stored under `bar.pill` in `~/.config/omarchy/shell.json` and apply live.

## Motion

`kinetic motion` sets Hyprland's animations: how workspaces switch (slide,
slide vertically, slide with a fade, fade, or instantly), how windows open,
close and move, and how menus and notifications appear. Each has a duration and
a curve (glide, smooth, snappy, linear, overshoot, spring).

Turning motion on also syncs the wallpaper with workspace switches. Omarchy
switches workspaces instantly by default. With motion on, the wallpaper engine
gets the same duration and curve as Hyprland, so on every switch the
wallpaper's position eases on exactly the windows' curve and lands with them.
**Parallax** pans the wallpaper by a fraction of a screen per workspace while
the windows move a whole screen.

```bash
kinetic motion preset parallax       # slide, 500 ms glide, wallpaper at 0.25 screens per workspace
kinetic motion set parallax 0.4      # deeper
kinetic motion set workspaces slidevert
kinetic motion set workspaceCurve spring
kinetic motion set passThrough true  # Super + 5 from 1 slides past 2, 3 and 4
kinetic motion show                  # the Hyprland config and engine setting it makes
kinetic motion off                   # Omarchy's own animations again
```

Presets: `parallax`, `vertical`, `deep`, `springy`, `snappy`, `calm`, `omarchy`.

**Pass through** (`passThrough`): Hyprland slides straight from workspace 1 to
5. With pass-through on, Super + 1–0 steps through every workspace between
instead, and the whole trip is one run of the switch curve: Glide eases in and
out once over the trip, Overshoot passes the target by as much as one switch
would and comes back. Hyprland plays a workspace at a time, each step on its own
slice of the curve, so the slices join seamlessly. A trip takes the switch
duration plus `passMs` (110 by default) for each workspace beyond the first, or
just the switch duration with `passSameTime`. The wallpaper traces the same
curve. It replaces Omarchy's Super + number binds while it is on; turning it
off, or motion off, gives them back.

A trip always ends on the workspace you chose. Its last step is Omarchy's own
focus of that workspace, on the monitor you pressed it on. Pressing another
number mid-trip carries on from wherever the slide has got to; pressing the same
one again lets it finish. If something else switches workspaces meanwhile
(Super + Tab, the bar, an app asking for focus), that switch wins and the trip
stops. If the pointer moves to another monitor, the trip lands at once and the
focus stays where the pointer went. When a workspace on the way is on another
monitor, the switch goes straight there instead of stepping.

The pan counts from each monitor's first workspace (Omarchy gives every monitor
its own block of ten), so workspace 1 always shows the wallpaper where it
starts. A paused wallpaper still follows a slide, with its scene held still.

Every wallpaper that moves with workspaces follows these settings and nothing
else: the parallax amount, the curve, pass-through and the slide's direction.
- Endless planes (Topography, Truchet, Glyphs) pan as a whole.
- Layered scenes (Aurora, Skyline, Starfield, Snow) move each layer by its own
  share: stars and haze barely move, the near hills and rows the most. A
  vertical slide keeps their horizon in place.
- Plexus, Flow and Mesh move or stir by the same amount.
- Orbit, Living, Smoke, Garden and Fireworks react to a switch itself (a
  flyby, a gust, a volley) on the same eased curve.

Centered scenes (Synthwave's sun, Orbit's planet) stay put. Parallax 0 holds
every wallpaper still. A wallpaper opts in with `parallax = 1` in its
`wallpaper.toml`, or reads `uParallax` / `lw_pan(depth)` for layers of its
own (see WALLPAPERS.md).

The animations go in one Hyprland toggle file, `kinetic-motion.lua`. Kinetic
checks it with Hyprland's own `--verify-config` before installing it, and takes
it out again if Hyprland reports an error. It loads before Omarchy's
`no-animations.lua`, so `omarchy toggle animations` still turns everything off.

![A workspace switch: windows slide a whole screen, the topography a quarter of one](docs/parallax.gif)

Try a switch without touching the desktop:

```bash
kinetic-wall thumbnail topo /tmp/a.png 1920x1200 8 --parallax 0.25,0   # one workspace along
```

## Looks

A look is a small JSON file. Bundled ones live in `modules/look/looks/` and
yours in `~/.config/kinetic/looks/`; yours win when ids clash.

```json
{
  "name": "Ink",
  "description": "Ink folding through water, seen through strongly bending lens islands",
  "wall": { "wallpaper": "fluid", "preset": "Ink", "params": { "speed": 1.2 } },
  "bar": { "background": "pill", "preset": "lens" },
  "motion": { "preset": "parallax" },
  "theme": "tokyo-night"
}
```

Every part is optional, and a look changes only what it names. `"wall":
{"enabled": false}` hands the desktop back to Omarchy's own background.
`kinetic look save <id>` snapshots what is showing now.

## Wallpapers

The wallpaper format, the shader uniforms and helpers, geometry passes, the
fill monitor and persistence are documented in
[modules/wall/WALLPAPERS.md](modules/wall/WALLPAPERS.md). `kinetic wall new
mine` scaffolds one in `~/.config/kinetic/wallpapers/`. Saving it reloads it
live.

## At login

The engine runs as `kinetic-wall.service`, started with the graphical session.
At login it isn't the only thing drawing a background: Omarchy's shell draws
its still (and briefly shows a startup cover), and owe plays the login intro
when your background has one. These come up within a second or two of each
other, in no fixed order. So the engine checks what Hyprland actually has
stacked whenever one of those layers comes or goes, and acts on it. It waits
for the shell's cover to fade and lets the intro play out. Then it suspends
the shell's background and moves its own layer back above owe's if needed.
After each step it looks again, a few times, backing off. If the compositor
or Hyprland isn't reachable yet when the service starts, it waits for them
(about 20 s for Wayland, a minute for Hyprland's socket). If the engine
crashes, systemd restarts it, backing off up to 30 s. To skip the intro,
turn Omarchy's background intros off (`~/.local/state/omarchy/toggles/background-intros-off`).

If the wallpaper didn't come up, the log says why:

```bash
journalctl --user -b -u kinetic-wall      # this login; -b -1 for the one before
```

It logs each output's first frame, everything it found over its layer and what
it did about it, and any retries. `kinetic-wall daemon -v` (or
`KINETIC_DEBUG=1`) adds more detail.

## Files

| Path | What |
|---|---|
| `~/.config/kinetic/wall.toml` | Engine options and per-wallpaper defaults (`kinetic wall reload`) |
| `~/.config/kinetic/wallpapers/` | Your own wallpapers |
| `~/.config/kinetic/looks/` | Your own looks |
| `~/.config/kinetic/motion.json` | Motion: on or off, and the options you set |
| `~/.config/omarchy/shell.json` | `bar.id: "kinetic.bar"`, `bar.background: "pill"`, options under `bar.pill` |
| `~/.local/state/kinetic/` | Live changes, saved wallpaper worlds, the last look, signals between parts (`signals/wall`, `signals/motion`) |
| `~/.local/state/omarchy/toggles/hypr/` | `zz-kinetic-bar.lua` (bar blur), `kinetic-motion.lua` (animations) |
| `~/.local/share/kinetic/` | The installed suite and bundled wallpapers |
| `~/.cache/kinetic/wall/thumbs/` | Wallpaper previews |

## Uninstall

`./uninstall.sh` (add `--purge` to also remove your settings and the modules you added). It disables
`kinetic-wall.service` and puts the bar back to Omarchy's own (a pill
background becomes glassy). It removes the `kinetic` and `kinetic.bar`
plugins, the Kinetic menu rows, the post-update hook
(`~/.config/omarchy/hooks/post-update.d/kinetic-bar`), the Hyprland toggle
`zz-kinetic-bar.lua`, `zz-kinetic-windows.lua`, `kinetic-motion.lua`, `kinetic-app.lua` and
`kinetic-plugin.lua`, the Hyprland plugin (unloaded, and `~/.local/lib/kinetic` removed), the
`kinetic.app` plugin and its launcher entry, and the `kinetic*` commands in `~/.local/bin`.
