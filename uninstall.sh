#!/bin/bash
# Remove Kinetic. Your settings stay (~/.config/kinetic, ~/.local/state/kinetic)
# unless you pass --purge. The bar goes back to Omarchy's own.
set -euo pipefail
cd "$(dirname "$0")"
PURGE=0
[[ ${1:-} == --purge ]] && PURGE=1

BIN="$HOME/.local/bin"
PLUGINS="$HOME/.config/omarchy/plugins"
MENU="$HOME/.config/omarchy/extensions/omarchy-menu.jsonc"

systemctl --user disable --now kinetic-wall.service 2>/dev/null || true
rm -f "$HOME/.config/systemd/user/kinetic-wall.service"
systemctl --user daemon-reload

if [[ -f $HOME/.config/omarchy/shell.json ]]; then
  # shellcheck disable=SC1091
  source omarchy-shell-config
  commit "$NORMALIZE
    | if .bar.id == \"kinetic.bar\" then del(.bar.id) else . end
    | if .bar.background == \"pill\" then .bar.background = \"glassy\" else . end
    | .bar.layout |= with_entries(.value |= (if type == \"array\" then map(select(.id != \"kinetic\")) else . end))
    | .plugins = [(.plugins // [])[] | select(.id != \"kinetic.app\")]"
  omarchy-bar sync-glass >/dev/null 2>&1 || true
fi
"$BIN/kinetic-bar-blur" off >/dev/null 2>&1 || true
# Kinetic's animations: back to Omarchy's own; the app's window rule, Hyprland's
# blur back, and the Hyprland plugin (window glass and the light) unloaded
rm -f "${XDG_STATE_HOME:-$HOME/.local/state}/omarchy/toggles/hypr/kinetic-motion.lua" \
      "${XDG_STATE_HOME:-$HOME/.local/state}/omarchy/toggles/hypr/kinetic-app.lua" \
      "${XDG_STATE_HOME:-$HOME/.local/state}/omarchy/toggles/hypr/zz-kinetic-windows.lua" \
      "${XDG_STATE_HOME:-$HOME/.local/state}/omarchy/toggles/hypr/kinetic-plugin.lua"
omarchy-restart-hyprctl >/dev/null 2>&1 || true
hyprctl plugin unload "$HOME/.local/lib/kinetic/kinetic-glass.so" >/dev/null 2>&1 || true
rm -rf "$HOME/.local/lib/kinetic"
rm -rf "$PLUGINS/kinetic" "$PLUGINS/kinetic.bar" "$PLUGINS/kinetic.app"
rm -f "$HOME/.local/share/applications/kinetic.desktop"
[[ -f $MENU ]] && python3 integration/menu.py remove "$MENU"
rm -f "$HOME/.config/omarchy/hooks/post-update.d/kinetic-bar"
rm -f "$BIN/kinetic" "$BIN/kinetic-wall" "$BIN/kinetic-wall-pick" "$BIN/kinetic-bar-blur" "$BIN/kinetic-window-blur"
rm -rf "$HOME/.local/share/kinetic/lib" "$HOME/.local/share/kinetic/modules" "$HOME/.local/share/kinetic/shell" \
       "$HOME/.local/share/kinetic/wallpapers" "$HOME/.local/share/kinetic/integration" "$HOME/.local/share/kinetic/kinetic" \
       "$HOME/.local/share/kinetic/install.json" "$HOME/.local/share/kinetic/THIRD-PARTY.md"
rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/kinetic"
if (( PURGE )); then
  rm -rf "${XDG_CONFIG_HOME:-$HOME/.config}/kinetic" "${XDG_STATE_HOME:-$HOME/.local/state}/kinetic"
  rm -rf "${XDG_DATA_HOME:-$HOME/.local/share}/kinetic-modules"
fi
omarchy-shell -q shell rescanPlugins >/dev/null 2>&1 || true
echo "Kinetic is removed.$([[ $PURGE == 1 ]] || echo " Settings kept in ~/.config/kinetic and ~/.local/state/kinetic, modules you added in ~/.local/share/kinetic-modules (--purge removes them).")"
exit 0
