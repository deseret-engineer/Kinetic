<div align="center">

# Kinetic

**Live wallpapers, rendered glass and fluid motion for [Omarchy](https://omarchy.org).**

![Version D0.1](https://img.shields.io/badge/version-D0.1-5b8def)
![Status: early development](https://img.shields.io/badge/status-early%20development-e5a50a)
![License MIT](https://img.shields.io/badge/license-MIT-3fb68b)
![Hyprland 0.56](https://img.shields.io/badge/Hyprland-0.56-58e1ff)
![Omarchy](https://img.shields.io/badge/Omarchy-Arch%20Linux-1793d1)

</div>

> [!WARNING]
> **Kinetic is in early development and may be unstable.** Expect bugs, rough
> edges and breaking changes between releases: options, commands and file
> formats can change without notice. Part of it runs inside Hyprland as a
> plugin, so a bug there can crash your session. Try it on a setup you can
> recover, and keep `./uninstall.sh` in mind.

<div align="center">

![Four of Kinetic's live wallpapers: Aurora, Swarm, Wormhole and Reaction](docs/wallpapers.gif)

</div>

Kinetic is a look-and-feel suite for Omarchy (Arch Linux, Hyprland and the
Omarchy shell). It brings live wallpapers, a floating glass bar, glass behind
windows, a selection glow and workspace animations together under one
command and one app. Because the parts know about each other, the glass bends
the real wallpaper behind it, and every workspace switch moves the wallpaper in
step with the windows, at its own depth.

## Contents

- [Features](#features)
- [Requirements](#requirements)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Live wallpapers](#live-wallpapers)
- [Glass](#glass)
- [The pill bar](#the-pill-bar)
- [Motion and parallax](#motion-and-parallax)
- [Looks](#looks)
- [Modules](#modules)
- [Troubleshooting](#troubleshooting)
- [Files](#files)
- [Uninstalling](#uninstalling)
- [Development](#development)
- [License](#license)

## Features

- **33 live wallpapers** drawn by `kinetic-wall`, a native C engine (EGL and
  OpenGL ES 3 on the Wayland layer shell). They follow your Omarchy theme, can
  react to sound, pause when covered, and scale their own resolution to stay
  within a GPU budget.
- **Rendered glass** under the bar and behind see-through windows. It bends,
  frosts and splits the real pixels behind it, with an adjustable edge profile,
  dispersion, tint and rim light.
- **Window glass drawn by Hyprland itself.** A small Hyprland plugin draws the
  glass in the same frame as each window, so it stays locked to the window as
  it opens, closes, moves, tiles or switches workspaces.
- **A floating pill bar** built from Omarchy's own bar: one capsule or separate
  islands, in liquid, glass, frosted, solid or outline styles.
- **A selection glow** around the focused window, with breathe, flow and
  shimmer animations and an attention pulse.
- **Motion with parallax.** Kinetic sets Hyprland's animations and moves the
  wallpaper on exactly the same curve, a fraction of a screen per workspace.
- **Pass-through switching.** Jumping several workspaces slides through each one
  on a single curve, instead of cutting straight to the last.
- **Looks** save a wallpaper, bar and motion setup together and apply it in
  one step.
- **Modules.** Every part is a module you can switch off, and you can install
  more from a link, a folder or an archive.
- **One app** for all of it, in the launcher and the Omarchy menu, with a
  matching CLI for scripts.

## Requirements

- Omarchy with Hyprland 0.56 or newer (Lua configuration).
- Build tools: `meson`, `ninja`, `gcc`, `pkg-config`, `jq` and `python3`.
- Libraries: Wayland, wayland-protocols, EGL and OpenGL ES (libglvnd), libpng,
  libjpeg-turbo, libwebp, and optionally PipeWire (for sound-reactive
  wallpapers).
- Hyprland's headers, which the `hyprland` package ships, to build the window
  glass plugin.

On Omarchy:

```bash
omarchy pkg add meson ninja jq python wayland wayland-protocols libglvnd libpng libjpeg-turbo libwebp pipewire
```

## Installation

```bash
git clone https://github.com/deseret-engineer/Kinetic.git
cd Kinetic
./install.sh
```

The installer runs as your user and needs no root access. It builds the
engine and the Hyprland plugin, installs Kinetic under `~/.local`, builds the
pill bar from your Omarchy's bar, adds Kinetic to the launcher and to
**Omarchy menu → Appearance**, and starts the wallpaper engine. Anything it
replaces is set aside in `~/.local/share/kinetic/replaced/`, never deleted.

If the bar doesn't change right away, restart the shell with
`omarchy restart shell`.

## Quick start

```bash
kinetic app                       # open the app
kinetic wall set fluid            # change the wallpaper
kinetic look apply contours       # a wallpaper and a bar together
kinetic bar preset lens           # a strongly bending glass bar
kinetic glass enable              # glass under the bar and behind see-through windows
kinetic light enable              # a glow around the focused window
kinetic motion preset parallax    # sliding workspaces with wallpaper parallax
kinetic status                    # everything, as one JSON object
```

The app is also in the launcher (**Kinetic**) and under **Omarchy menu →
Appearance → Kinetic**. The 󰸉 widget on the bar covers the wallpaper alone:
what's showing, its presets and parameters, and the library. Its gear (or `o`)
opens the app.

In the app, the sidebar lists the modules that are on, each with its pages.
<kbd>Ctrl</kbd> + <kbd>1</kbd>–<kbd>9</kbd> and <kbd>Ctrl</kbd> + <kbd>Tab</kbd>
move between pages.

## Live wallpapers

![A dozen of Kinetic's wallpapers](docs/wallpapers.png)

Each wallpaper is a small folder of GLSL shaders and a `wallpaper.toml`. They
take their colors from the current Omarchy theme and change with it. Some
simulate a world (particles, ants, fluid, a garden) that's saved and picks up
where it left off; others react to sound, the time of day, the battery or a
workspace switch.

```bash
kinetic wall list                 # every wallpaper
kinetic wall set aurora           # show one
kinetic wall next                 # the next one in the library
kinetic wall pause                # hold the current frame
kinetic wall new mine             # scaffold your own in ~/.config/kinetic/wallpapers/
```

Saving a wallpaper's files reloads it live. The format, uniforms and helpers are
documented in [modules/wall/WALLPAPERS.md](modules/wall/WALLPAPERS.md).

## Glass

![The glass in three looks: clear, frosted and prismatic](docs/glass.png)

Kinetic's glass bends what's actually behind it. Each glass shape is a lens with
a rounded edge: the image bends most at the rim, along an edge profile you
choose. On top of the bend it can frost, split into color fringes (dispersion),
shift saturation and brightness, tint toward the theme for legible text, and
catch a rim light from any angle. One look applies to the bar and the windows:

```bash
kinetic glass set refraction 24
kinetic glass set frost 2
kinetic glass set dispersion 0.3
```

| Option | Effect |
|---|---|
| `refraction` | How far the rim bends the image, in pixels |
| `frost` | Blur of what shows through; 0 is clear glass |
| `dispersion` | Color fringes where the glass bends most |
| `curvature` | Edge profile: below 1 a soft slope, 1 rounded, above 1 a sharp rim with a flat middle |
| `saturation`, `brightness` | The color of what shows through |
| `specular`, `lightAngle` | Light catching the rim, and where it comes from |

**Under the bar.** The bar tells the wallpaper engine where its capsules are,
and the engine bends the wallpaper behind them. With the live wallpaper off,
the engine keeps showing your Omarchy background as a still image, so the
glass still has something to bend.

**Behind windows.** Glass goes only behind windows you can see through, since an
opaque window would hide it:

- windows Hyprland draws at 95% opacity or less, and
- apps that are see-through by their own settings, which Hyprland can't detect.
  By default this is every terminal (Omarchy tags them all `terminal`). Change
  the list with `kinetic glass apps <class|class*|tag> ...`, or turn it off
  with `kinetic glass apps none`.

`kinetic glass windows floating` and `kinetic glass windows all` widen it.
The window glass is drawn by Kinetic's Hyprland plugin, inside Hyprland's own
frame. It sits under the window the way Hyprland's blur does, and bends whatever
is behind it on that frame, including other windows. Its corners follow your
Hyprland rounding exactly. While window glass is on, Hyprland's own blur is
turned off, since the glass brings its own frost.

The installer builds the plugin and loads it at every login. Hyprland only
accepts a plugin built for its exact version, so Kinetic rebuilds it
automatically after an Omarchy update brings a new Hyprland.

```bash
kinetic glass plugin              # where it stands
kinetic glass plugin build        # build and load it by hand
```

## The pill bar

`kinetic.bar` isn't a copy of Omarchy's bar. Kinetic applies 16 small, named
hooks to the bar that ships with your Omarchy, so the pill keeps every upstream
change. An update hook rebuilds it after each Omarchy update. If an update ever
changes a part the hooks rely on, the rebuild stops, keeps the previous build
and notifies you.

```bash
kinetic bar on liquid             # the pill, in the liquid style
kinetic bar shape islands         # separate capsules instead of one
kinetic bar set radius full
kinetic bar set tint accent
kinetic bar options               # every option, with its range and default
kinetic bar off glassy            # back to one of Omarchy's backgrounds
```

Options apply live and are stored under `bar.pill` in
`~/.config/omarchy/shell.json`. Every option is defined once, in
`modules/bar/schema.json`, which the bar, the app and the CLI all read.

## Motion and parallax

![A workspace switch: the windows move a whole screen, the wallpaper a quarter of one](docs/parallax.gif)

`kinetic motion` sets Hyprland's animations: how workspaces switch (slide,
vertical slide, slide and fade, fade, or instant), how windows open, close and
move, and how menus and notifications appear. Each has a duration and a curve
(glide, smooth, snappy, linear, overshoot or spring).

With motion on, the wallpaper engine uses the same duration and curve as
Hyprland, so the wallpaper eases exactly with the windows. **Parallax** pans
the wallpaper a fraction of a screen per workspace while the windows move a
whole screen. Layered wallpapers move each layer at its own depth, and some
react to the switch itself with a gust, a flyby or a volley.

```bash
kinetic motion preset parallax    # slide, 500 ms glide, wallpaper at 0.25 screens per workspace
kinetic motion set parallax 0.4   # deeper
kinetic motion set workspaces slidevert
kinetic motion set workspaceCurve spring
kinetic motion show               # the Hyprland config and engine setting it produces
kinetic motion off                # Omarchy's own animations again
```

Presets: `parallax`, `vertical`, `deep`, `springy`, `snappy`, `calm` and `omarchy`.

![Pass-through: Super + 4 from workspace 1 slides past 2 and 3 on a single curve](docs/pass-through.gif)

**Pass-through** (`kinetic motion set passThrough true`) makes a jump across
several workspaces slide through each one on a single run of the switch curve,
instead of cutting straight to the last. Each extra workspace adds 110 ms
(`passMs`), or the whole trip takes the time of one switch with `passSameTime`.
A trip always ends on the workspace you chose. If something else switches
workspaces mid-trip (the bar, <kbd>Super</kbd> + <kbd>Tab</kbd>, an app asking
for focus), that switch wins.

Kinetic writes its animations to a single Hyprland toggle file and checks it
with Hyprland's own `--verify-config` first. `omarchy toggle animations` still
turns everything off.

## Looks

A look saves a wallpaper (with its preset and parameters), a bar and motion
together. Bundled looks live in `modules/look/looks/`, and your own in
`~/.config/kinetic/looks/`.

```bash
kinetic look list
kinetic look apply ink
kinetic look save mine            # snapshot what's showing now
```

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

Every part is optional, and a look changes only what it names.

## Modules

| Module | What it does | Help |
|---|---|---|
| `wall` | Live wallpapers on the native engine, and the glass under the bar | `kinetic wall help` |
| `bar` | The pill bar: a capsule or islands, in five styles | `kinetic bar help` |
| `glass` | Glass under the bar and behind windows, one look for both | `kinetic glass help` |
| `light` | The selection glow around windows | `kinetic light help` |
| `motion` | Hyprland's animations, wallpaper parallax and pass-through | `kinetic motion help` |
| `look` | Saved looks across all of the above | `kinetic look help` |

`kinetic <module> enable` and `disable` switch a module (so does the app's
Modules page). Switching a module on also switches on the modules it needs, and
switching one off asks first if others depend on it.

You can add modules from a GitHub link, a folder or an archive:

```bash
kinetic module install https://github.com/someone/kinetic-thing
kinetic modules                   # every module, on or off, and its version
```

Kinetic fetches the module and its dependencies, shows you everything it would
install, and installs nothing until you confirm. In the app, you can also drop
a folder, archive or link onto the window. [MODULES.md](MODULES.md) is the
guide to writing a module.

## Troubleshooting

**The wallpaper didn't come up at login.** The engine runs as
`kinetic-wall.service`. At login it waits for Omarchy's own background, startup
cover and login intro, then makes sure its wallpaper ends up on top. Every step
is logged:

```bash
journalctl --user -b -u kinetic-wall      # this login; -b -1 for the one before
```

For more detail, run `kinetic-wall daemon -v` or set `KINETIC_DEBUG=1`.

**There's no glass behind windows.** Check the plugin with
`kinetic glass plugin` and `hyprctl kinetic-glass status`. After a Hyprland
update, the new build loads at the next login.

**The bar didn't change.** Restart the shell with `omarchy restart shell`.

## Files

| Path | Contents |
|---|---|
| `~/.config/kinetic/wall.toml` | Engine options and per-wallpaper defaults |
| `~/.config/kinetic/wallpapers/` | Your own wallpapers |
| `~/.config/kinetic/looks/` | Your own looks |
| `~/.config/kinetic/motion.json` | Motion settings |
| `~/.config/omarchy/shell.json` | The bar's options, under `bar.pill` |
| `~/.local/state/kinetic/` | Live state, saved wallpaper worlds and signals between parts |
| `~/.local/state/omarchy/toggles/hypr/` | Kinetic's Hyprland toggles: `zz-kinetic-bar.lua`, `zz-kinetic-windows.lua`, `kinetic-motion.lua`, `kinetic-app.lua`, `kinetic-plugin.lua` |
| `~/.local/share/kinetic/` | The installed suite and bundled wallpapers |
| `~/.local/share/kinetic-modules/` | Modules you added |
| `~/.local/lib/kinetic/` | The Hyprland plugin |
| `~/.cache/kinetic/wall/thumbs/` | Wallpaper previews |

## Uninstalling

```bash
./uninstall.sh            # add --purge to also remove your settings and added modules
```

This stops and disables `kinetic-wall.service` and returns the bar to Omarchy's
own (a pill background becomes glassy). It removes Kinetic's shell plugins,
menu rows, launcher entry, post-update hook, Hyprland toggles and plugin, and
the `kinetic*` commands in `~/.local/bin`.

## Development

```bash
ninja -C modules/wall/build       # build the engine (first: meson setup modules/wall/build modules/wall --prefix ~/.local)
tests/run.sh                      # every offline test
docs/media.py                     # re-render the images in this README, offscreen
```

Everything in the test suite and in `docs/media.py` runs offscreen, without
touching the running desktop. [AGENTS.md](AGENTS.md) describes the layout,
build and internal contracts in detail.

## License

[MIT](LICENSE)
