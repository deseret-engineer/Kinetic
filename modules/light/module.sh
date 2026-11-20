#!/bin/bash
# kinetic:summary=Selection light: a soft glow around windows, under the glass
# The light is drawn inside Hyprland by Kinetic's plugin (modules/glass/plugin);
# its options are the engine's light_* settings (wall.toml), which the engine
# sends the plugin, and which this sets and reads by their short names.
set -euo pipefail
source "$KINETIC_HOME/lib/kinetic.sh"
need jq

usage() {
  cat <<USAGE
Usage: kinetic light <command> [args]

  enable [focused|all]    Light the focused window (default), or every window
  disable                 No light
  active                  true or false
  set <option> <value>    e.g. set color rainbow, set strength 0.3, set animation breathe
  get                     Every light option and its value
  options                 Every option with its range
USAGE
}

mode() { engine_option light_mode; }

cmd="${1:-help}"
shift || true
case "$cmd" in
  active) m=$(mode); [[ -n $m && $m != off ]] && echo true || echo false ;;
  enable)
    m="${1:-$(module_get last focused)}"
    [[ $m =~ ^(focused|all)$ ]] || die "enable takes focused or all"
    engine_set light_mode "$m"
    ;;
  disable)
    m=$(mode)
    [[ $m == focused || $m == all ]] && module_set last "\"$m\""   # enable brings back the same
    engine_running && engine_set light_mode off
    ;;
  set)
    k="${1:?option}" v="${2:?value}"
    [[ $k =~ ^[a-z_]+$ ]] || die "no option named '$k'"
    [[ $k == light_* ]] || k="light_$k"
    engine_set "$k" "$v"
    ;;
  get) facts | jq -r '.engine.options | to_entries[] | select(.key | startswith("light_")) | "\(.key | sub("^light_"; ""))\t\(.value)"' | column -t ;;
  options) "$(wall_bin)" config | grep -E '^\s*light_' | sed 's/^\(\s*\)light_/\1/' ;;
  help|-h|--help) usage ;;
  *) usage >&2; exit 2 ;;
esac
