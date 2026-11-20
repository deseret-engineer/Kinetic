#!/bin/bash
# kinetic:summary=The pill bar: a floating glass capsule over Omarchy's own bar
set -euo pipefail
source "$KINETIC_HOME/lib/kinetic.sh"
HERE="$KINETIC_HOME/modules/bar"
SCHEMA="$HERE/schema.json"
need jq

usage() {
  cat <<USAGE
Usage: kinetic bar <command> [args]

  on [style]              Use the pill background (optionally with a style)
  off [opaque|glassy|none]
                          Back to one of Omarchy's backgrounds (default: glassy)
  style <name>            $(jq -r '.options[] | select(.key == "style") | .options | join(" | ")' "$SCHEMA")
  shape <bar|islands>     One capsule, or one per group of widgets
  set <key> <value>       Set an option (checked against the schema)
  unset <key>             Back to its default
  get [key]               The options you have set (all, or one)
  options                 Every option with its type, range and default
  reset                   Clear every option
  preset <name>           $(jq -r '.presets | keys | join(" | ")' "$SCHEMA")
  build [--check]         Rebuild the kinetic.bar plugin from Omarchy's stock bar
  use                     Make kinetic.bar the active bar (builds it if needed)
  ui                      Open the Bar tab of the Kinetic panel

Options live under bar.pill in ~/.config/omarchy/shell.json and apply live.
USAGE
}

# A value checked and converted by the schema; prints it as JSON.
checked() {
  jq -cn --slurpfile s "$SCHEMA" --arg k "$1" --arg v "$2" '
    ([$s[0].options[] | select(.key == $k)] | first) as $o
    | if $o == null then error("unknown option: \($k) (see: kinetic bar options)") else . end
    | def clampn: if . < $o.min then $o.min elif . > $o.max then $o.max else . end;
      if $o.type == "choice" then
        if ($o.options | index($v)) != null then $v else error("\($k) is one of: \($o.options | join(", "))") end
      elif $o.type == "bool" then
        if $v == "true" or $v == "on" then true elif $v == "false" or $v == "off" then false
        else error("\($k) is true or false") end
      elif $o.type == "color" then
        if ($o.options | index($v)) != null then $v
        elif ($v | test("^#?([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$")) then "#" + ($v | ltrimstr("#") | ascii_downcase)
        else error("\($k) is \($o.options | join(", ")) or a color like #7aa2f7") end
      elif $o.special != null and $v == $o.special then $v
      else ($v | tonumber? // error("\($k) takes a number from \($o.min) to \($o.max)"))
        | clampn | if $o.type == "int" then round else . end
      end' 2>&1 | sed 's/^jq: error (at <unknown>): //'
}

set_key() {
  local v
  v=$(checked "$1" "$2") || die "$v"
  shell_config
  commit "$NORMALIZE | .bar.pill = ((.bar.pill // {}) + {(\$k): \$v})" --arg k "$1" --argjson v "$v"
}

active_bar_is_kinetic() { [[ $(shell_json | jq -r '.bar.id // "omarchy.bar"') == "kinetic.bar" ]]; }

build() {
  need python3
  python3 "$HERE/build.py" "$@"
  [[ " $* " == *" --check "* ]] && return
  active_bar_is_kinetic && { omarchy-shell -q shell rescanPlugins >/dev/null 2>&1 || true; }
  return 0
}

use() {
  [[ -f $HOME/.config/omarchy/plugins/kinetic.bar/Bar.qml ]] || build
  omarchy-shell -q shell rescanPlugins >/dev/null 2>&1 || true
  omarchy-bar use kinetic.bar
}

cmd="${1:-help}"
shift || true
case "$cmd" in
  on)
    active_bar_is_kinetic || use
    shell_config
    commit "$NORMALIZE | .bar.background = \"pill\" | del(.bar.transparent)"
    [[ -z ${1:-} ]] || set_key style "$1"
    ;;
  off)
    bg="${1:-glassy}"
    [[ $bg =~ ^(opaque|glassy|none)$ ]] || die "off takes opaque, glassy or none"
    omarchy-bar background "$bg"
    ;;
  style) set_key style "${1:?style name}" ;;
  shape) set_key shape "${1:?bar or islands}" ;;
  set) set_key "${1:?key}" "${2:?value}" ;;
  unset)
    jq -e --arg k "${1:?key}" '.options | any(.key == $k)' "$SCHEMA" >/dev/null || die "unknown option: $1"
    shell_config
    commit "$NORMALIZE | .bar.pill = ((.bar.pill // {}) | del(.[\$k]))" --arg k "$1"
    ;;
  get)
    if [[ -n ${1:-} ]]; then shell_json | jq --arg k "$1" '.bar.pill[$k]'
    else shell_json | jq '{background: (.bar.background // "opaque"), pill: (.bar.pill // {})}'; fi
    ;;
  options)
    jq -r '.options[] | [.key, .type,
      (if .type == "choice" or .type == "color" then (.options | join("|"))
       elif .type == "bool" then "true|false" else "\(.min)..\(.max)" end),
      (.default | if type == "object" then (if .by then "by \(.by)" else "from \(.from)" end) else tostring end),
      (.description // .label)] | @tsv' "$SCHEMA" | column -t -s $'\t'
    ;;
  reset) shell_config; commit "$NORMALIZE | del(.bar.pill)" ;;
  preset)
    p=$(jq -ce --arg n "${1:?preset name}" '.presets[$n]' "$SCHEMA") || die "no preset named '$1'"
    active_bar_is_kinetic || use
    shell_config
    commit "$NORMALIZE | .bar.background = \"pill\" | del(.bar.transparent) | .bar.pill = \$p" --argjson p "$p"
    ;;
  presets) jq -r '.presets | keys[]' "$SCHEMA" ;;
  build) build "$@" ;;
  use) use ;;
  active) active_bar_is_kinetic && echo true || echo false ;;
  enable) "$KINETIC_HOME/kinetic" bar on ;;
  disable) # Omarchy's own bar; a pill background becomes glassy
    shell_config
    commit "$NORMALIZE | if .bar.background == \"pill\" then .bar.background = \"glassy\" else . end"
    omarchy-bar use omarchy.bar
    "$HERE/blur" off >/dev/null 2>&1 || true
    ;;
  ui) exec "$KINETIC_HOME/kinetic" panel bar ;;
  blur) exec "$HERE/blur" "$@" ;;
  help|-h|--help) usage ;;
  *) usage >&2; exit 2 ;;
esac
