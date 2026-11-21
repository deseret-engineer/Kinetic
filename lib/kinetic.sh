# shellcheck shell=bash disable=SC2034 # the paths below are for the modules that source this
# Kinetic's toolkit for shell modules. Source it; don't run it.
#
#   source "$KINETIC_HOME/lib/kinetic.sh"
#
# What it gives a module (MODULES.md has the full reference):
#   paths       KINETIC_HOME KINETIC_CONFIG KINETIC_STATE KINETIC_BIN KINETIC_MODULE KINETIC_MODULE_DIR
#   facts       one snapshot of the desktop per `kinetic` command, shared by every module it runs:
#               fact '.engine.running', fact '.bar.pill_on', facts_refresh after a change
#   engine      engine_running, engine_option <key>, engine_set <key> <value>, wall_bin
#   bar         bar_is_kinetic, pill_on, bar_option <key>, shell_json, shell_config
#   modules     module_active <id>, module_dir <id>, module_get/module_set (your own settings)
#   Hyprland    hypr_toggle_write <name> <lua>, hypr_toggle_remove <name>, hypr_reload
#   people      confirm <question>, notify <title> [body], die, need
#
# KINETIC_HOME is set by the `kinetic` dispatcher (the install or the repo).

: "${KINETIC_HOME:=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
KINETIC_VERSION=0.1.0
KINETIC_BIN="${KINETIC_BIN:-$HOME/.local/bin}"
KINETIC_CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}/kinetic"
KINETIC_STATE="${XDG_STATE_HOME:-$HOME/.local/state}/kinetic"
# modules you add (kinetic module install) live apart from Kinetic, so reinstalling keeps them
KINETIC_MODULES_USER="${KINETIC_MODULES_USER:-${XDG_DATA_HOME:-$HOME/.local/share}/kinetic-modules}"
SHELL_JSON="$HOME/.config/omarchy/shell.json"
HYPR_TOGGLES="${XDG_STATE_HOME:-$HOME/.local/state}/omarchy/toggles/hypr"

die() {
  echo "kinetic${KINETIC_MODULE:+ $KINETIC_MODULE}: $*" >&2
  exit 1
}

need() {
  local c
  local c pkg
  for c in "$@"; do
    [[ $c == python3 ]] && pkg=python || pkg=$c # the Arch package, where its name differs
    command -v "$c" >/dev/null || die "needs '$c' (install it with: omarchy pkg add $pkg)"
  done
}

# ---------- the wallpaper engine ----------

# The engine binary: the installed one, else a development build in the repo.
wall_bin() {
  if [[ -x $KINETIC_BIN/kinetic-wall ]]; then
    printf '%s\n' "$KINETIC_BIN/kinetic-wall"
  elif [[ -x $KINETIC_HOME/modules/wall/build/kinetic-wall ]]; then
    printf '%s\n' "$KINETIC_HOME/modules/wall/build/kinetic-wall"
  elif command -v kinetic-wall >/dev/null; then
    command -v kinetic-wall
  else
    die "the wallpaper engine is not built (run install.sh)"
  fi
}

# ---------- Omarchy's shell.json ----------

# shell.json, through Omarchy's own helper so the running shell reloads it.
shell_config() {
  # shellcheck disable=SC1091
  source omarchy-shell-config
}

shell_json() {
  if [[ -s $SHELL_JSON ]]; then cat "$SHELL_JSON"; else cat "${OMARCHY_PATH:-/usr/share/omarchy}/config/omarchy/shell.json"; fi
}

# ---------- facts: what every module would otherwise each look up ----------
# One snapshot per `kinetic` command: the dispatcher hands every module it runs
# (and every nested `kinetic`) the same file in KINETIC_FACTS, filled on first use.
# After changing something, facts_refresh so the next read sees it.

_facts_compute() {
  local describe='null' motion='{}'
  if [[ -S ${KINETIC_WALL_SOCKET:-${XDG_RUNTIME_DIR:-/run/user/$UID}/kinetic/wall.sock} ]]; then
    describe=$("$(wall_bin)" describe 2>/dev/null) || describe='null'
    [[ -n $describe ]] || describe='null'
  fi
  [[ -s $KINETIC_CONFIG/motion.json ]] && motion=$(cat "$KINETIC_CONFIG/motion.json")
  shell_json | jq -c --argjson d "$describe" --argjson motion "$motion" \
    --arg version "$KINETIC_VERSION" --arg home "$KINETIC_HOME" \
    --arg hypr "${HYPRLAND_INSTANCE_SIGNATURE:-}" '
    {
      kinetic: {version: $version, home: $home},
      engine: {
        running: ($d != null),
        on: ($d != null and $d.enabled == true),
        still: ($d != null and $d.still == true),
        wallpaper: (if $d != null then $d.current else null end),
        options: (if $d != null then ($d.config | map({(.key): .value}) | add) else {} end)
      },
      bar: {
        id: (.bar.id // "omarchy.bar"),
        kinetic: ((.bar.id // "omarchy.bar") == "kinetic.bar"),
        background: (.bar.background // "opaque"),
        pill_on: ((.bar.id // "") == "kinetic.bar" and .bar.background == "pill"),
        pill: (.bar.pill // {})
      },
      motion: {on: ($motion.enabled == true)},
      hyprland: {running: ($hypr != "")}
    }' 2>/dev/null || echo '{}'
}

facts() {
  if [[ -n ${KINETIC_FACTS:-} ]]; then
    # written whole and moved into place, so a module running alongside never reads half of it
    [[ -s $KINETIC_FACTS ]] || { _facts_compute > "$KINETIC_FACTS.$$" && mv -f "$KINETIC_FACTS.$$" "$KINETIC_FACTS"; }
    cat "$KINETIC_FACTS"
  else
    _facts_compute
  fi
}

# fact '.engine.options.window_glass' -> its value (raw), or empty
fact() { facts | jq -r "($1) // empty" 2>/dev/null; }

facts_refresh() { [[ -n ${KINETIC_FACTS:-} ]] && : > "$KINETIC_FACTS"; return 0; }

engine_running() { [[ $(fact '.engine.running') == true ]]; }
engine_option() { fact ".engine.options[\"$1\"]"; }
engine_set() {
  engine_running || die "the wallpaper engine is not running (kinetic wall enable)"
  local out
  out=$("$(wall_bin)" config "$1" "$2") || die "could not set $1"
  [[ $out == error* ]] && die "${out#error: }"
  facts_refresh
}

bar_is_kinetic() { [[ $(fact '.bar.kinetic') == true ]]; }
pill_on() { [[ $(fact '.bar.pill_on') == true ]]; }
bar_option() { fact ".bar.pill[\"$1\"]"; }

# ---------- modules ----------

# A module's folder: Kinetic's own first, then the ones you added.
module_dir() {
  [[ $1 =~ ^[a-z][a-z0-9-]*$ ]] || return 1
  if [[ -x $KINETIC_HOME/modules/$1/module.sh ]]; then printf '%s\n' "$KINETIC_HOME/modules/$1"
  elif [[ -x $KINETIC_MODULES_USER/$1/module.sh ]]; then printf '%s\n' "$KINETIC_MODULES_USER/$1"
  else return 1; fi
}

# Whether another module is on (asks it, sharing this command's facts).
module_active() {
  local d
  d=$(module_dir "$1") || return 1
  [[ $(KINETIC_MODULE=$1 KINETIC_MODULE_DIR=$d "$d/module.sh" active 2>/dev/null) == true ]]
}

# Your module's own settings: ~/.config/kinetic/<module>.json
#   module_get <key> [default]      module_set <key> <json value>     e.g. module_set speed 2, module_set name '"x"'
_module_file() { printf '%s/%s.json\n' "$KINETIC_CONFIG" "${KINETIC_MODULE:?module_get/module_set need KINETIC_MODULE}"; }
module_get() {
  local f v
  f=$(_module_file)
  v=$(jq -r --arg k "$1" '.[$k] // empty' "$f" 2>/dev/null) || true
  printf '%s\n' "${v:-${2:-}}"
}
module_set() {
  local f
  f=$(_module_file)
  mkdir -p "$KINETIC_CONFIG"
  [[ -s $f ]] || echo '{}' > "$f"
  jq --arg k "$1" --argjson v "$2" '.[$k] = $v' "$f" > "$f.tmp" && mv "$f.tmp" "$f"
}

# ---------- Hyprland ----------

hypr_reload() {
  if command -v omarchy-restart-hyprctl >/dev/null; then omarchy-restart-hyprctl >/dev/null 2>&1 || true
  else hyprctl reload >/dev/null 2>&1 || true; fi
}

# Writes one of Omarchy's Hyprland toggle files (loaded after your config) if it
# changed: checked with Hyprland's own --verify-config first, then Hyprland reloads.
#   hypr_toggle_write zz-mymodule "hl.window_rule({ ... })"
# Returns 0 if it was written, 1 if it was already so.
hypr_toggle_write() {
  local name=$1 lua=$2 file
  [[ $name =~ ^[a-z0-9][a-z0-9_.-]*$ ]] || die "bad toggle name '$name'"
  file="$HYPR_TOGGLES/$name.lua"
  [[ -f $file && $(cat "$file") == "$lua" ]] && return 1
  if command -v Hyprland >/dev/null && [[ -z ${KINETIC_NO_VERIFY:-} ]]; then
    local tmp out
    tmp=$(mktemp --suffix=.lua)
    printf '%s\n' "$lua" > "$tmp"
    out=$(Hyprland --verify-config -c "$tmp" 2>&1 | tail -1)
    rm -f "$tmp"
    [[ $out == *"config ok"* ]] || die "Hyprland would not accept $name.lua: ${out//$'\e'/}"
  fi
  mkdir -p "$HYPR_TOGGLES"
  printf '%s\n' "$lua" > "$file.tmp" && mv "$file.tmp" "$file"
  hypr_reload
  return 0
}

# Removes a toggle file; Hyprland reloads if there was one. Returns 1 if there wasn't.
hypr_toggle_remove() {
  local file="$HYPR_TOGGLES/$1.lua"
  [[ -f $file ]] || return 1
  rm -f "$file"
  hypr_reload
}

# ---------- people ----------

# Asks yes or no on the terminal. Without one, only KINETIC_YES=1 (kinetic ... --yes) answers yes.
confirm() {
  [[ ${KINETIC_YES:-} == 1 ]] && return 0
  if [[ -t 0 && -t 2 ]]; then
    local a
    read -r -p "$1 [y/N] " a </dev/tty >&2 || return 1
    [[ $a =~ ^[Yy]([Ee][Ss])?$ ]]
  else
    echo "kinetic${KINETIC_MODULE:+ $KINETIC_MODULE}: $1 (no terminal to ask: add --yes)" >&2
    return 1
  fi
}

notify() { command -v notify-send >/dev/null && notify-send -a Kinetic "$1" "${2:-}" 2>/dev/null || true; }
