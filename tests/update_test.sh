#!/bin/bash
# kinetic update and kinetic settings, against a scratch copy of this repository:
# checking, branches, every status, applying (with a stand-in for install.sh), the
# lock and refusals. Nothing outside the scratch folder is read or written.
set -euo pipefail
cd "$(dirname "$0")/.."
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
fail() { echo "update test: $*" >&2; exit 1; }
export XDG_CONFIG_HOME=$T/config XDG_STATE_HOME=$T/state XDG_CACHE_HOME=$T/cache XDG_DATA_HOME=$T/data
export GIT_CONFIG_GLOBAL=/dev/null GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@t GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@t
up() { python3 lib/update.py "$@"; }
set_() { python3 lib/settings.py "$@"; }
installed() { mkdir -p "$T/data/kinetic"; jq -n --arg s "$1" --arg b "$2" --arg c "$3" --argjson d "${4:-false}" \
  '{version: "0.1.0", source: $s, branch: $b, commit: $c, dirty: $d}' > "$T/data/kinetic/install.json"; }
st() { jq -r "$1" "$T/state/kinetic/update.json"; }

# a remote with two branches: main a commit behind dev
git init -q -b dev "$T/work"
cp lib/kinetic.sh "$T/w.sh"; mkdir -p "$T/work/lib"
for n in 1 2 3; do
  sed "s/^KINETIC_VERSION=.*/KINETIC_VERSION=0.$n.0/" "$T/w.sh" > "$T/work/lib/kinetic.sh"
  git -C "$T/work" add -A && git -C "$T/work" commit -qm "change $n"
  [[ $n == 2 ]] && git -C "$T/work" branch main
done
git clone -q --bare "$T/work" "$T/remote.git"
c1=$(git -C "$T/work" rev-parse HEAD~2) c3=$(git -C "$T/work" rev-parse HEAD)

# settings: the branch defaults to where Kinetic was installed from
installed "$T/remote.git" dev "$c1"
[[ $(set_ get updateBranch) == '"dev"' ]] || fail "the branch should default to the installed one"
[[ $(set_ get updateCheck) == true ]] || fail "checking should default to on"
set_ set updateBranch '../x' 2>/dev/null && fail "a bad branch name was taken"
set_ set updateCheck maybe 2>/dev/null && fail "a bad bool was taken"
set_ set updateCheck off && [[ $(set_ get updateCheck) == false ]] || fail "updateCheck off"
set_ unset updateCheck && [[ $(set_ get updateCheck) == true ]] || fail "unset"

# branches, then every status
[[ $(up branches --json | jq -c .) == '["main","dev"]' ]] || fail "branches: $(up branches --json)"
up check >/dev/null; [[ $(st .status) == available && $(st .behind) == 2 && $(st .latestVersion) == 0.3.0 ]] \
  || fail "behind dev: $(st .status) $(st .behind) $(st .latestVersion)"
[[ $(st '.changes | join(",")') == "change 3,change 2" ]] || fail "changes: $(st '.changes')"
installed "$T/remote.git" dev "$c3"; up check >/dev/null; [[ $(st .status) == current ]] || fail "current: $(st .status)"
installed "$T/remote.git" dev "$c3" true; up check >/dev/null; [[ $(st .status) == available ]] || fail "dirty: $(st .status)"
set_ set updateBranch main
installed "$T/remote.git" dev "$c3"; up check >/dev/null; [[ $(st .status) == ahead ]] || fail "ahead of main: $(st .status)"
installed "$T/remote.git" other 0123456789abcdef0123456789abcdef01234567; up check >/dev/null
[[ $(st .status) == different ]] || fail "unknown commit: $(st .status)"
set_ set updateBranch gone; up check >/dev/null || true
[[ $(st .status) == error && $(st .error) == "there is no branch gone" ]] || fail "missing branch: $(st .error)"
set_ unset updateBranch

# refusals: no source, and a source that is not a repository address
installed "" dev "$c1"; up check >/dev/null 2>&1 && fail "checked with no source"
[[ $(st .error) == *"did not come from a git repository"* ]] || fail "no source: $(st .error)"
installed "--upload-pack=touch $T/pwned" dev "$c1"; up check >/dev/null 2>&1 && fail "an option as a source"
[[ ! -e $T/pwned ]] || fail "a source ran a command"

# apply: the clone moves to the branch, the stand-in install runs in it, the state says so
installed "$T/remote.git" dev "$c1"
KINETIC_UPDATE_INSTALL="sh -c 'pwd > ran; echo installing'" up apply --yes >/dev/null
[[ $(st .status) == current ]] || fail "after apply: $(st .status)"
[[ $(git -C "$T/cache/kinetic/source" rev-parse HEAD) == "$c3" ]] || fail "the clone is not at the branch tip"
[[ $(cat "$T/cache/kinetic/source/ran") == "$T/cache/kinetic/source" ]] || fail "the install did not run in the clone"
[[ $(cat "$T/state/kinetic/update.log") == installing ]] || fail "the install's output was not logged"
KINETIC_UPDATE_INSTALL="false" up apply --yes >/dev/null 2>&1 && fail "a failed install reported success"
[[ $(st .status) == failed ]] || fail "after a failed install: $(st .status)"
installed "$T/remote.git" dev "$c3"; [[ $(KINETIC_UPDATE_INSTALL=false up apply --yes) == *"up to date"* ]] || fail "applied with nothing new"
installed "$T/remote.git" dev "$c1"; up apply </dev/null >/dev/null 2>&1 && fail "applied without --yes and no terminal"

# one update at a time
installed "$T/remote.git" dev "$c1"
exec 9>"$T/state/kinetic/update.lock"; flock -n 9
out=$(KINETIC_UPDATE_INSTALL=true up apply --yes 2>&1) && fail "a second update ran"
[[ $out == *"already running"* ]] || fail "lock: $out"
exec 9>&-
echo "updates: checked, applied, refused and locked right against a scratch repository"
