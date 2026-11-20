#!/bin/bash
# kinetic:summary=Presets: a wallpaper, a bar and motion saved and applied together
set -euo pipefail
source "$KINETIC_HOME/lib/kinetic.sh"
need jq
BUNDLED="$KINETIC_HOME/modules/preset/presets"
USER_PRESETS="$KINETIC_CONFIG/presets"
SCHEMA="$KINETIC_HOME/modules/bar/schema.json"

usage() {
  cat <<USAGE
Usage: kinetic preset <command> [args]

  list [--json]           Every preset: yours ($USER_PRESETS) and the bundled ones
  show <id>               A preset's file
  apply <id>              Switch the wallpaper, the bar and motion to it
  save <id> [description] Save what is showing now as a preset of your own
  remove <id>             Delete one of your own presets
  current                 The preset last applied

A preset file: {"name", "description", "wall": {"wallpaper", "preset", "params",
"enabled"}, "bar": {"background", "preset" | "pill"}, "motion": {"preset" |
"options", "enabled"}, "theme"}. Every part is optional; only what a preset
names is changed.
USAGE
}

valid_id() { [[ $1 =~ ^[a-z0-9][a-z0-9-]{0,47}$ ]] || die "a preset id is lowercase letters, digits and dashes"; }

preset_file() {
  valid_id "$1"
  if [[ -f $USER_PRESETS/$1.json ]]; then printf '%s\n' "$USER_PRESETS/$1.json"
  elif [[ -f $BUNDLED/$1.json ]]; then printf '%s\n' "$BUNDLED/$1.json"
  else die "no preset named '$1' (see: kinetic preset list)"; fi
}

list_json() {
  local f id seen=" " out=()
  for f in "$USER_PRESETS"/*.json "$BUNDLED"/*.json; do
    [[ -f $f ]] || continue
    id=$(basename "$f" .json)
    [[ $seen == *" $id "* ]] && continue
    seen+="$id "
    out+=("$(jq -c --arg id "$id" --argjson mine "$([[ $f == "$USER_PRESETS"/* ]] && echo true || echo false)" \
      '{id: $id, name: (.name // $id), description: (.description // ""), mine: $mine,
        wallpaper: (if .wall.enabled == false then null else .wall.wallpaper end), bar: (.bar.preset // .bar.pill.style // .bar.background)}' "$f" 2>/dev/null)") || true
  done
  printf '%s\n' "${out[@]}" | jq -cs 'map(select(. != null))'
}

apply() {
  local f wall
  f=$(preset_file "$1")
  jq -e 'type == "object"' "$f" >/dev/null || die "$f is not a preset"
  wall=$(wall_bin)

  # wallpaper
  if [[ $(jq -r '.wall.enabled' "$f") == "false" ]]; then
    "$wall" off >/dev/null
  elif jq -e '.wall.wallpaper' "$f" >/dev/null; then
    "$wall" set "$(jq -r '.wall.wallpaper' "$f")" >/dev/null
    "$wall" param-reset >/dev/null
    if jq -e '.wall.preset' "$f" >/dev/null; then "$wall" preset "$(jq -r '.wall.preset' "$f")" >/dev/null; fi
    jq -r '.wall.params // {} | to_entries[] | "\(.key)\u001f\(.value)"' "$f" | while IFS=$'\x1f' read -r k v; do
      "$wall" param "$k" "$v" >/dev/null || echo "kinetic preset: skipped parameter $k" >&2
    done
  fi

  # bar
  if jq -e '.bar' "$f" >/dev/null; then
    local bg pill
    bg=$(jq -r '.bar.background // "pill"' "$f")
    [[ $bg =~ ^(pill|opaque|glassy|none)$ ]] || die "bar.background must be pill, opaque, glassy or none"
    if jq -e '.bar.preset' "$f" >/dev/null; then
      pill=$(jq -c --slurpfile s "$SCHEMA" '$s[0].presets[.bar.preset] // error("no bar preset \(.bar.preset)")' "$f")
    else
      pill=$(jq -c '.bar.pill // null' "$f")
    fi
    if [[ $bg == pill ]]; then
      [[ $(shell_json | jq -r '.bar.id // ""') == kinetic.bar ]] || "$KINETIC_HOME/modules/bar/module.sh" use >/dev/null
    fi
    shell_config
    commit "$NORMALIZE | .bar.background = \$bg | del(.bar.transparent)
            | if \$pill != null then .bar.pill = \$pill else . end" --arg bg "$bg" --argjson pill "$pill"
    [[ $bg == pill ]] || omarchy-bar background "$bg" >/dev/null
  fi

  # motion: Hyprland animations and wallpaper parallax
  if jq -e '.motion' "$f" >/dev/null; then
    local motion="$KINETIC_HOME/modules/motion/module.sh"
    if [[ $(jq -r '.motion.enabled' "$f") == "false" ]]; then "$motion" off >/dev/null
    elif jq -e '.motion.preset' "$f" >/dev/null; then "$motion" preset "$(jq -r '.motion.preset' "$f")" >/dev/null
    else
      local mcfg="$KINETIC_CONFIG/motion.json"
      mkdir -p "$KINETIC_CONFIG"
      jq '{enabled: true, options: (.motion.options // {})}' "$f" >"$mcfg.tmp" && mv "$mcfg.tmp" "$mcfg"
      "$motion" apply >/dev/null
    fi
  fi

  # theme (optional)
  if jq -e '.theme' "$f" >/dev/null; then omarchy theme set "$(jq -r '.theme' "$f")"; fi

  mkdir -p "$KINETIC_STATE"
  printf '%s\n' "$1" >"$KINETIC_STATE/preset"
  echo "Applied preset: $(jq -r '.name // empty' "$f")"
}

save() {
  local id="$1" desc="${2:-}" wall describe out
  valid_id "$id"
  wall=$(wall_bin)
  describe=$("$wall" describe 2>/dev/null) || describe='{"enabled":false}'
  mkdir -p "$USER_PRESETS"
  out="$USER_PRESETS/$id.json"
  local motion='{"enabled":false}'
  [[ -f $KINETIC_CONFIG/motion.json ]] && motion=$(jq -c '{enabled: (.enabled != false), options: (.options // {})}' "$KINETIC_CONFIG/motion.json")
  shell_json | jq --argjson d "$describe" --argjson motion "$motion" --arg id "$id" --arg desc "$desc" '
    {name: ($id | gsub("-"; " ") | ascii_upcase[0:1] + .[1:]),
     description: $desc,
     wall: (if $d.enabled == false or $d.wallpaper == null then {enabled: false}
            else {wallpaper: $d.wallpaper.id,
                  params: ([$d.wallpaper.params[] | select((.value | tostring) != (.default | tostring)) | {key: .name, value: .value}] | from_entries)}
            end),
     bar: {background: (.bar.background // "opaque")} + (if .bar.background == "pill" then {pill: (.bar.pill // {})} else {} end),
     motion: (if $motion.enabled then {options: $motion.options} else {enabled: false} end)}' >"$out.tmp"
  mv "$out.tmp" "$out"
  echo "Saved $out"
}

cmd="${1:-help}"
shift || true
case "$cmd" in
  list)
    if [[ ${1:-} == --json ]]; then list_json
    else list_json | jq -r '.[] | [.id, .name, (if .mine then "yours" else "bundled" end), .description] | @tsv' | column -t -s $'\t'; fi
    ;;
  show) cat "$(preset_file "${1:?preset id}")" ;;
  apply) apply "${1:?preset id}" ;;
  save) save "${1:?preset id}" "${2:-}" ;;
  remove|rm)
    valid_id "${1:?preset id}"
    [[ -f $USER_PRESETS/$1.json ]] || die "'$1' is not one of your presets (bundled presets can't be removed)"
    rm -f "$USER_PRESETS/$1.json"
    echo "Removed $1"
    ;;
  current) cat "$KINETIC_STATE/preset" 2>/dev/null || echo "(none)" ;;
  # Presets run nothing, so switching the module off only hides it in the app
  active) jq -e '(.hidden // []) | index("preset") | not' "$KINETIC_CONFIG/modules.json" >/dev/null 2>&1 \
            || [[ ! -f $KINETIC_CONFIG/modules.json ]] && echo true || echo false ;;
  enable|disable)
    mkdir -p "$KINETIC_CONFIG"
    f="$KINETIC_CONFIG/modules.json"
    [[ -f $f ]] || echo '{}' > "$f"
    jq --arg on "$cmd" '.hidden = (((.hidden // []) - ["preset"]) + (if $on == "disable" then ["preset"] else [] end))' "$f" > "$f.tmp" && mv "$f.tmp" "$f"
    ;;
  help|-h|--help) usage ;;
  *) usage >&2; exit 2 ;;
esac
