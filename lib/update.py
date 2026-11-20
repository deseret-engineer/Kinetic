#!/usr/bin/env python3
"""kinetic update: keep Kinetic up to date from a branch of the repository it came from.

    kinetic update [status] [--json]     what is installed, and the last check
    kinetic update check [--json]        fetch the branch and compare (the branch: kinetic settings)
    kinetic update branches [--json]     the repository's branches
    kinetic update apply [--yes]         install the branch's newest commit, then restart the shell

Updates come from the repository install.sh was run from (its `origin`, recorded
in ~/.local/share/kinetic/install.json). They are fetched into a clone of
Kinetic's own (~/.cache/kinetic/source), never into your working copy, and
installed with that clone's install.sh. The last result is in
~/.local/state/kinetic/update.json (the app watches it) and the last install's
output in update.log beside it.
"""
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import settings  # noqa: E402

HOME = os.path.expanduser("~")
STATE_DIR = os.path.join(os.environ.get("XDG_STATE_HOME") or os.path.join(HOME, ".local/state"), "kinetic")
STATE = os.path.join(STATE_DIR, "update.json")
LOG = os.path.join(STATE_DIR, "update.log")
LOCK = os.path.join(STATE_DIR, "update.lock")
SOURCE = os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.join(HOME, ".cache"), "kinetic", "source")
# a remote Kinetic may fetch from: https, ssh (git@host:path) or, for tests, a local path or file://
URL = re.compile(r"^(https://[A-Za-z0-9.-]+/[^\s]+|ssh://[^\s]+|[A-Za-z0-9._-]+@[A-Za-z0-9.-]+:[^\s]+|file:///[^\s]+|/[^\s]+)$")
GIT_ENV = dict(os.environ, GIT_TERMINAL_PROMPT="0", GIT_SSH_COMMAND="ssh -o BatchMode=yes", LC_ALL="C")


class Failed(Exception):
    pass


def git(*args, cwd=None, timeout=120):
    try:
        r = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, timeout=timeout, env=GIT_ENV)
    except subprocess.TimeoutExpired:
        raise Failed(f"git {args[0]} took too long (the network?)")
    except OSError:
        raise Failed("needs git (omarchy pkg add git)")
    if r.returncode:
        last = r.stderr.strip().splitlines()[-1] if r.stderr.strip() else f"git {args[0]} failed"
        raise Failed(last.removeprefix("fatal: "))
    return r.stdout.strip()


def installed():
    i = settings.installed()
    return {"version": i.get("version"), "commit": i.get("commit"), "branch": i.get("branch"),
            "source": i.get("source"), "dirty": bool(i.get("dirty")), "installed": i.get("installed")}


def read_state():
    try:
        with open(STATE, encoding="utf-8") as f:
            s = json.load(f)
        return s if isinstance(s, dict) else {}
    except (OSError, ValueError):
        return {}


def write_state(s):
    os.makedirs(STATE_DIR, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=STATE_DIR, prefix=".update.")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(s, f, indent=2)
        f.write("\n")
    os.replace(tmp, STATE)  # whole, so the app never reads half of it


def source_url():
    url = installed().get("source")
    if not url:
        raise Failed("this install did not come from a git repository: update by running install.sh from a newer copy")
    if not URL.match(url):
        raise Failed(f"cannot update from {url}")
    return url


def branch():
    return settings.get("updateBranch")


def prepare(url, b):
    """Kinetic's own clone, fetched to the branch's newest commit; -> that commit."""
    if os.path.isdir(os.path.join(SOURCE, ".git")):
        try:
            same = git("remote", "get-url", "origin", cwd=SOURCE) == url
        except Failed:
            same = False
        if not same:
            shutil.rmtree(SOURCE)
    if not os.path.isdir(os.path.join(SOURCE, ".git")):
        os.makedirs(os.path.dirname(SOURCE), exist_ok=True)
        shutil.rmtree(SOURCE, ignore_errors=True)
        # history and trees only; files come when a commit is checked out
        git("clone", "--quiet", "--no-checkout", "--filter=blob:none", "--no-tags", "--", url, SOURCE, timeout=300)
    try:
        git("fetch", "--quiet", "--no-tags", "--prune", "origin", f"+refs/heads/{b}:refs/remotes/origin/{b}",
            cwd=SOURCE, timeout=300)
    except Failed as e:
        if "couldn't find remote ref" in str(e):
            raise Failed(f"there is no branch {b}")
        raise
    return git("rev-parse", f"refs/remotes/origin/{b}", cwd=SOURCE)


def version_at(commit):
    try:
        text = git("show", f"{commit}:lib/kinetic.sh", cwd=SOURCE)
    except Failed:
        return None
    m = re.search(r"^KINETIC_VERSION=(\S+)$", text, re.M)
    return m.group(1) if m else None


def known(commit):
    if not commit or not re.match(r"^[0-9a-f]{7,40}$", commit):
        return False
    try:
        git("cat-file", "-e", f"{commit}^{{commit}}", cwd=SOURCE)
        return True
    except Failed:
        return False


def ancestor(a, b):
    try:
        git("merge-base", "--is-ancestor", a, b, cwd=SOURCE)
        return True
    except Failed:
        return False


def check():
    """-> the state: current, available, ahead, different or unknown (and what changed)."""
    i, b = installed(), branch()
    s = {"checked": int(time.time()), "branch": b, "installed": i}
    url = source_url()
    latest = prepare(url, b)
    have = i.get("commit")
    s.update(latest=latest, latestVersion=version_at(latest), behind=0, changes=[])
    if have and have == latest:  # the same commit: current, unless installed with changes of its own
        s["status"] = "available" if i.get("dirty") else "current"
    elif known(have) and ancestor(have, latest):
        s["status"] = "available"
        s["behind"] = int(git("rev-list", "--count", f"{have}..{latest}", cwd=SOURCE))
        s["changes"] = git("log", "--format=%s", "-n", "12", f"{have}..{latest}", cwd=SOURCE).splitlines()
    elif known(have) and ancestor(latest, have):
        s["status"] = "ahead"  # installed from newer work than the branch has
    else:
        s["status"] = "different" if have else "unknown"  # another branch or an unknown install
    return s


def checked():
    """check(), recorded for the app, errors included."""
    try:
        s = check()
    except Failed as e:
        s = {"checked": int(time.time()), "branch": branch(), "installed": installed(), "status": "error",
             "error": str(e)}
    write_state(s)
    return s


def branches():
    out = git("ls-remote", "--heads", "--", source_url(), timeout=60)
    names = [line.split("refs/heads/", 1)[1] for line in out.splitlines() if "refs/heads/" in line]
    return sorted((n for n in names if settings.BRANCH.match(n)), key=lambda n: (n not in ("main", "master"), n != "dev", n))


def say(s):
    i = s.get("installed") or {}
    have = f"Kinetic {i.get('version') or '?'}" + (f" ({i['branch']} @ {i['commit'][:7]})" if i.get("commit") else "")
    print(f"installed: {have}" + (", with local changes" if i.get("dirty") else ""))
    st = s.get("status")
    if not st:
        print("not checked yet (kinetic update check)")
        return
    if st == "error":
        print(f"could not check: {s.get('error')}")
        return
    latest = f"{s['branch']} @ {s['latest'][:7]}" + (f", Kinetic {s['latestVersion']}" if s.get("latestVersion") else "")
    print({"current": f"up to date with {latest}",
           "available": f"an update: {latest}" + (f", {s['behind']} commits" if s.get("behind") else ""),
           "ahead": f"newer than {latest}: nothing to update",
           "different": f"{latest} is not where this install came from: updating switches to it",
           "unknown": f"{latest}: this install's commit is unknown",
           "updating": f"updating to {latest}…",
           "failed": f"the update to {latest} failed: see {LOG}"}.get(st, st))
    for c in s.get("changes") or []:
        print(f"  · {c}")


def lock():
    """One update at a time: an exclusive lock held for the life of this process (and its exec)."""
    import fcntl
    os.makedirs(STATE_DIR, exist_ok=True)
    fd = os.open(LOCK, os.O_RDWR | os.O_CREAT, 0o600)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise Failed("an update is already running")
    return fd  # inherited across exec: the lock lasts until the install ends


def apply(yes):
    s = check()
    if s["status"] in ("current", "ahead") and not os.environ.get("KINETIC_UPDATE_FORCE"):
        write_state(s)
        say(s)
        return 0
    if not yes:
        say(s)
        if not (sys.stdin.isatty() and input("Install it? [y/N] ").strip().lower() in ("y", "yes")):
            print("left as it is")
            return 1
    fd = lock()
    git("checkout", "--quiet", "--force", "-B", s["branch"], s["latest"], cwd=SOURCE, timeout=300)
    git("clean", "-fdq", cwd=SOURCE)  # leaves the ignored build folders, for a quicker build
    s["status"] = "updating"
    s.pop("error", None)
    write_state(s)
    # The install replaces this very program, and the branch may be from before it existed,
    # so the last step runs from a copy of it set aside now: install.sh in the clone, then
    # `finished` from the copy, which works whether the install succeeds, fails or is old.
    # (KINETIC_UPDATE_INSTALL stands in for install.sh in the tests: words, never shell code)
    here = os.path.dirname(os.path.abspath(__file__))
    aside = os.path.join(STATE_DIR, "updater")
    shutil.rmtree(aside, ignore_errors=True)
    os.makedirs(aside)
    for f in ("update.py", "settings.py"):
        shutil.copy2(os.path.join(here, f), aside)
    install = shlex.split(os.environ.get("KINETIC_UPDATE_INSTALL") or "./install.sh")
    script = ('cd "$1" || exit 1; log=$2; finish=$3; shift 3; '
              'if "$@" >"$log" 2>&1; then rc=0; else rc=$?; fi; '
              'exec python3 "$finish" finished "$rc"')
    os.set_inheritable(fd, True)
    sys.stdout.flush()
    os.execvp("bash", ["bash", "-c", script, "kinetic-update", SOURCE, LOG, os.path.join(aside, "update.py"), *install])


def finished(rc):
    """After the install: record it, say so, and restart the shell so the app and bar load the new version."""
    s = read_state()
    ok = rc == "0"
    s["status"] = "current" if ok else "failed"
    s["finished"] = int(time.time())
    if ok:
        s["installed"] = installed()
        s["behind"], s["changes"] = 0, []
    write_state(s)
    title = "Kinetic updated" if ok else "Kinetic could not update"
    body = (f"Now {s.get('latestVersion') or ''} ({s.get('branch')} @ {str(s.get('latest'))[:7]})" if ok
            else f"The install failed; see {LOG}")
    print(f"{title}: {body}")
    if os.environ.get("KINETIC_UPDATE_INSTALL"):  # a test: no desktop to tell or restart
        return 0 if ok else 1
    if shutil.which("notify-send"):
        subprocess.run(["notify-send", "-a", "Kinetic", title, body], capture_output=True)
    if ok and shutil.which("omarchy-restart-shell"):
        subprocess.run(["omarchy-restart-shell"], capture_output=True)
    return 0 if ok else 1


def main(argv):
    args = argv[1:]
    as_json = "--json" in args
    yes = "--yes" in args or "-y" in args
    args = [a for a in args if a not in ("--json", "--yes", "-y")]
    cmd = args[0] if args else "status"
    try:
        if cmd in ("help", "-h", "--help"):
            print(__doc__.split("\n\n", 1)[1].rstrip())
            return 0
        if cmd == "status":
            s = read_state()
            s["installed"] = installed()
            s.setdefault("branch", branch())
            print(json.dumps(s)) if as_json else say(s)
            return 0
        if cmd == "check":
            s = checked()
            print(json.dumps(s)) if as_json else say(s)
            return 1 if s["status"] == "error" else 0
        if cmd == "branches":
            b = branches()
            print(json.dumps(b) if as_json else "\n".join(b))
            return 0
        if cmd == "apply":
            return apply(yes)
        if cmd == "finished" and len(args) == 2:
            return finished(args[1])
        print(f"kinetic update: no command {cmd} (kinetic update help)", file=sys.stderr)
        return 2
    except Failed as e:
        if cmd == "apply":
            s = read_state()
            s.update(status="error", error=str(e), checked=int(time.time()))
            write_state(s)
        print(f"kinetic update: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
