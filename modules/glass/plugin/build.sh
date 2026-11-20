#!/bin/bash
# Builds Kinetic's Hyprland plugin against the installed Hyprland's headers.
#   build.sh [out.so]      default: ~/.local/lib/kinetic/kinetic-glass.so
# Writes beside the target and renames, so a running Hyprland that has the old
# one loaded keeps it. Prints the path on success.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:-${KINETIC_LIB:-$HOME/.local/lib/kinetic}/kinetic-glass.so}
mkdir -p "$(dirname "$out")"

command -v g++ >/dev/null || { echo "kinetic-glass: g++ is missing (pacman -S gcc)" >&2; exit 1; }
pkg-config --exists hyprland || { echo "kinetic-glass: Hyprland's headers are missing (the hyprland package ships them)" >&2; exit 1; }

deps="pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon"
tmp="$out.new.$$"
trap 'rm -f "$tmp"' EXIT
# --no-gnu-unique: Hyprland can unload and reload it
g++ -O2 -shared -fPIC -std=c++2b --no-gnu-unique -Wall -Wno-unused-parameter \
    "$here/kinetic-glass.cpp" -o "$tmp" $(pkg-config --cflags $deps) -lGLESv2
mv -f "$tmp" "$out"
trap - EXIT
# the Hyprland commit it was built for, so Kinetic can tell when Hyprland has moved on
inc=$(pkg-config --variable=includedir hyprland 2>/dev/null || true)
sed -n 's/.*GIT_COMMIT_HASH *"\(.*\)".*/\1/p' "${inc:-/usr/include}/hyprland/src/version.h" > "$out.hyprland" 2>/dev/null || true
echo "$out"
