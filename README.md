<div align="center">

# Kinetic

**A modular look-and-feel suite for [Omarchy](https://omarchy.org).**

Live wallpapers, rendered glass and fluid motion, as modules you can switch off, swap out or build on.

![Version 0.1.0-dev](https://img.shields.io/badge/version-0.1.0--dev-5b8def)
![Status: early development](https://img.shields.io/badge/status-early%20development-e5a50a)
![License MIT](https://img.shields.io/badge/license-MIT-3fb68b)
![Hyprland 0.56](https://img.shields.io/badge/Hyprland-0.56-58e1ff)
![Omarchy](https://img.shields.io/badge/Omarchy-Arch%20Linux-1793d1)

</div>

> [!WARNING]
> **Kinetic is in early development and may be unstable.** Expect bugs and
> breaking changes between releases. Part of it runs inside Hyprland as a
> plugin, so a bug there can crash your session. Try it on a setup you can
> recover, and keep `./uninstall.sh` in mind.

<div align="center">

![Four of Kinetic's live wallpapers: Aurora, Swarm, Wormhole and Reaction](docs/wallpapers.gif)

</div>

Kinetic adds live wallpapers, a floating glass bar, glass behind windows, a
glow around the focused window and workspace animations with parallax to
Omarchy. The parts work together: the glass bends the real wallpaper behind
it, and the wallpaper moves in step with your windows when you switch
workspaces.

Every part is a module. Switch off the ones you don't want, add new ones from
a link, or write your own.

## Contents

- [Features](#features)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Modules](#modules)
- [Live wallpapers](#live-wallpapers)
- [Glass](#glass)
- [The pill bar](#the-pill-bar)
- [Motion and parallax](#motion-and-parallax)
- [Looks](#looks)
- [Troubleshooting](#troubleshooting)
- [Files](#files)
- [Uninstalling](#uninstalling)
- [Development](#development)

## Features

- **33 live wallpapers** that follow your Omarchy theme, and can react to
  sound or pause when covered.
- **Glass** under the bar and behind see-through windows that bends, frosts
  and tints what's behind it.
- **A floating pill bar** built on Omarchy's own bar: one capsule or separate
  islands, in five styles.
- **A selection glow** around the focused window, with animations and an
  attention pulse.
- **Motion with parallax**: workspace and window animations, with the
  wallpaper sliding along at its own depth.
- **Looks** that save a wallpaper, bar and motion setup and apply it in one step.
- **Add-ons**: add wallpapers and modules from a link, a folder or an archive.
- **One app and one CLI** for all of it.

## Installation

You need Omarchy with Hyprland 0.56 or newer, plus a few build tools and
libraries:

```bash
omarchy pkg add meson ninja jq python wayland wayland-protocols libglvnd libpng libjpeg-turbo libwebp pipewire
```

Then:

```bash
git clone https://github.com/deseret-engineer/Kinetic.git
cd Kinetic
./install.sh
```

The installer runs as your user, without root. It installs Kinetic under
`~/.local`, adds it to the launcher and to **Omarchy menu → Appearance**, and
starts the wallpaper engine. Anything it replaces is moved to
`~/.local/share/kinetic/replaced/`, never deleted. If the bar doesn't change
right away, run `omarchy restart shell`.

## Quick start

```bash
kinetic app                       # open the app
kinetic wall set fluid            # change the wallpaper
kinetic look apply contours       # a wallpaper and a bar together
kinetic bar preset lens           # a strongly bending glass bar
kinetic glass enable              # glass under the bar and behind see-through windows
kinetic light enable              # a glow around the focused window
kinetic motion preset parallax    # sliding workspaces with wallpaper parallax
kinetic status                    # everything, as JSON
```

The app is also in the launcher (**Kinetic**) and under **Omarchy menu →
Appearance → Kinetic**. The 󰸉 widget on the bar handles the wallpaper; its
gear (or <kbd>o</kbd>) opens the app. In the app, <kbd>Ctrl</kbd> +
<kbd>1</kbd>–<kbd>9</kbd> and <kbd>Ctrl</kbd> + <kbd>Tab</kbd> move between
pages.

## Modules

```bash
kinetic modules                   # every module, on or off
kinetic light disable             # switch one off
kinetic light enable              # and back on
```

Switching a module on also switches on what it needs; switching one off asks
first if others depend on it. The app's Modules page does the same.

| Module | What it does |
|---|---|
| [`wall`](modules/wall/) | Live wallpapers |
| [`bar`](modules/bar/) | The pill bar |
| [`glass`](modules/glass/) | Glass under the bar and behind windows |
| [`light`](modules/light/) | The glow around the focused window |
| [`motion`](modules/motion/) | Animations, parallax and pass-through switching |
| [`look`](modules/look/) | Saved looks across the other modules |

Each has `kinetic <module> help`.

### Adding and writing modules

```bash
kinetic module install https://github.com/someone/kinetic-thing   # or a folder, or an archive
kinetic module remove kinetic-thing
```

Kinetic shows you everything it would install and waits for you to confirm.
In the app, paste a link on the Modules page or drop it onto the window.

To write a module, see [MODULES.md](MODULES.md). The built-in modules use
nothing a module you add couldn't, so they double as examples.

## Live wallpapers

![A dozen of Kinetic's wallpapers](docs/wallpapers.png)

Wallpapers take their colors from your Omarchy theme. Some simulate a world
(particles, ants, fluid, a garden) that picks up where it left off; others
react to sound, the time of day or a workspace switch.

```bash
kinetic wall list                 # every wallpaper
kinetic wall set aurora           # show one
kinetic wall next                 # the next one
kinetic wall pause                # hold the current frame
```

### Adding wallpapers

```bash
kinetic wall add https://github.com/someone/kinetic-wallpapers   # a link
kinetic wall add ~/Downloads/nebula.zip                          # an archive
kinetic wall add ~/src/my-wallpaper                              # a folder
kinetic wall add ./nebula --as nebula-2                          # under another name
kinetic wall added                                               # the ones you added
kinetic wall remove nebula
```

A source can hold one wallpaper or a whole collection. Kinetic checks that
each one builds before adding it. In the app, paste a link into the Library
page or drop it onto the window.

### Making your own

```bash
kinetic wall new mine             # scaffold one in ~/.config/kinetic/wallpapers/mine
kinetic wall set mine             # it reloads live as you edit
```

Wallpapers are GLSL shaders plus a `wallpaper.toml`; the format is in
[modules/wall/WALLPAPERS.md](modules/wall/WALLPAPERS.md). Share them as a Git
repository and anyone can `kinetic wall add` the link.

## Glass

![The glass in three looks: clear, frosted and prismatic](docs/glass.png)

Kinetic's glass bends what's actually behind it, most strongly at the rim. It
can also frost, split into color fringes and catch a rim light. One look
applies to both the bar and the windows:

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
| `curvature` | Edge profile: below 1 a soft slope, above 1 a sharp rim |
| `saturation`, `brightness` | The color of what shows through |
| `specular`, `lightAngle` | Light catching the rim, and where it comes from |

By default, window glass goes only behind windows you can see through:
translucent windows and terminals. Change which apps count with
`kinetic glass apps <class|class*|tag> ...`, or cover more windows with
`kinetic glass windows floating` or `all`.

Window glass is drawn by a small Hyprland plugin. The installer builds it, and
Kinetic rebuilds it after Omarchy updates Hyprland. Check on it with
`kinetic glass plugin`.

## The pill bar

The pill is Omarchy's own bar with a new shape, so it keeps up with Omarchy
updates.

```bash
kinetic bar on liquid             # the pill, in the liquid style
kinetic bar shape islands         # separate capsules instead of one
kinetic bar set radius full
kinetic bar set tint accent
kinetic bar options               # every option, with its range and default
kinetic bar off glassy            # back to one of Omarchy's backgrounds
```

Styles: liquid, glass, frosted, solid and outline. Options apply live and are
saved under `bar.pill` in `~/.config/omarchy/shell.json`.

## Motion and parallax

![A workspace switch: the windows move a whole screen, the wallpaper a quarter of one](docs/parallax.gif)

`kinetic motion` sets how workspaces switch, how windows open, close and move,
and how menus and notifications appear, each with its own duration and curve.
With **parallax**, the wallpaper pans a fraction of a screen per workspace
while the windows move a whole one.

```bash
kinetic motion preset parallax    # slide with the wallpaper at 0.25 screens per workspace
kinetic motion set parallax 0.4   # deeper
kinetic motion set workspaces slidevert
kinetic motion set workspaceCurve spring
kinetic motion off                # Omarchy's own animations again
```

Presets: `parallax`, `vertical`, `deep`, `springy`, `snappy`, `calm` and `omarchy`.

![Pass-through: Super + 4 from workspace 1 slides past 2 and 3 on a single curve](docs/pass-through.gif)

**Pass-through** (`kinetic motion set passThrough true`) slides through every
workspace in between when you jump several at once, instead of cutting
straight to the last.

`omarchy toggle animations` still turns all animation off.

## Looks

A look saves a wallpaper, a bar and motion together.

```bash
kinetic look list
kinetic look apply ink
kinetic look save mine            # save what's showing now
```

Your looks are JSON files in `~/.config/kinetic/looks/`:

```json
{
  "name": "Ink",
  "wall": { "wallpaper": "fluid", "preset": "Ink" },
  "bar": { "background": "pill", "preset": "lens" },
  "motion": { "preset": "parallax" }
}
```

Every part is optional; a look changes only what it names.

## Troubleshooting

**The wallpaper didn't come up.** Check the engine's log:

```bash
journalctl --user -b -u kinetic-wall
```

**There's no glass behind windows.** Run `kinetic glass plugin`. After a
Hyprland update, the rebuilt plugin loads at your next login.

**The bar didn't change.** Run `omarchy restart shell`.

## Files

| Path | Contents |
|---|---|
| `~/.config/kinetic/` | Your settings, wallpapers and looks |
| `~/.config/omarchy/shell.json` | The bar's options, under `bar.pill` |
| `~/.local/state/kinetic/` | Live state and saved wallpaper worlds |
| `~/.local/share/kinetic/` | The installed suite and bundled wallpapers |
| `~/.local/share/kinetic-modules/` | Modules you added |

## Uninstalling

```bash
./uninstall.sh            # add --purge to also remove your settings and added modules
```

This removes everything Kinetic installed and returns the bar to Omarchy's own.
Without `--purge`, your settings, wallpapers and added modules are kept.

## Development

```bash
ninja -C modules/wall/build       # build the engine (first: meson setup modules/wall/build modules/wall --prefix ~/.local)
tests/run.sh                      # every offline test
docs/media.py                     # re-render this README's images
```

Tests and media rendering run offscreen without touching your desktop.
[AGENTS.md](AGENTS.md) covers the layout and internals. Kinetic follows
[Semantic Versioning](https://semver.org); development builds carry a `-dev`
label.

## License

[MIT](LICENSE)
