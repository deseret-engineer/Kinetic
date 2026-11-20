#!/bin/bash
# Build Kinetic and install it for the current user. Nothing here needs root,
# and everything it changes outside ~/.local/share/kinetic is listed in the
# README's Uninstall section (uninstall.sh reverses it).
#
#   ./install.sh               build and install
#   ./install.sh --no-service  don't (re)start the wallpaper engine
set -euo pipefail
cd "$(dirname "$0")"

START_SERVICE=1
for a in "$@"; do
  case "$a" in
    --no-service) START_SERVICE=0 ;;
    -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
    *) echo "unknown option: $a" >&2; exit 2 ;;
  esac
done

HOME_DIR="$HOME/.local/share/kinetic"
BIN="$HOME/.local/bin"
PLUGINS="$HOME/.config/omarchy/plugins"
STATE="${XDG_STATE_HOME:-$HOME/.local/state}"
CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}"
MENU="$HOME/.config/omarchy/extensions/omarchy-menu.jsonc"
step() { printf '\033[1m==>\033[0m %s\n' "$*"; }

# ---------- dependencies ----------
# Omarchy ships everything Kinetic builds with, so on a stock install this finds
# nothing. If something was removed, it names the Arch packages to add back and
# installs nothing itself.
declare -A pkg_of=(
  [make]=make [jq]=jq [python3]=python [cc]=gcc [c++]=gcc [pkg-config]=pkgconf
  [wayland-scanner]=wayland [wayland-client]=wayland [wayland-egl]=wayland [wayland-protocols]=wayland-protocols
  [egl]=libglvnd [glesv2]=libglvnd [libpng]=libpng [libjpeg]=libjpeg-turbo [libwebp]=libwebp
)
missing=()
for c in make jq python3 cc c++ pkg-config wayland-scanner; do
  command -v "$c" >/dev/null || missing+=("${pkg_of[$c]}")
done
if command -v pkg-config >/dev/null; then
  for m in wayland-client wayland-egl wayland-protocols egl glesv2 libpng libjpeg libwebp; do
    pkg-config --exists "$m" || missing+=("${pkg_of[$m]}")
  done
fi
if (( ${#missing[@]} )); then
  mapfile -t missing < <(printf '%s\n' "${missing[@]}" | sort -u)
  echo "Kinetic needs ${missing[*]} to build. Install with:" >&2
  echo "  omarchy pkg add ${missing[*]}" >&2
  echo "then run ./install.sh again." >&2
  exit 1
fi

# ---------- build ----------
step "Building the wallpaper engine"
make -s -C modules/wall -j"$(nproc)" >/dev/null
step "Checking the bar hooks against this Omarchy's bar"
python3 modules/bar/build.py --check

# ---------- stop the running engine ----------
systemctl --user stop kinetic-wall.service 2>/dev/null || true
"$BIN/kinetic-wall" quit >/dev/null 2>&1 || true

# ---------- install files ----------
step "Installing to $HOME_DIR"
mkdir -p "$HOME_DIR" "$BIN"
rm -rf "${HOME_DIR:?}/lib" "${HOME_DIR:?}/modules" "${HOME_DIR:?}/shell" "${HOME_DIR:?}/wallpapers"
cp kinetic THIRD-PARTY.md "$HOME_DIR/"
cp -r lib shell "$HOME_DIR/"
# Where this install came from, for `kinetic update`: the repository's remote, branch and
# commit, and whether it had changes of its own (an install from a folder has none of these)
git_info() { git -C "$PWD" "$@" 2>/dev/null || true; }
jq -n --arg version "$(sed -n 's/^KINETIC_VERSION=//p' lib/kinetic.sh)" \
  --arg source "$(git_info remote get-url origin)" --arg branch "$(git_info rev-parse --abbrev-ref HEAD)" \
  --arg commit "$(git_info rev-parse HEAD)" --arg dirty "$(git_info status --porcelain --untracked-files=no | head -c1)" \
  '{version: $version, source: ($source | select(. != "") // null), branch: ($branch | select(. != "" and . != "HEAD") // null),
    commit: ($commit | select(. != "") // null), dirty: ($dirty != ""), installed: now | floor}' > "$HOME_DIR/install.json"
mkdir -p "$HOME_DIR/modules"
# every module but the wall, whose engine make installs (its scripts follow)
for m in modules/*/; do
  m=${m%/}
  [[ $m == modules/wall ]] || cp -r "$m" "$HOME_DIR/modules/"
done
mkdir -p "$HOME_DIR/modules/wall"
cp modules/wall/module.sh modules/wall/module.json "$HOME_DIR/modules/wall/"
cp -r modules/wall/integration "$HOME_DIR/modules/wall/"
cp -r integration "$HOME_DIR/"
find "$HOME_DIR" -name '__pycache__' -prune -exec rm -rf {} +
make -s -C modules/wall install PREFIX="$HOME/.local" >/dev/null   # kinetic-wall + wallpapers in ~/.local/share/kinetic/wallpapers
ln -sfn "$HOME_DIR/kinetic" "$BIN/kinetic"
ln -sfn "$HOME_DIR/modules/bar/blur" "$BIN/kinetic-bar-blur"
install -m755 modules/wall/integration/kinetic-wall-pick "$BIN/kinetic-wall-pick"
# beside kinetic-wall, which runs it; a link into the install, so it finds lib/kinetic.sh
rm -f "$BIN/kinetic-window-blur"
ln -s "$HOME_DIR/modules/wall/integration/window-blur" "$BIN/kinetic-window-blur"
install -Dm644 modules/wall/integration/kinetic-wall.service "$HOME/.config/systemd/user/kinetic-wall.service"

# ---------- config and state ----------
mkdir -p "$CONFIG/kinetic/wallpapers" "$CONFIG/kinetic/presets" "$STATE/kinetic/wall" "$STATE/kinetic/signals"
# Presets were called looks before 0.1.0: yours move over (one already there stays put)
if [[ -d $CONFIG/kinetic/looks ]]; then
  for f in "$CONFIG/kinetic/looks"/*.json; do
    [[ -f $f && ! -e $CONFIG/kinetic/presets/${f##*/} ]] && mv "$f" "$CONFIG/kinetic/presets/"
  done
  rmdir "$CONFIG/kinetic/looks" 2>/dev/null || true
fi
if [[ -f $STATE/kinetic/look && ! -e $STATE/kinetic/preset ]]; then mv "$STATE/kinetic/look" "$STATE/kinetic/preset"; fi
if [[ -f $CONFIG/kinetic/modules.json ]] && jq -e '(.hidden // []) | index("look")' "$CONFIG/kinetic/modules.json" >/dev/null 2>&1; then
  jq '.hidden = ((.hidden - ["look"]) + ["preset"] | unique)' "$CONFIG/kinetic/modules.json" > "$CONFIG/kinetic/modules.json.tmp" \
    && mv "$CONFIG/kinetic/modules.json.tmp" "$CONFIG/kinetic/modules.json"
fi
[[ -f $CONFIG/kinetic/wall.toml ]] || install -m644 modules/wall/integration/wall.toml.example "$CONFIG/kinetic/wall.toml"
[[ -f $STATE/kinetic/signals/wall ]] || echo off >"$STATE/kinetic/signals/wall"

# ---------- shell plugins ----------
step "Building the Kinetic bar from Omarchy's bar"
KINETIC_BIN="$BIN" python3 "$HOME_DIR/modules/bar/build.py"
step "Installing the Kinetic panel"
rm -rf "$PLUGINS/kinetic"
cp -r shell/kinetic "$PLUGINS/kinetic"
rm -rf "$PLUGINS/kinetic/tests"
KINETIC_BIN="$BIN" python3 "$HOME_DIR/modules/bar/build.py" --env "$PLUGINS/kinetic"

# The app: the same pages, in a window of their own (a shell service, kinetic.app).
step "Installing the Kinetic app"
rm -rf "$PLUGINS/kinetic.app"
cp -r shell/kinetic "$PLUGINS/kinetic.app"
rm -rf "$PLUGINS/kinetic.app/tests"
cp shell/app-manifest.json "$PLUGINS/kinetic.app/manifest.json"
KINETIC_BIN="$BIN" python3 "$HOME_DIR/modules/bar/build.py" --env "$PLUGINS/kinetic.app"
mkdir -p "$HOME/.local/share/applications"
sed "s|@KINETIC@|$BIN/kinetic|" integration/kinetic.desktop > "$HOME/.local/share/applications/kinetic.desktop"
# Hyprland: the app's window floats
TOGGLES="${XDG_STATE_HOME:-$HOME/.local/state}/omarchy/toggles/hypr"
mkdir -p "$TOGGLES"
if ! cmp -s integration/kinetic-app.lua "$TOGGLES/kinetic-app.lua"; then
  cp integration/kinetic-app.lua "$TOGGLES/kinetic-app.lua"
  command -v omarchy-restart-hyprctl >/dev/null && omarchy-restart-hyprctl >/dev/null 2>&1 || true
fi

shell_running=0
if command -v omarchy-shell >/dev/null && omarchy-shell -q shell rescanPlugins >/dev/null 2>&1; then
  shell_running=1
  sleep 1
fi

# shell.json: the bar and the panel icon switch to Kinetic's
if [[ -f $HOME/.config/omarchy/shell.json ]]; then
  step "Switching the bar to kinetic.bar and the panel icon to Kinetic"
  # shellcheck disable=SC1091
  source omarchy-shell-config
  commit "$NORMALIZE
    | if .bar.background == \"pill\" and (.bar.id // \"omarchy.bar\") == \"omarchy.bar\"
      then .bar.id = \"kinetic.bar\" else . end
    | if any(.plugins[]; .id == \"kinetic.app\") then . else .plugins += [{id: \"kinetic.app\"}] end"
  if ! jq -e '[.bar.layout[]? | arrays | .[] | select(.id == "kinetic")] | length > 0' "$HOME/.config/omarchy/shell.json" >/dev/null; then
    omarchy-bar put kinetic --before omarchy.tray >/dev/null 2>&1 || omarchy-bar put kinetic >/dev/null 2>&1 || true
  fi
fi
"$BIN/kinetic-bar-blur" off >/dev/null 2>&1 || true   # the bar re-asks if it wants blur

# ---------- menu and update hook ----------
if [[ -f $MENU ]]; then
  step "Adding Appearance > Kinetic to the Omarchy menu"
  python3 integration/menu.py add "$MENU" integration/menu.jsonc
fi
install -Dm755 integration/post-update "$HOME/.config/omarchy/hooks/post-update.d/kinetic-bar"

# ---------- the Hyprland plugin: glass behind windows and the selection light ----------
step "Building Kinetic's Hyprland plugin (window glass and the light)"
"$HOME_DIR/kinetic" glass plugin build \
  || echo "    it did not build: window glass and the light stay off until it does (kinetic glass plugin build)"

# ---------- start ----------
"$BIN/kinetic-wall" thumbnails >/dev/null || true
systemctl --user daemon-reload
if (( START_SERVICE )); then
  step "Starting the wallpaper engine"
  systemctl --user enable --now kinetic-wall.service
fi
(( shell_running )) && omarchy-shell -q shell rescanPlugins >/dev/null 2>&1 || true

echo
# Motion stays as it was: off until you turn it on (kinetic motion preset parallax).
# An earlier install's choice is re-applied, so its Hyprland file matches this version.
if [[ -f $CONFIG/kinetic/motion.json ]]; then
  KINETIC_HOME="$HOME_DIR" python3 "$HOME_DIR/modules/motion/motion.py" apply || true
fi

echo "Kinetic is installed. Try:  kinetic app  |  kinetic panel  |  kinetic preset apply contours  |  kinetic motion preset parallax"
echo "If the bar does not change, run: omarchy restart shell"
