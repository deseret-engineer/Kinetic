#!/bin/bash
# kinetic:summary=Motion: window and workspace animations, with wallpaper parallax
set -euo pipefail
source "$KINETIC_HOME/lib/kinetic.sh"
# `active` is asked often (the app's module list): answered here, without starting Python.
# The same rule as motion.py's read_config: on unless motion.json is missing, bad or says enabled: false.
if [[ ${1:-} == active ]]; then
  jq -e 'type == "object" and .enabled != false' "$KINETIC_CONFIG/motion.json" >/dev/null 2>&1 && echo true || echo false
  exit 0
fi
need python3
exec python3 "$KINETIC_HOME/modules/motion/motion.py" "$@"
