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

### Changed

- Looks are now called presets: `kinetic preset`, the Presets page, and
  `~/.config/kinetic/presets/`. Reinstalling moves saved looks over, and
  `kinetic look` and `kinetic app looks` still work for now.

### Fixed

- `kinetic motion options` printed true/false defaults as `True`/`False`.
- The engine skipped a wallpaper folder whose path was too long instead of
  checking a cut-off path, and no longer lists a wallpaper twice when its name
  is over 63 characters.

[0.1.0]: https://github.com/deseret-engineer/Kinetic/releases/tag/v0.1.0
