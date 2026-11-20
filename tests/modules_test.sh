#!/bin/bash
# The module manager, offline, in throwaway folders: installing with dependencies,
# the yes it needs, switching in dependency order, removing, and what it refuses.
set -euo pipefail
cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export KINETIC_MODULES_USER=$T/user XDG_CACHE_HOME=$T/cache KINETIC_ALLOW_FILE_URLS=1
K() { ./kinetic "$@" </dev/null; }
fail() { echo "modules test: $*" >&2; exit 1; }
says() { K "$@" 2>&1 || true; } # what a command says, whether or not it succeeds

# a module that keeps its on/off in a file, so switching it can be seen
mk() { # mk <dir> <id> <version> <module.json extras>
  mkdir -p "$1"
  cat > "$1/module.sh" <<SH
#!/bin/bash
# kinetic:summary=Test module $2
f="$T/state-$2"
case "\${1:-}" in
  active) [[ -f \$f ]] && echo true || echo false ;;
  enable) touch "\$f" ;;
  disable) rm -f "\$f" ;;
  install) touch "$T/installed-hook-$2" ;;
esac
SH
  chmod +x "$1/module.sh"
  printf '{"id": "%s", "name": "%s", "version": "%s", "summary": "Test %s"%s}\n' "$2" "${2^}" "$3" "$2" "$4" > "$1/module.json"
}

mk "$T/dep-repo" dep 1.2.0 ''
git -C "$T/dep-repo" init -q && git -C "$T/dep-repo" add -A && git -C "$T/dep-repo" -c user.email=t@t -c user.name=t commit -qm dep
mk "$T/demo" demo 0.1.0 ", \"requires\": [{\"id\": \"dep\", \"version\": \">=1.0\", \"source\": \"file://$T/dep-repo\", \"why\": \"testing\"}], \"hooks\": [\"install\"], \"pages\": [{\"id\": \"demo\", \"label\": \"Demo\", \"qml\": \"app/Page.qml\"}]"
mkdir -p "$T/demo/app" && echo 'import QtQuick; Item {}' > "$T/demo/app/Page.qml"

# no yes, no terminal: nothing installs
K module install "$T/demo" 2>/dev/null && fail "installed without a yes"
[[ ! -e $T/user/demo ]] || fail "something was installed without a yes"

# with a yes: the dependency first, from its link
K module install "$T/demo" --yes >/dev/null 2>&1 || fail "install failed"
[[ -x $T/user/dep/module.sh && -x $T/user/demo/module.sh ]] || fail "demo and dep should both be installed"
[[ -f $T/installed-hook-demo ]] || fail "demo's install step did not run"
[[ -L $T/user/demo/kinetic-ui ]] || fail "a module with pages gets the app's components (kinetic-ui)"
jq -e '.commit | length == 40' "$T/user/dep/.kinetic-install.json" >/dev/null || fail "dep's commit was not recorded"
K modules --json | jq -e '[.[] | select(.id == "demo")][0] | (.builtin | not) and .requires[0].installed and (.pages[0].qml | endswith("app/Page.qml"))' >/dev/null \
  || fail "modules --json does not describe demo"

# installed to be used: demo came on, with dep first
[[ -f $T/state-dep && -f $T/state-demo ]] || fail "installing demo should switch it (and dep) on"
# switching follows the dependencies
rm -f "$T/state-dep" "$T/state-demo"
K demo enable >/dev/null 2>&1 || fail "enable failed"
[[ -f $T/state-dep && -f $T/state-demo ]] || fail "enabling demo should switch dep on first"
K dep disable 2>/dev/null && fail "disabling dep should ask first (demo needs it)"
K dep disable --yes >/dev/null 2>&1 || fail "disable --yes failed"
[[ ! -f $T/state-dep && ! -f $T/state-demo ]] || fail "disabling dep should switch demo off too"

# removing: not what something else needs, never Kinetic's own
K module remove dep --yes 2>/dev/null && fail "removed dep while demo needs it"
K module remove glass --yes 2>/dev/null && fail "removed a built-in module"
out=$(K module remove demo --yes 2>&1) || fail "remove failed: $out"
[[ ! -e $T/user/demo && $out == *"now unused: dep"* ]] || fail "removing demo should offer dep, added only for it"
K module remove dep --yes >/dev/null 2>&1 || fail "removing dep failed"

# what is refused
mk "$T/clash" glass 1.0.0 ''
[[ $(says module install "$T/clash" --yes) == *"one of Kinetic's own"* ]] || fail "a module may not take a built-in id"
mk "$T/link" linky 1.0.0 '' && ln -s /etc/passwd "$T/link/pw"
[[ $(says module install "$T/link" --yes) == *"may not contain links"* ]] || fail "links inside a module are refused"
python3 - "$T/evil.tar.gz" <<'PY'
import io, sys, tarfile
with tarfile.open(sys.argv[1], "w:gz") as t:
    for name, data in (("m/module.sh", b"#!/bin/bash\n"), ("m/../../escaped", b"x")):
        i = tarfile.TarInfo(name); i.size = len(data); t.addfile(i, io.BytesIO(data))
PY
K module install "$T/evil.tar.gz" --yes 2>/dev/null && fail "an archive reaching outside was accepted"
[[ ! -e $T/../escaped ]] || fail "the archive wrote outside"
mk "$T/old" old 1.0.0 ", \"requires\": [{\"id\": \"wall\", \"version\": \">=99\"}]"
[[ $(says module install "$T/old" --yes) == *">=99"* ]] || fail "an unmet version should be refused"
mk "$T/a" ca 1.0.0 ", \"requires\": [{\"id\": \"cb\", \"source\": \"$T/b\"}]"
mk "$T/b" cb 1.0.0 ", \"requires\": [{\"id\": \"ca\", \"source\": \"$T/a\"}]"
[[ $(says module install "$T/a" --yes) == *"circle"* ]] || fail "a dependency circle should be refused"
mk "$T/upd" update 1.0.0 ""
[[ $(says module install "$T/upd" --yes) == *"kinetic's own commands"* ]] || fail "a module named like a kinetic command should be refused"
mk "$T/pg" pagey 1.0.0 ", \"pages\": [{\"id\": \"settings\", \"label\": \"S\", \"qml\": \"app/Page.qml\"}]"
[[ $(says module install "$T/pg" --yes) == *"the app's own pages"* ]] || fail "a page named like the app's own should be refused"
[[ -z $(find "$T/user" -mindepth 1 -maxdepth 1 ! -name '.*' 2>/dev/null) ]] || fail "refused installs left something: $(ls "$T/user")"

# links: GitHub, with a branch and a subfolder
python3 -c "
import sys; sys.path.insert(0, 'lib'); import modman as M
assert M.parse_source('https://github.com/a/b') == ('git', 'https://github.com/a/b.git', None, '')
assert M.parse_source('https://github.com/a/b/tree/main/mods/x') == ('git', 'https://github.com/a/b.git', 'main', 'mods/x')
for bad in ('http://github.com/a/b', 'ftp://x/y', '--upload-pack=x', 'https://example.com/a/b'):
    try: M.parse_source(bad); raise SystemExit('accepted ' + bad)
    except M.Refused: pass
"
echo "module manager tests passed (install with dependencies, yes, switching order, remove, refusals)"
