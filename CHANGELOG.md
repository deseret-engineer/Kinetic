# Changelog

Notable changes to Kinetic, in the
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) format. Kinetic follows
[Semantic Versioning](https://semver.org); while the major version is 0,
anything may change between releases.

## [0.1.0] - 2026-11-20

The first release. Changed and Fixed are against `0.1.0-dev`.

### Added

- `wall`: the `kinetic-wall` engine and 33 live wallpapers in the Omarchy
  theme's colors, reacting to sound and pausing when covered. Add more from a
  link, a folder or an archive (`kinetic wall add`), or start one with
  `kinetic wall new`.
- `bar`: the pill bar, built as hooks on Omarchy's own bar: five styles, two
  shapes, seven presets.
- `glass`: glass under the bar and behind see-through windows, drawn by a
  Hyprland plugin that is rebuilt when Hyprland updates.
- `light`: an animated glow around the focused window, with an attention pulse.
- `motion`: workspace and window animations with wallpaper parallax and
  pass-through switching, in seven presets.
- `preset`: a wallpaper, a bar and motion saved and applied together; six
  bundled.
- Modules switch on and off, and install from a link, a folder or an archive
  (`kinetic module install`).
- The Kinetic app, the bar widget, the `kinetic` CLI and Omarchy menu rows.
- Settings in the app (the gear beside Reload): updates, about, and
  attributions (also in `THIRD-PARTY.md`).
- `kinetic update`: check for and install updates from a branch of the
  repository Kinetic came from, in the terminal or Settings, or daily when the
  app opens. Updates use Kinetic's own copy, never your working copy.
- `kinetic settings`: the update branch, and whether to check.
- `kinetic reload`, and Reload in the app (Ctrl + R or F5): restarts the engine,
  glass plugin, bar and app.

### Changed

- Installing needs nothing beyond a stock Omarchy: the engine builds with
  `make` instead of Meson and Ninja, and `install.sh` names only missing
  packages.
- Looks are now presets: `kinetic preset`, the Presets page and
  `~/.config/kinetic/presets/`. Reinstalling moves saved looks over;
  `kinetic look` and `kinetic app looks` still work for now.
- Galaxy Collision has many more settings: one to five galaxies; their shape,
  size and spin; their start, collision angle and speed, and how much each
  encounter varies; the spiral arms; the background stars and color; and a
  camera. It also has presets, draws 65,536 stars by default and skips those
  off screen, and Start over begins a new encounter.
- Wallpapers can have 38 parameters (was 22); one with more no longer loads,
  rather than silently losing the rest.
- `kinetic modules`, which the app runs after changes, answers in about 100 ms
  (was 170). A slider drag sends only its latest value and refreshes the
  module list once, when it stops.
- The app's Start over button has no description.

### Fixed

- Every install that changed the Omarchy menu left another timestamped backup
  of it; now there are two, the original and the latest.
- The wallpaper picker removed every preview if the wallpaper list failed.
- The app's Update button did nothing: `kinetic` passes `--yes` on as
  `KINETIC_YES`, which `kinetic update` didn't read. An update refused for want
  of a yes now says so in Settings.
- Swarm, Particle Life and Fireworks: the Size setting did nothing (a shader
  name hid it). Their default look is unchanged.
- Galaxy Collision drew a dense ring around each galaxy, and dimmed on
  workspaces with windows (wallpapers with trails can use `lw_trail_gain`).
- The app could miss the last change when a refresh was asked for during
  another.
- Module installs check archive size limits before unpacking, and a link's
  subfolder can no longer reach a sibling folder (`../src2` beside `src`).
- Added modules can no longer take a name `kinetic` itself answers to
  (`update`, `settings`, `status` …) or the app's own page ids.
- The engine cut commands off at 511 bytes, so a long `lens` line was applied
  wrong; it now takes 4 KB and refuses longer. A client that never reads its
  reply can't hold up the engine.
- The engine checked a cut-off path for an over-long wallpaper folder, and
  could list a wallpaper twice when its name was over 63 characters.
- `kinetic reload` said the engine restarted when it wasn't running, and left
  the plugin alone when only the light was on.
- `kinetic motion options` printed defaults as `True`/`False`.
- The wlr-layer-shell protocol file had lost its authors' notice; it is
  restored, and the glass plugin's corner code carries Hyprland's.

[0.1.0]: https://github.com/deseret-engineer/Kinetic/releases/tag/v0.1.0
