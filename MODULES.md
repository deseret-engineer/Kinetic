# Writing a Kinetic module

A module is a folder with two files: `module.sh`, its commands, and
`module.json`, its manifest. Kinetic finds it, lists it, switches it on and off
in the right order with what it needs, and shows its pages in the app.

```
my-module/
  module.json        what it is, what it needs, its pages
  module.sh          its commands (executable)
  app/Page.qml       optional: a page in the Kinetic app
```

Kinetic's own modules are in `modules/` and are written the same way: read
`modules/light/` for a small one.

## Install, try, remove

```bash
kinetic module install ./my-module                  # a folder
kinetic module install https://github.com/you/my-module
kinetic module install https://github.com/you/mods/tree/main/my-module   # a branch and a subfolder
kinetic module install my-module.tar.gz             # .zip, .tar.gz, .tar.xz
kinetic modules                                     # every module, on or off, its version
kinetic my-module enable                            # what it needs comes on first
kinetic module update my-module                     # fetch it again from where it came from
kinetic module remove my-module
```

The app's Modules page does the same: paste a link, or drop a folder, archive
or link on the window.

Installing first fetches the module and everything it would bring into a
staging folder, runs none of it, and shows each module: why it's coming, where
from (and the commit), its files, its pages, and whether it runs an install
step. Nothing installs without a yes, in the terminal (`--yes`) or the app.
Added modules live in `~/.local/share/kinetic-modules/`, so reinstalling Kinetic
keeps them, and can't reuse a built-in module's id.

## module.json

```json
{
  "id": "aurora-clock",
  "name": "Aurora clock",
  "summary": "A clock drawn into the wallpaper",
  "version": "1.0.0",
  "author": "you",
  "icon": "󰥔",
  "order": 70,
  "kinetic": ">=0.1.0-dev",
  "requires": [
    { "id": "wall", "why": "the clock is drawn by the wallpaper engine" },
    { "id": "fonts-extra", "version": ">=1.2", "source": "https://github.com/you/kinetic-fonts-extra" },
    { "id": "bar", "optional": true, "why": "shows the time on the pill too" }
  ],
  "pages": [
    { "id": "clock", "label": "Clock", "about": "Its face and its colors", "icon": "󰥔", "qml": "app/Page.qml" }
  ],
  "engineGroups": [],
  "hooks": ["install", "uninstall"]
}
```

| Key | |
|---|---|
| `id` | Lowercase letters, digits and dashes, starting with a letter. It is also the command (`kinetic aurora-clock ...`), so it can't be one of Kinetic's: `app`, `panel`, `status`, `reload`, `update`, `settings`, `modules`, `module`, `version`, `help` |
| `name`, `summary`, `icon`, `author` | How it shows in `kinetic modules` and the app. `icon` is a Nerd Font glyph |
| `version` | [Semantic Versioning](https://semver.org), like `1.2.0` or `1.2.0-beta.1`: MAJOR for changes that break modules using yours, MINOR for features, PATCH for fixes. Others can require one (`>=1.2`) |
| `order` | Where it sorts in lists and in the app's sidebar (Kinetic's own use 10 to 90) |
| `kinetic` | The Kinetic it needs, like `>=0.1.0-dev`; install refuses an older one. A development build ranks below its release, so `>=0.1` refuses `0.1.0-dev` |
| `requires` | What it needs. Each is an id, or `{id, why, version, source, optional}`. `source` is a link Kinetic fetches the dependency from if it is not installed (it asks first, with the rest). `optional`: used if there, never installed or switched on for you |
| `pages` | Its app pages: `{id, label, icon, qml, order}` and an optional `about` (not shown now). `qml` is a file in the module; `order` sorts the module's own pages. Ids `modules` and `settings` are the app's. Several pages make a group in the sidebar: the first is the row, the rest nest under it (as Library and Engine do under Wallpaper) |
| `engineGroups` | Option groups of the wallpaper engine your module shows on its own page (the Engine page leaves them out) |
| `hooks` | `install` and `uninstall`: Kinetic runs `module.sh install` after copying it in (if it fails, nothing of the module is kept) and `module.sh uninstall` before removing it |

A module without a `module.json` still works, as an id and its summary line.

## module.sh

```bash
#!/bin/bash
# kinetic:summary=A clock drawn into the wallpaper
set -euo pipefail
source "$KINETIC_HOME/lib/kinetic.sh"

case "${1:-help}" in
  active)  [[ $(module_get on false) == true ]] && echo true || echo false ;;
  enable)  module_set on true ;;
  disable) module_set on false ;;
  install) ;;     # if "install" is in hooks
  uninstall) ;;   # if "uninstall" is in hooks
  help|*)  echo "Usage: kinetic aurora-clock enable|disable|active" ;;
esac
```

Every module answers three commands, which the app's switch and dependency
handling use:

| Command | |
|---|---|
| `active` | Prints `true` or `false`: is it on right now |
| `enable` | Switches it on. Kinetic switches on what it `requires` first |
| `disable` | Switches it off. Kinetic asks before, and switches off, the modules that require it |

Everything else is yours. Kinetic sets `KINETIC_HOME`, `KINETIC_MODULE` (your
id) and `KINETIC_MODULE_DIR` (your folder) before running it.

## What Kinetic gives a module

`source "$KINETIC_HOME/lib/kinetic.sh"`:

**Facts.** One snapshot of the desktop, made once per `kinetic` command and
shared by every module it runs, so modules don't each ask the engine and read
the same files. `fact '<jq path>'` reads it; after you change something,
`facts_refresh`.

| Fact | |
|---|---|
| `.engine.running` | The wallpaper engine is up |
| `.engine.on` | A live wallpaper is showing (`.engine.still`: Omarchy's background, held for the glass) |
| `.engine.wallpaper` | The wallpaper showing |
| `.engine.options.<key>` | Any engine option (`kinetic-wall config` lists them) |
| `.bar.id`, `.bar.kinetic`, `.bar.background`, `.bar.pill_on`, `.bar.pill.<key>` | The bar |
| `.motion.on` | Kinetic's animations are on |
| `.hyprland.running` | Running under Hyprland |
| `.kinetic.version`, `.kinetic.home` | Kinetic itself |

`kinetic status` prints the whole snapshot.

**Helpers.**

| Helper | |
|---|---|
| `engine_running`, `engine_option <key>`, `engine_set <key> <value>` | The wallpaper engine's options (set checks the value and refreshes the facts) |
| `bar_is_kinetic`, `pill_on`, `bar_option <key>` | The bar |
| `module_active <id>` | Whether another module is on |
| `module_dir <id>` | Another module's folder |
| `module_get <key> [default]`, `module_set <key> <json>` | Your own settings, in `~/.config/kinetic/<id>.json` |
| `hypr_toggle_write <name> <lua>` | One of Omarchy's Hyprland toggle files (loaded after the user's config). Checked with Hyprland's `--verify-config` first, written only if it changed, then Hyprland reloads. Name it `zz-<id>...` to load after Omarchy's own |
| `hypr_toggle_remove <name>`, `hypr_reload` | |
| `confirm <question>` | Yes or no on the terminal; `--yes` answers it, and without a terminal the answer is no |
| `notify <title> [body]` | A desktop notification |
| `die <message>`, `need <command>...` | Stop with a message; check tools are there |
| `wall_bin`, `shell_json`, `shell_config` | The engine binary; Omarchy's shell.json; Omarchy's helper for changing it |
| `KINETIC_CONFIG`, `KINETIC_STATE`, `KINETIC_BIN` | Kinetic's folders |

## A page in the app

```qml
// app/Page.qml
import QtQuick
import qs.Commons
import qs.Ui
import "../kinetic-ui/components"   // Kinetic's components (Kinetic links kinetic-ui into your folder)

Column {
  property var ctx: null   // the app's state and commands
  spacing: Style.space(10)
  PanelSectionHeader { text: "Clock"; foreground: ctx.fg; fontFamily: ctx.fontFamily }
  Button { text: "Switch off"; bordered: true; foreground: ctx.fg; onClicked: ctx.setModule("aurora-clock", false) }
}
```

Pages run in Omarchy's shell, with Omarchy's `qs.Commons` and `qs.Ui`, and get
`ctx`:

| `ctx` | |
|---|---|
| `fg`, `accent`, `fontFamily`, `tr(key)` | Colors and font, matching the app |
| `send(module, args)` | Runs `kinetic <module> <args...>` (queued, one at a time) |
| `setModule(id, on)` | Switches a module |
| `engineOption(key)` | An engine option, with its label, range and value: give it to a `SettingRow` (`kind: "config"`) for a ready-made row |
| `info`, `modules`, `moduleInfo(id)`, `moduleActive(id)` | What the engine describes, and every module |
| `refresh()`, `refreshModules()` | Read them again |

`kinetic-ui/components` has `SettingRow`, `WallHero` and the rest of the app's own.

## Publishing

Put the module in a GitHub repository, at the top or in a subfolder, and share
the link: `https://github.com/you/repo` or `https://github.com/you/repo/tree/main/sub`.
GitLab and Codeberg links work too, and any https git link ending in `.git`.
Modules that need yours can list it in their `requires` with that link as the
`source`. Tag versions in `module.json` as you go: `requires` can ask for one.

Keep a module to its own folder: no links (symlinks), under 4000 files and
64 MB. Kinetic refuses archives with paths that leave the folder.

## Trust

A module is code that runs as you: its `module.sh`, its install step and its
pages. Kinetic shows what it is about to install and asks first, but it cannot
tell a good module from a bad one. Install modules from people you trust, and
read them first when you can (`kinetic module inspect <link>` fetches and
shows one without installing it).
