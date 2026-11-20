#!/bin/bash
# kinetic:summary=Living wallpapers: GLSL scenes drawn by a native engine
# The engine is its own binary (kinetic-wall); this forwards to it, apart from
# switching the module itself: the engine's service.
set -euo pipefail
source "$KINETIC_HOME/lib/kinetic.sh"
case "${1:-}" in
  active) systemctl --user is-active --quiet kinetic-wall.service && echo true || echo false; exit 0 ;;
  enable) systemctl --user enable --now kinetic-wall.service >/dev/null 2>&1 || die "could not start kinetic-wall.service"; exit 0 ;;
  disable) # Omarchy's background comes back; glass, light and parallax need the engine
    systemctl --user disable --now kinetic-wall.service >/dev/null 2>&1 || true; exit 0 ;;
esac
exec "$(wall_bin)" "$@"
