# AGENTS.md — Kinetic

Notes for coding agents. `README.md` is the user guide;
`modules/wall/WALLPAPERS.md` is the wallpaper-format reference; `MODULES.md`
is the module author's guide (manifest, commands, what Kinetic gives modules).

## What it is

Kinetic is a look-and-feel suite for Omarchy (Arch + Hyprland + the Quickshell
"Omarchy shell"). Its parts are modules: `wall` (the `kinetic-wall` C engine:
live wallpapers and the glass under the bar), `bar` (a floating pill bar, built
as hooks on Omarchy's stock bar rather than a fork), `glass` (window glass, a
Hyprland plugin), `light`, `motion` and `preset`, all driven by one CLI and one
Quickshell app.

## Layout

```
kinetic                   dispatcher: kinetic <module> ... | panel | status
lib/kinetic.sh            the module toolkit: facts snapshot (shared per command), engine/bar/module helpers,
                          Hyprland toggles, confirm, notify. Modules use these instead of each re-checking
lib/modman.py             module manager: manifests, dependency order, install/remove from links, folders, archives
lib/wallman.py            wallpapers: kinetic wall add <link|folder|archive> (one or a collection, compiled first) / remove / added
lib/schema.js             normalize()/items() over any module's schema.json; used by the bar and the panel; pure
lib/schema.py             the same for the command line; tests/schema.test.js keeps the two identical
modules/<name>/module.sh  one per module; answers active|enable|disable
modules/<name>/module.json its manifest: name, version, icon, order, requires (+ source links), pages, engineGroups
modules/glass/            the glass switch and one look for bar + windows (bar options and engine window_* options); `kinetic glass plugin`
modules/glass/plugin/     kinetic-glass.cpp: the Hyprland plugin drawing window glass and the selection light in Hyprland's frame; build.sh
modules/light/            the selection light's CLI over the engine's light_* options (the plugin draws it)
modules/wall/             C engine (src/, Makefile: make, gcc and pkg-config only), wallpapers/, integration/ (unit, picker, example config)
modules/bar/schema.json   EVERY pill option: type, range, default (may depend on an earlier option), group, label
modules/bar/qml/          PillSurface, PillRing, PillIslands, PillContent, PillGlass (copied into kinetic.bar)
modules/bar/build.py      makes ~/.config/omarchy/plugins/kinetic.bar from the stock bar + 16 named hooks
modules/bar/blur          Hyprland blur toggle (installed as kinetic-bar-blur)
modules/motion/           schema.json (options, curves, presets) + motion.py: Hyprland toggle + engine timing; trip.py: pass-through trips cut into per-step curves
modules/wall/src/motion.c workspace easing on Hyprland's curve, parallax pan (uParallax, lw_Shift on the image pass)
modules/preset/           module.sh + presets/*.json (bundled presets: wallpaper, bar and motion together)
shell/kinetic/            the bar widget (Panel.qml, wallpaper and library) and the app (App.qml, every module),
                          both on KineticCtx.qml (state + commands); pages/, components/, Model.js.
                          Installed twice: plugins/kinetic (bar-widget) and plugins/kinetic.app (service,
                          manifest shell/app-manifest.json, enabled through shell.json plugins[])
modules/wall/src/lens.c   rendered glass under the bar: capsules (socket), optics, placement. plugin.c: sends the plugin its settings
modules/wall/src/mod_shell.c the guard: keeps Kinetic's layer the one on screen over the shell's background and covers and owe's layer
modules/wall/src/layers.c reads Hyprland's j/layers (what is stacked over Kinetic's layer); json.c: the mini JSON walker it and mod_hyprland share
integration/              Omarchy menu rows (menu.jsonc + menu.py), post-update hook
tests/                    run.sh runs everything offline: schema, model, motion, layers and lw_run (C), hook check, Hyprland verify of presets,
                          pass_through_test.lua: the generated pass-through Lua against a simulated Hyprland + Omarchy + engine
```

## Build, test

```bash
make -C modules/wall -j                                        # the engine, into modules/wall/build (tests/run.sh builds it too)
KINETIC_WALL_PATH=modules/wall/wallpapers modules/wall/build/kinetic-wall validate
KINETIC_WALL_PATH=modules/wall/wallpapers modules/wall/build/kinetic-wall thumbnail topo /tmp/t.png 1920x1200 10 \
    --glass 10 0.6 0.35 "8,6,600,36,18"                        # the bar glass, offscreen
python3 modules/bar/build.py --check                           # do all hooks still apply to this Omarchy's bar?
python3 modules/bar/build.py --out /tmp/kb && /usr/lib/qt6/bin/qmlformat /tmp/kb/Bar.qml >/dev/null   # parses
tests/run.sh                                                   # every offline test
KINETIC_WALL_PATH=modules/wall/wallpapers modules/wall/build/kinetic-wall thumbnail topo /tmp/p.png 1920x1200 8 --parallax 0.25,0
./kinetic motion show                                          # the Lua and engine line motion would write
./kinetic bar options | ./kinetic preset list                    # the CLIs run from the repo too
./install.sh                                                   # installs + migrates; touches the live desktop
```

## Versioning

[Semantic Versioning](https://semver.org). Kinetic's version is `KINETIC_VERSION`
in `lib/kinetic.sh`; the engine's Makefile reads it from there, both shell
manifests and the Hyprland plugin (`kinetic-glass.cpp`, twice) carry the same
one, and `tests/versions_test.py` fails when they drift. Breaking change:
MAJOR; feature: MINOR; fix: PATCH (while MAJOR is 0, anything may change).
Development builds are pre-releases, `X.Y.Z-dev`, tagged `vX.Y.Z-dev` (annotated);
drop `-dev` only for a real release.
Every user-visible change gets a line in `CHANGELOG.md` (Keep a Changelog) under the coming version.
`modman.py` checks module versions and `requires` by SemVer precedence (`vkey`).

## Contracts (other parts depend on these)

| Interface | Detail |
|---|---|
| `kinetic-wall` socket | `$KINETIC_WALL_SOCKET`, else `$XDG_RUNTIME_DIR/kinetic/wall.sock`. One command line in, one reply out. `describe` is the JSON the panel is built from: keep it stable |
| `lens` command | `lens <output> <refract> <frost> <dispersion> <x,y,w,h,r;...> [bevel=..,curve=..,sat=..,bright=..,tint=..,spec=..,light=..]` (logical px, y down, at most 16), `lens <output> off`, `lens` lists. Sent by `PillGlass.qml`. Only the bar's: window glass is the plugin's |
| `kinetic-glass` hyprctl command | Registered by the plugin: `kinetic-glass set key=value ...` (glass, apps (comma separated), refract, bevel, curve, frost, disp, sat, bright, tint, tintrgb, spec, angle; light, anim, color, rainbow, pal (6 `r,g,b` joined by `;`), strength, unfocused, outside, inside, edge, softness, speed, sound, urgent, urgentrgb, fade) answers `ok` or the errors; `kinetic-glass status` is JSON. `plugin.c` sends the whole line when it changes, on connect, and on the plugin's `kinetic-glass>>ready` event (posted on every load). Sound level: `$XDG_RUNTIME_DIR/kinetic/light-level`, two floats the engine writes, the plugin mmaps |
| Module switch | Every `module.sh` answers `active` (prints true/false), `enable`, `disable`. The dispatcher switches `requires` on first and asks before stopping dependents. `kinetic modules --json` (modman list) is what the app builds its sidebar and Modules page from |
| Added modules | `~/.local/share/kinetic-modules/<id>` (`KINETIC_MODULES_USER`), with `.kinetic-install.json` (source, commit). install.sh never touches it. Built-in ids win and can't be taken. `tests/modules_test.sh` covers install, dependencies, yes, removal and refusals |
| Added wallpapers | `kinetic wall add` (`lib/wallman.py`, fetching through `modman.fetch` with its own root finder) copies each wallpaper that `kinetic-wall validate` compiles into `~/.config/kinetic/wallpapers/<id>` with `.kinetic-source.json`, then renders previews. Ids come from the folder (a single wallpaper: the source's own name); bundled ids are refused without `--as`. `--json` answers `{added, skipped}` or `{error, notWallpaper}`; the app's drop tries a wallpaper first and hands `notWallpaper` sources to the module installer. `tests/wallpapers_test.sh` covers it (in a scratch config, away from the live engine) |
| Facts | `KINETIC_FACTS`: the dispatcher's temp file, filled once by `facts`, shared by nested calls; `facts_refresh` after a change |
| `kinetic.app` IPC | `omarchy-shell kinetic.app open <module>`, `close`, `toggle` (what `kinetic app` runs) |
| Still mode | `kinetic-wall off` with `glass_still` and an image background keeps the engine showing the `still` wallpaper; status/describe say `enabled: false, still: true`, the signal stays `on` |
| `motion` command | `motion <ms> bezier x1,y1,x2,y2 | spring mass,stiffness,dampening <parallax> h|v [slide|fade|slidefade:<pct>] [window settings]`, or `motion off [window settings]`. Window settings (`motion.py` `window_spec`): `open=<ms>/bezier|spring/<points>/popin:<pct>|slide|gnomed` (or `open=off`), `move=<ms>/<curve>/<points>`, `close=` like open, `fade=on|off`: Hyprland's windowsIn, windowsMove, windowsOut and fade. The engine checks them and refuses a bad line, but plays none (window glass is the plugin's). Applied live and kept in `signals/motion`, which `kinetic motion` also writes, so the engine starts with it |
| `kinetic-travel` event | Pass-through: the toggle's Lua sends Hyprland's custom event `kinetic-travel <output> <n> <dir> <trip ms> <x1,y1,x2,y2> <to>` before the first step, `to` being the workspace id it ends on (the engine's own id plus n when absent: that can be a step behind). `kinetic-travel <output> 0` calls it off; the Lua sends it whenever Hyprland won't end at `to` (another switch wins, a trip lands early) and a newer trip's event replaces an older one's. The trip is one run of that bezier (the switch curve, squeezed so it overshoots like one switch); Hyprland plays it as n slices made by `modules/motion/trip.py`, and the engine traces the bezier itself. `tests/trip_test.py` keeps the two equal |
| Pass-through trips | Each step's timer is set at the press; the last step is `o.workspaces.focus(slot, monitor)`, checked. The Lua knows its own switches from others through `workspace.active` (Hyprland calls it synchronously) and gives way to any other. `tests/pass_through_test.lua` (run by `pass_through_test.py` for every sliding preset) must pass: every scenario ends on the last workspace chosen, with the engine's trip ending there too |
| Motion toggle | `~/.local/state/omarchy/toggles/hypr/kinetic-motion.lua`: must sort BEFORE `no-animations.lua` so Omarchy's off switch wins. Verified with `Hyprland --verify-config` before it is written |
| Background guard | `mod_shell.c`, run on Hyprland's `openlayer`/`closelayer` for `omarchy-background`, `owe-background` and `kinetic-wall`, and after each first frame: reads `j/layers` (each level's list is the stacking order, last on top), waits while a shell cover (`omarchy-background` on bottom/overlay) is up, suspends the shell's background (`omarchy-shell background setSuspended`, its answer checked), lets owe's intro play (`owe status` `"intro":true`, at most 45 s) and otherwise raises Kinetic's layer (a `set_layer` hop to the next level and back; a new surface if that did not do it). Bounded: 5 tries per episode with backoff, 20 actions per 5 minutes. Never blind suspends at the first frame: the shell's cover fades only once its background was up |
| Engine startup | Waits about 20 s for the Wayland display and retries Hyprland's socket for about a minute (finding the instance under `$XDG_RUNTIME_DIR/hypr` if `HYPRLAND_INSTANCE_SIGNATURE` is missing). A paused output still draws one first frame. Layers the compositor closes are remade (3 times in a row at most); a layer with no first frame after 5 s is diagnosed in the log. Every step is in `journalctl --user -u kinetic-wall` |
| Engine signal | `~/.local/state/kinetic/signals/wall`: `on <pid>` while showing, `off` otherwise. It sits in its own directory because the bar's FileView fires on any change in that directory. The pid makes a restart visible, so the bar re-sends its capsules |
| `bar.pill` in shell.json | Raw options; the bar normalizes them with `schema.js` against `KineticEnv.SCHEMA` |
| `KineticEnv.js` | Generated into both plugins by `build.py` (install paths + schema). Never edit it by hand |
| Plugin ids | `kinetic.bar` (bar), `kinetic` (panel; IPC targets `kinetic`, `kinetic.data`) |
| Hyprland toggle | `~/.local/state/omarchy/toggles/hypr/zz-kinetic-bar.lua`: must sort after `window-glassy.lua` |
| Window blur toggle | `zz-kinetic-windows.lua` (same dir), written by `kinetic-window-blur on|off` (`all|floating|see-through` still mean on): `decoration.blur.enabled = false`, Hyprland's blur off while window glass is on. The engine (plugin.c) runs it from beside its own binary while `window_glass` is not off (unless `window_hypr_blur`), and with `off` when it stops |
| Hyprland plugin | `~/.local/lib/kinetic/kinetic-glass.so` (+ `.hyprland`: the Hyprland commit it was built for), loaded by the toggle `kinetic-plugin.lua` (`hl.plugin.load`; Hyprland loads and unloads config plugins on reload). Built by install.sh and `kinetic glass plugin build`, rebuilt by the post-update hook when the headers' commit changes. Refuses a mismatched Hyprland (hash check), never loads into one. Per frame: lights after the background layer (`RENDER_POST_WALLPAPER`); each covered window a `DECORATION_LAYER_BOTTOM` decoration that copies what is behind it from the frame and draws the glass; damage touching a covered window widens to all of it plus the frost's reach (`widenDamage`, as live blur does). Corners use Hyprland's rounding test (`rounding.glsl`, `rounding_power`). Test changes in a nested Hyprland first (it renders only while its window is visible) |
| See-through windows | `window_glass see-through`: the opacity Hyprland draws the window at right now (`WINDOW_ALPHA_ACTIVE`) ≤ 0.95, or on `window_glass_apps` (classes, `prefix*`, Hyprland tags; default `terminal`, Omarchy's tag for every terminal). Decided by the plugin on every frame |
| App window | `App.qml`'s `open` maps the window afresh every time: Hyprland closing it (Super + W) leaves Quickshell's `visible` true |
| Menu | Rows between `// kinetic:begin` and `// kinetic:end`; `integration/menu.py` adds and removes them. Nested by dotted id: `appearance.kinetic` holds the app's pages and the `appearance.kinetic.wallpaper` submenu (wallpaper actions and the wall module's pages) |
| App sidebar | `KineticCtx.pages` is module by module (modules' `order`), each module's pages by their own `order`. `App.qml` makes a module with several pages a group: its first page is the row, the rest carry `parent` and nest (folding). Ctrl + 1–9 / Ctrl + Tab walk `rowIds`, the rows showing |

## Adding a bar option

1. `schema.json`: add it (type, range, default, group, label). Defaults may be
   `{"by": "style", "values": {...}, "else": x}` or
   `{"from": "opacity", "factor": .5, "min": a, "max": b}`, and may only read
   options listed earlier.
2. Use it in `qml/PillSurface.qml` (or the geometry in `build.py`'s root
   properties).
3. Add a label for a new group in `shell/kinetic/i18n/en.json`. The panel row,
   CLI validation and `kinetic bar options` follow from the schema on their own.
4. `tests/run.sh`.

Motion options work the same way (`modules/motion/schema.json`). A new
Hyprland style must pass `Hyprland --verify-config`; `tests/run.sh` checks every
preset with it. Leaf names: `hyprctl animations`.

## Adding a module

Create `modules/<name>/` with `module.json` and an executable `module.sh`
answering `active`, `enable` and `disable` (MODULES.md). Its app pages come from
`pages` in the manifest (`builtin:<Name>Page` for one in `shell/kinetic/pages/`);
several pages make it a group in the app's sidebar, the first page leading.
install.sh copies every module folder; nothing else lists modules. For a panel tab, add `shell/kinetic/pages/<Name>Page.qml`
taking `ctx` (the panel root: `send(module, args)`, `setValue`, `fg`, `tr` …)
and a tab entry in `Panel.qml`.

## Hard rules

- Never edit `/usr/share/omarchy/`. Read it; build from it.
- Never hand-edit the generated `~/.config/omarchy/plugins/kinetic.bar`; change
  `modules/bar` and rebuild.
- QML edits load only after `omarchy restart shell`. That restarts the user's
  bar, so ask first. Check with `grim` and
  `journalctl --user --since -1min | grep -iE "kinetic|pill"`.
- Don't change the user's live wallpaper or bar to test something; use the
  offscreen `thumbnail`/`bench`/`validate` and `build.py --check`.
- Kinetic builds with what Omarchy ships (`omarchy-base.packages` and what those
  pull in): make, gcc, pkg-config, jq, python, Wayland, GLES, image libraries,
  PipeWire, Hyprland's headers. A new build dependency needs a very good reason.
- `install.sh` stays rootless and never deletes the user's own files. Keep
  README's Uninstall in step with it.
- Text from config or the daemon shown in QML uses `textFormat: Text.PlainText`.
  Anything that reaches a command line passes `Model.safeArg`.
- No polling and no busy work: the engine waits in one epoll, and the bar and
  panel react to file watches and signals.
- Wallpaper params must not collide with identifiers in their GLSL. This has
  bitten twice.
- Wallpapers move with workspaces only through `uParallax` / `lw_pan` (or
  `parallax = 1`), so the motion settings drive all of it. `uWorkspace.x` is
  for switch reactions (`z - x`); `tests/run.sh` fails on any other use.
