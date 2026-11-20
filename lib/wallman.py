#!/usr/bin/env python3
"""Adds and removes wallpapers: what `kinetic wall add` and `kinetic wall remove` run.

    wallman.py add <folder|archive|link> [--as <id>] [--yes] [--json]
    wallman.py remove <id> [--yes] [--json]
    wallman.py list [--json]                  the wallpapers you added

A source is fetched the way modules are (lib/modman.py: GitHub, GitLab and
Codeberg links, https git links, folders, .zip and .tar archives; no links in
it, a sane size, nothing run from it). It may hold one wallpaper (a folder with
wallpaper.toml) or several (a collection: wallpaper folders at its top, or one
level down). Each is compiled with `kinetic-wall validate` before it is copied
into ~/.config/kinetic/wallpapers/<id>, where the engine picks it up at once.
A wallpaper is shaders and a toml file: nothing in it runs on the CPU.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modman  # noqa: E402

HOME = os.path.expanduser("~")
CONFIG = os.environ.get("XDG_CONFIG_HOME") or os.path.join(HOME, ".config")
USER_DIR = os.path.join(CONFIG, "kinetic/wallpapers")
RECORD = ".kinetic-source.json"
ID = re.compile(r"^[a-z0-9][a-z0-9_-]{0,47}$")
MAX_WALLPAPERS = 64


class Refused(Exception):
    pass


def wall_bin():
    """The engine: the installed one, else the repository's build."""
    found = shutil.which("kinetic-wall")
    if found:
        return found
    built = os.path.join(modman.KINETIC_HOME, "modules/wall/build/kinetic-wall")
    if os.access(built, os.X_OK):
        return built
    raise Refused("the wallpaper engine (kinetic-wall) is not installed")


def bundled_ids():
    """Wallpapers that ship with Kinetic (a wallpaper you add with the same id would hide one)."""
    data = os.environ.get("XDG_DATA_HOME") or os.path.join(HOME, ".local/share")
    ids = set()
    for d in (os.path.join(data, "kinetic/wallpapers"), "/usr/local/share/kinetic/wallpapers", "/usr/share/kinetic/wallpapers",
              os.path.join(modman.KINETIC_HOME, "modules/wall/wallpapers")):
        if os.path.isdir(d):
            ids.update(n for n in os.listdir(d) if os.path.isfile(os.path.join(d, n, "wallpaper.toml")))
    return ids


def is_wallpaper(d):
    return os.path.isfile(os.path.join(d, "wallpaper.toml"))


def find_root(base, sub):
    """The folder to look in: the subfolder of a link, or the top (an archive's single folder too)."""
    root = os.path.normpath(os.path.join(base, sub)) if sub else base
    if os.path.commonpath([root, base]) != base:
        raise Refused("that subfolder leaves the repository")
    if not os.path.isdir(root):
        raise Refused(f"no folder {sub} in it")
    return root


def wallpapers_in(root):
    """One wallpaper, or a collection: wallpaper folders at the top, or one level down."""
    if is_wallpaper(root):
        return [root]

    def subdirs(d):
        return [os.path.join(d, n) for n in sorted(os.listdir(d)) if not n.startswith(".") and os.path.isdir(os.path.join(d, n))]

    found = [d for d in subdirs(root) if is_wallpaper(d)]
    if not found:  # e.g. a repository with its wallpapers in wallpapers/
        found = [g for d in subdirs(root) for g in subdirs(d) if is_wallpaper(g)]
    if not found:
        raise Refused("no wallpaper there: a wallpaper is a folder with a wallpaper.toml (see WALLPAPERS.md)")
    if len(found) > MAX_WALLPAPERS:
        raise Refused(f"more than {MAX_WALLPAPERS} wallpapers in one source")
    return found


def toml_name(d):
    try:
        for line in open(os.path.join(d, "wallpaper.toml"), encoding="utf-8", errors="replace"):
            m = re.match(r'^\s*name\s*=\s*"([^"\n]{1,80})"', line)
            if m:
                return m.group(1)
            if line.lstrip().startswith("["):
                break
    except OSError:
        pass
    return None


def make_id(name):
    i = re.sub(r"[^a-z0-9_-]+", "-", name.lower()).strip("-_")[:48]
    return i if ID.match(i) else ""


def source_name(src):
    """What a source calls itself: a link's subfolder or repository, a folder's or archive's name."""
    try:
        kind, loc, _, sub = modman.parse_source(src)
    except modman.Refused:
        return ""
    if sub:
        return os.path.basename(sub.rstrip("/"))
    name = os.path.basename(loc.rstrip("/"))
    return re.sub(r"(\.git|\.zip|\.tar|\.tar\.gz|\.tgz|\.tar\.xz|\.txz|\.tar\.bz2)$", "", name, flags=re.I)


def validate(d):
    """Compiles it offscreen: '' if it builds, else the first lines of the error."""
    try:
        r = subprocess.run([wall_bin(), "validate", d], capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return "compiling it took over a minute"
    if r.returncode == 0:
        return ""
    lines = [l.replace(d, os.path.basename(d)) for l in (r.stderr + r.stdout).strip().splitlines() if l.strip()]
    return "\n".join(lines[-4:]) or "it does not compile"


def confirm(question, yes):
    return modman.confirm(question, yes)


def add(src, as_id=None, yes=False):
    """-> {"added": [{id, name, replaced}], "skipped": [{id, why}]}"""
    os.makedirs(os.path.join(CONFIG, "kinetic"), exist_ok=True)
    stage = tempfile.mkdtemp(prefix="kinetic-wallpaper-")
    try:
        try:
            root, record = modman.fetch(src, os.path.join(stage, "fetch"), find=find_root)
        except modman.Refused as e:
            raise Refused(str(e))
        found = wallpapers_in(root)
        if as_id and len(found) > 1:
            raise Refused("--as names one wallpaper, and there are several here")
        bundled, plan, skipped = bundled_ids(), [], []
        for d in found:
            folder = source_name(src) if d == root else os.path.basename(d.rstrip("/"))
            wid = as_id or make_id(folder)
            if not ID.match(wid or ""):
                skipped.append({"id": os.path.basename(d), "why": "its folder name can't be an id (lowercase letters, digits, - and _): add it with --as <id>"})
                continue
            if wid in bundled and not as_id:
                skipped.append({"id": wid, "why": f"a bundled wallpaper is called {wid}: add it under another id with --as <id>"})
                continue
            err = validate(d)
            if err:
                skipped.append({"id": wid, "why": "it does not compile:\n" + err})
                continue
            plan.append({"id": wid, "name": toml_name(d) or wid, "dir": d,
                         "replaced": os.path.isdir(os.path.join(USER_DIR, wid))})
        if not plan:
            return {"added": [], "skipped": skipped}
        if not yes:
            for p in plan:
                print(f"  {p['id']}  {p['name']}" + ("   (replaces yours)" if p["replaced"] else ""), file=sys.stderr)
            for s in skipped:
                print(f"  skipped {s['id']}: {s['why']}", file=sys.stderr)
            n = len(plan)
            if not confirm(f"Add {n} wallpaper{'s' if n > 1 else ''} to {USER_DIR}?", yes):
                raise Refused("nothing added")
        os.makedirs(USER_DIR, exist_ok=True)
        for p in plan:
            dest = os.path.join(USER_DIR, p["id"])
            tmp = dest + ".kinetic-new"
            shutil.rmtree(tmp, ignore_errors=True)
            shutil.copytree(p["dir"], tmp, ignore=shutil.ignore_patterns(".git"))
            with open(os.path.join(tmp, RECORD), "w") as f:
                json.dump(dict(record, added=int(time.time())), f)
            if os.path.isdir(dest):
                shutil.rmtree(dest)
            os.rename(tmp, dest)
        # their previews, for the library (only the missing ones are drawn)
        subprocess.run([wall_bin(), "thumbnails"], capture_output=True, timeout=300)
        return {"added": [{k: p[k] for k in ("id", "name", "replaced")} for p in plan], "skipped": skipped}
    finally:
        shutil.rmtree(stage, ignore_errors=True)


def remove(wid, yes=False):
    if not ID.match(wid or ""):
        raise Refused(f"no wallpaper id {wid!r}")
    d = os.path.join(USER_DIR, wid)
    if not is_wallpaper(d):
        if wid in bundled_ids():
            raise Refused(f"{wid} ships with Kinetic: only wallpapers you added can be removed")
        raise Refused(f"you have no wallpaper called {wid}")
    if not confirm(f"Remove the wallpaper {wid} ({d})?", yes):
        raise Refused("nothing removed")
    # showing it now: move on first, so the screen is never left without one
    cur = subprocess.run([wall_bin(), "current"], capture_output=True, text=True)
    if cur.returncode == 0 and cur.stdout.strip() == wid:
        subprocess.run([wall_bin(), "next"], capture_output=True)
    shutil.rmtree(d)
    return {"removed": wid}


def listing():
    out = []
    if os.path.isdir(USER_DIR):
        for n in sorted(os.listdir(USER_DIR)):
            d = os.path.join(USER_DIR, n)
            if not is_wallpaper(d):
                continue
            rec = {}
            try:
                rec = json.load(open(os.path.join(d, RECORD)))
            except (OSError, ValueError):
                pass
            out.append({"id": n, "name": toml_name(d) or n, "source": rec.get("source", "")})
    return out


def main(argv):
    want_json = "--json" in argv
    yes = "--yes" in argv or os.environ.get("KINETIC_YES") == "1"
    args = [a for a in argv if a not in ("--json", "--yes")]
    as_id = None
    if "--as" in args:
        i = args.index("--as")
        if i + 1 >= len(args):
            return fail("--as needs an id", want_json)
        as_id = args[i + 1]
        if not ID.match(as_id):
            return fail("an id is lowercase letters, digits, - and _ (at most 48)", want_json)
        del args[i:i + 2]
    cmd = args[0] if args else "help"
    try:
        if cmd == "add" and len(args) == 2:
            r = add(args[1], as_id, yes)
        elif cmd == "remove" and len(args) == 2:
            r = remove(args[1], yes)
        elif cmd == "list" and len(args) == 1:
            r = listing()
        else:
            print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
            return 2
    except Refused as e:
        return fail(str(e), want_json, not_wallpaper="no wallpaper there" in str(e))
    if want_json:
        print(json.dumps(r))
    elif cmd == "add":
        for a in r["added"]:
            print(f"added {a['id']} ({a['name']})" + (", replacing yours" if a["replaced"] else ""))
        for s in r["skipped"]:
            print(f"skipped {s['id']}: {s['why']}", file=sys.stderr)
        if r["added"]:
            print(f"show it: kinetic wall set {r['added'][0]['id']}")
        else:
            return 1
    elif cmd == "remove":
        print(f"removed {r['removed']}")
    else:
        for w in r:
            print(f"{w['id']:<20} {w['name']:<24} {w['source']}")
    return 0


def fail(msg, want_json, not_wallpaper=False):
    if want_json:
        print(json.dumps({"error": msg, "notWallpaper": not_wallpaper}))
    print(f"kinetic wall: {msg}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
