# Changelog

All notable changes to Kinetic are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and Kinetic follows
[Semantic Versioning](https://semver.org). While the major version is 0,
anything may change between releases.

## [0.1.0] - Unreleased

The first release.

### Added

- `wall`: the `kinetic-wall` engine and 33 live wallpapers that follow the
  Omarchy theme, react to sound and pause when covered. Wallpapers can be added
  from a link, a folder or an archive (`kinetic wall add`), or scaffolded with
  `kinetic wall new`.
- `bar`: the pill bar, built as hooks on Omarchy's own bar, in five styles and
  two shapes, with seven presets.
- `glass`: glass under the bar and behind see-through windows, drawn by a
  Hyprland plugin that is rebuilt after Hyprland updates.
- `light`: a glow around the focused window, with animations and an attention
  pulse.
- `motion`: workspace and window animations with wallpaper parallax and
  pass-through switching, in seven presets.
- `preset`: a wallpaper, a bar and motion saved and applied together, with six
  bundled presets.
- Modules can be switched on and off, and added or removed from a link, a
  folder or an archive (`kinetic module install`).
- The Kinetic app, the bar widget, the `kinetic` CLI, and rows in the Omarchy
  menu.
- A Settings page in the app (Ctrl + ,) for the app's own settings: updates,
  about, and attributions for the work Kinetic includes or follows (also in
  `THIRD-PARTY.md`).
- `kinetic update`: check for and install updates from a branch of the
  repository Kinetic came from (main, dev or another), from the terminal or
  Settings, which can also look once a day when the app opens. Updates are
  fetched into a clone of Kinetic's own, never your working copy.
- `kinetic settings`: the app's own settings (the update branch, and whether to
  check).
- `kinetic reload`, and a Reload button in the Kinetic app (Ctrl + R or F5): restarts
  the engine, the glass plugin, the bar and the app when something looks wrong.

### Changed

- `kinetic modules` (which the app runs after changes) asks every module at
  once and answers in about 100 ms instead of 170. A slider drag sends only
  the latest value of a setting and refreshes the module list once it stops.
- Installing needs nothing beyond a stock Omarchy: the engine builds with
  `make` instead of Meson and Ninja, and `install.sh` names only the packages
  that are missing (if you removed any) instead of listing them all.
- Looks are now called presets: `kinetic preset`, the Presets page, and
  `~/.config/kinetic/presets/`. Reinstalling moves saved looks over, and
  `kinetic look` and `kinetic app looks` still work for now.
- Galaxy Collision has more settings: one to five galaxies, their shape
  (spiral, irregular, ring, lenticular, elliptical or mixed), size and spin,
  how much they differ, how far apart they start, the collision angle and
  speed and how much each encounter varies them, the spiral arms (strength,
  count, winding), the background stars, the background color (the theme's,
  black or your own), and a camera (zoom, turn, pan). It
  has presets, draws 65,536 stars by default, skips stars off the screen, and
  Start over begins a new encounter.
- Wallpapers can have up to 38 parameters (was 22), and one with more fails
  to load instead of silently losing the rest.
- The app's Start over button no longer carries a description.

### Fixed

- The wlr-layer-shell protocol file Kinetic includes had lost its authors'
  copyright and permission notice; it is back, and the glass plugin's corner
  rounding, which follows Hyprland's, carries Hyprland's notice.
- Installing a module from an archive checks the size limits before
  unpacking, so an archive that expands to gigabytes is refused before it
  fills the disk, and a link's subfolder can no longer point at a sibling
  folder whose name starts the same (`../src2` beside `src`).
- The app could miss the last change: a refresh asked for while another was
  running was dropped. It now runs once more when that one ends.
- The engine read at most 511 bytes of a command, so a `lens` line for many
  capsules was cut off and applied wrong; it now takes 4 KB and refuses
  anything longer. A client that never reads its reply can no longer hold up
  the engine.
- The Size setting of Swarm, Particle Life and Fireworks did nothing: the
  shaders' point size had the same name and hid it. Their default look is
  unchanged (Fireworks' and Particle Life's defaults are now 1, the size they
  always drew at), and `tests/run.sh` now fails on any parameter a GLSL name
  hides.
- `kinetic reload` said the engine restarted when it wasn't running, and
  left the plugin alone when only the selection light was on.
- Galaxy Collision drew a dense ring of stars around each galaxy, where stars
  beyond the disk's edge had been piled onto it.
- Galaxy Collision dimmed on workspaces with windows, where the engine runs at
  a lower frame rate and its trails held less light. Wallpapers with trails
  can use `lw_trail_gain` to stay as bright at any frame rate.
- `kinetic motion options` printed true/false defaults as `True`/`False`.
- The engine skipped a wallpaper folder whose path was too long instead of
  checking a cut-off path, and no longer lists a wallpaper twice when its name
  is over 63 characters.

[0.1.0]: https://github.com/deseret-engineer/Kinetic/releases/tag/v0.1.0
