#!/bin/bash
# kinetic wall add/remove (lib/wallman.py), in a scratch config: one wallpaper
# from a folder or an archive, a collection, a broken one refused, bundled ids
# kept, --as, removing only what was added. Nothing touches your own setup.
set -euo pipefail
cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
W=modules/wall/wallpapers
run() { env KINETIC_WALL_SOCKET="$T/no.sock" PATH="$PWD/modules/wall/build:$PATH" XDG_CONFIG_HOME="$T/cfg" XDG_CACHE_HOME="$T/cache" XDG_DATA_HOME="$T/data" \
          KINETIC_HOME="$PWD" python3 lib/wallman.py "$@"; }
has() { [[ -f $T/cfg/kinetic/wallpapers/$1/wallpaper.toml ]]; }
fail() { echo "wallpapers test: $*" >&2; exit 1; }

# one wallpaper from a folder: its own name is its id; a bundled id is kept, --as names it
cp -r "$W/orbit" "$T/ringed"
run add "$T/ringed" --yes >/dev/null && has ringed || fail "a folder was not added as 'ringed'"
[[ -f $T/cache/kinetic/wall/thumbs/ringed.png ]] || fail "no preview for an added wallpaper"
if run add "$W/topo" --yes >/dev/null 2>&1; then fail "a wallpaper with a bundled id was added"; fi
run add "$W/topo" --as my-topo --yes >/dev/null && has my-topo || fail "--as did not name it"

# a collection in an archive: the good ones in, the broken one refused
mkdir -p "$T/coll/walls/broken"
cp -r "$W/aurora" "$T/coll/walls/Night Sky"; cp -r "$W/plexus" "$T/coll/walls/dots"
printf 'name = "Broken"\n' > "$T/coll/walls/broken/wallpaper.toml"; printf 'void main(){ nope }\n' > "$T/coll/walls/broken/main.frag"
tar czf "$T/coll.tgz" -C "$T/coll" .
out=$(run add "$T/coll.tgz" --yes --json)
has night-sky && has dots || fail "a collection's wallpapers were not added: $out"
! has broken || fail "a wallpaper that does not compile was added"
[[ $out == *'"id": "broken"'* ]] || fail "the broken one was not reported: $out"

# not a wallpaper: said so (the app hands it to the module installer)
out=$(run add modules/glass --yes --json 2>/dev/null || true)
[[ $out == *'"notWallpaper": true'* ]] || fail "a module was not reported as no wallpaper: $out"
# a link that is not one Kinetic fetches
if run add "http://example.com/x" --yes >/dev/null 2>&1; then fail "a plain http link was accepted"; fi

# listing and removing: only what was added
[[ $(run list | wc -l) -eq 4 ]] || fail "list does not show the 4 added"
if run remove topo --yes >/dev/null 2>&1; then fail "a bundled wallpaper was removed"; fi
run remove dots --yes >/dev/null && ! has dots || fail "remove did not remove"
echo "wallpapers: added from folders, archives and collections (compiled first), refused and removed right"
