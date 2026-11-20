#!/bin/bash
# kinetic:summary=Motion: window and workspace animations, with wallpaper parallax
set -euo pipefail
source "$KINETIC_HOME/lib/kinetic.sh"
need python3
exec python3 "$KINETIC_HOME/modules/motion/motion.py" "$@"
