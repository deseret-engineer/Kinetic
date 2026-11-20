#!/bin/bash
# kinetic:summary=Rendered glass: the wallpaper bent behind the bar and windows
# The glass lives in two places: the bar's options (shell.json: glass, refraction,
# frost ...) and the wallpaper engine's (wall.toml: window_glass, window_refract ...).
# This switches both together and sets one look on both. The glass behind windows
# is drawn inside Hyprland by Kinetic's plugin (plugin/), which the engine sends
# those options to; `kinetic glass plugin` builds and loads it.
set -euo pipefail
source "$KINETIC_HOME/lib/kinetic.sh"
need jq

usage() {
  cat <<USAGE
Usage: kinetic glass <command> [args]

  enable                  Glass under the bar and behind see-through windows
  disable                 None: the bar uses Hyprland's blur, windows have none
  active                  true or false
  windows <off|see-through|floating|all>
                          Which windows get glass (see-through: the ones you can see through)
  apps <word ...|none>    Apps see-through on their own config, which Hyprland can't tell:
                          classes (a trailing * matches a prefix) or Hyprland tags (default: terminal)
  set <option> <value>    One look for the bar and the windows:
                          refraction frost dispersion curvature saturation brightness specular lightAngle
  plugin [status]         Kinetic's Hyprland plugin, which draws the window glass and the light
  plugin build            Build it for the installed Hyprland and load it (also after a Hyprland update)
  plugin remove           Unload it and stop loading it at login
USAGE
}

# the bar's option -> the engine's window option
declare -A WIN=([refraction]=window_refract [frost]=window_frost [dispersion]=window_dispersion
                [curvature]=window_curve [saturation]=window_saturation [brightness]=window_brightness
                [specular]=window_specular [lightAngle]=window_light)

bar_glass_on() { pill_on && [[ $(bar_option glass) =~ ^(auto|wallpaper|)$ ]]; }

# ---------- the Hyprland plugin ----------
PLUGIN_SO="${KINETIC_LIB:-$HOME/.local/lib/kinetic}/kinetic-glass.so"
PLUGIN_TOGGLE=kinetic-plugin # Omarchy's Hyprland toggles: loaded at every login, and on reload

running_commit() { hyprctl version -j 2>/dev/null | jq -r '.commit // empty'; }
plugin_loaded() { hyprctl plugin list 2>/dev/null | grep -q '^Plugin kinetic-glass '; }

plugin_status() {
  local built="" running
  [[ -f $PLUGIN_SO.hyprland ]] && built=$(<"$PLUGIN_SO.hyprland")
  running=$(running_commit)
  if [[ ! -f $PLUGIN_SO ]]; then echo "not built (kinetic glass plugin build)"
  elif plugin_loaded; then echo "loaded: $(hyprctl kinetic-glass status 2>/dev/null)"
  elif [[ -n $running && $built != "$running" ]]; then echo "built for another Hyprland ($built, running $running): kinetic glass plugin build"
  else echo "built, not loaded (kinetic glass plugin build loads it)"
  fi
}

plugin_build() {
  local out
  out=$("${KINETIC_MODULE_DIR:-$KINETIC_HOME/modules/glass}/plugin/build.sh" "$PLUGIN_SO" 2>&1) || { echo "$out" >&2; die "the plugin did not build: window glass and the light stay off"; }
  # the toggle loads it at every login (and Hyprland loads it now, on the reload)
  hypr_toggle_write "$PLUGIN_TOGGLE" "-- Kinetic's Hyprland plugin: glass behind windows and the selection light.
-- Written by kinetic glass plugin build; do not edit.
hl.plugin.load(\"$PLUGIN_SO\")" || true
  [[ -n ${HYPRLAND_INSTANCE_SIGNATURE:-} ]] || return 0
  # a Hyprland updated but not restarted keeps the old one until the next login
  if [[ $(running_commit) != "$(<"$PLUGIN_SO.hyprland")" ]]; then
    echo "built for the Hyprland just installed: it loads at the next login"
    return 0
  fi
  if plugin_loaded; then hyprctl plugin unload "$PLUGIN_SO" >/dev/null 2>&1 || true; fi
  out=$(hyprctl plugin load "$PLUGIN_SO" 2>&1)
  [[ $out == ok* ]] || die "Hyprland would not load the plugin: $out"
  echo "loaded"
}

cmd="${1:-help}"
shift || true
case "$cmd" in
  active)
    w=$(engine_option window_glass)
    if bar_glass_on || [[ -n $w && $w != off ]]; then echo true; else echo false; fi
    ;;
  enable)
    if pill_on && ! bar_glass_on; then "$KINETIC_HOME/kinetic" bar set glass auto; fi
    [[ $(engine_option window_glass) == off ]] && engine_set window_glass see-through
    engine_set glass_still 1
    ;;
  disable)
    if bar_glass_on; then "$KINETIC_HOME/kinetic" bar set glass blur; fi
    if engine_running; then engine_set window_glass off; engine_set glass_still 0; fi
    ;;
  windows)
    [[ ${1:-} =~ ^(off|see-through|floating|all)$ ]] || die "windows takes off, see-through, floating or all"
    engine_set window_glass "$1"
    ;;
  apps)
    (( $# )) || die "apps takes classes or tags (or none)"
    engine_set window_glass_apps "$*"
    ;;
  set)
    k="${1:?option}" v="${2:?value}"
    [[ -n ${WIN[$k]:-} ]] || die "no look option named '$k' (${!WIN[*]})"
    engine_set "${WIN[$k]}" "$v"
    "$KINETIC_HOME/kinetic" bar set "$k" "$v"
    ;;
  plugin)
    case "${1:-status}" in
      status) plugin_status ;;
      build|rebuild) plugin_build ;;
      remove)
        hypr_toggle_remove "$PLUGIN_TOGGLE" || true
        if [[ -n ${HYPRLAND_INSTANCE_SIGNATURE:-} ]] && plugin_loaded; then hyprctl plugin unload "$PLUGIN_SO" >/dev/null 2>&1 || true; fi
        echo "removed: window glass and the light are off until it is built again"
        ;;
      *) die "plugin takes status, build or remove" ;;
    esac
    ;;
  help|-h|--help) usage ;;
  *) usage >&2; exit 2 ;;
esac
