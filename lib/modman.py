#!/usr/bin/env python3
"""Kinetic's module manager: manifests, dependencies, and adding modules.

A module is a folder with a module.sh (its commands) and a module.json (its
manifest: what it is, what it needs, and its pages in the app). Kinetic's own
modules live in $KINETIC_HOME/modules; the ones you add live in
$KINETIC_MODULES_USER (~/.local/share/kinetic-modules), so reinstalling Kinetic
keeps them, and they can never stand in for one of Kinetic's own.

    modman.py list [--json]                    every module, its state, what it needs
    modman.py inspect [--json] <source>        fetch and check a module and the dependencies
                                               it would bring, into a staging folder; runs nothing
    modman.py install <source> [--yes] [--no-enable]
                                               inspect, ask, install it all, switch it on
    modman.py install --staged <dir> [--yes]   install exactly what an inspect staged
    modman.py remove <id> [--yes] [--force]    remove a module you added
    modman.py update <id> [--yes]              fetch it again from where it came from
    modman.py plan-enable <id>                 the modules to switch on first (it needs them)
    modman.py plan-disable <id>                the modules that stop with it (they need it)
    modman.py validate <dir>                   check a module folder's manifest

A source is a GitHub link (https://github.com/owner/repo, optionally
/tree/<ref>/<subfolder>), any https git link ending in .git, a folder, or a
.zip / .tar.gz / .tar.xz archive. Fetching runs nothing from the module:
installing does, so nothing is installed without a yes.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile

HOME = os.path.expanduser("~")
KINETIC_HOME = os.environ.get("KINETIC_HOME") or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILTIN = os.path.join(KINETIC_HOME, "modules")
USER = os.environ.get("KINETIC_MODULES_USER") or os.path.join(
    os.environ.get("XDG_DATA_HOME") or os.path.join(HOME, ".local/share"), "kinetic-modules")
CACHE = os.path.join(os.environ.get("XDG_CACHE_HOME") or os.path.join(HOME, ".cache"), "kinetic")
STAGING = os.path.join(CACHE, "staging")
# the app's own files: a module's pages import "../kinetic-ui/components"
APP_UI = os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.join(HOME, ".config"), "omarchy/plugins/kinetic.app")
RECORD = ".kinetic-install.json"

ID = re.compile(r"^[a-z][a-z0-9-]{0,31}$")
# Semantic Versioning (semver.org): MAJOR.MINOR.PATCH, an optional -pre.release and +build.
# MINOR and PATCH may be left out (1.2 is 1.2.0), as in a requirement such as >=1.2.
VERSION = re.compile(r"^(0|[1-9]\d*)(\.(0|[1-9]\d*)){0,2}(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?(\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$")
MAX_FILES, MAX_BYTES, MAX_DEPTH = 4000, 64 * 1024 * 1024, 8


class Refused(Exception):
    pass


def kinetic_version():
    try:
        for line in open(os.path.join(KINETIC_HOME, "lib/kinetic.sh"), encoding="utf-8"):
            if line.startswith("KINETIC_VERSION="):
                return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return "0"


def vkey(v):
    """A sort key in Semantic Versioning's order: by MAJOR, MINOR, PATCH; a
    pre-release (1.0.0-beta.2) ranks below its release, its identifiers compared
    one by one (numbers numerically and below words); build metadata (+...) is
    ignored. Something unreadable ranks as 0.0.0."""
    m = re.match(r"^\s*v?(\d+)(?:\.(\d+))?(?:\.(\d+))?(?:-([0-9A-Za-z.-]+))?(?:\+[0-9A-Za-z.-]+)?\s*$", str(v))
    if not m:
        return ((0, 0, 0), (1,))
    core = tuple(int(x or 0) for x in m.group(1, 2, 3))
    if not m.group(4):
        return (core, (1,))
    return (core, (0,) + tuple((0, int(p), "") if p.isdigit() else (1, 0, p) for p in m.group(4).split(".")))


def satisfies(have, want):
    """'>=1.2' style: >=, >, ==, <=, <; a bare version means >=."""
    if not want:
        return True
    m = re.match(r"^\s*(>=|<=|==|>|<)?\s*(\S+)\s*$", str(want))
    if not m:
        return False
    op, v = m.group(1) or ">=", vkey(m.group(2))
    h = vkey(have)
    return {">=": h >= v, ">": h > v, "==": h == v, "<=": h <= v, "<": h < v}[op]


# ---------- manifests ----------

def summary_line(path):
    try:
        for line in open(path, encoding="utf-8", errors="replace"):
            if line.startswith("# kinetic:summary="):
                return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return ""


def load_manifest(d, builtin=False):
    """A module folder's manifest, checked. Raises Refused with every problem."""
    sh = os.path.join(d, "module.sh")
    path = os.path.join(d, "module.json")
    if os.path.isfile(path):
        try:
            m = json.load(open(path, encoding="utf-8"))
        except (OSError, ValueError) as e:
            raise Refused(f"module.json does not read: {e}")
        if not isinstance(m, dict):
            raise Refused("module.json is not an object")
    else:  # an older module: only its summary line
        m = {"id": os.path.basename(d.rstrip("/")), "summary": summary_line(sh)}
    problems = []
    mid = m.get("id")
    if not isinstance(mid, str) or not ID.match(mid):
        problems.append("id: lowercase letters, digits and dashes, starting with a letter")
    m.setdefault("name", (mid or "?").capitalize())
    m.setdefault("summary", summary_line(sh))
    m.setdefault("version", kinetic_version() if builtin else "0.0.0")
    m.setdefault("icon", "󰏗")
    m.setdefault("order", 500)
    m.setdefault("requires", [])
    m.setdefault("pages", [])
    m.setdefault("engineGroups", [])
    m.setdefault("hooks", [])
    for k in ("name", "summary", "icon", "author"):
        if k in m and not isinstance(m[k], str):
            problems.append(f"{k}: text")
    if not isinstance(m["version"], str) or not VERSION.match(m["version"]):
        problems.append("version: like 1.2.0")
    if not isinstance(m["order"], (int, float)):
        problems.append("order: a number")
    if m.get("kinetic") and not satisfies(kinetic_version(), m["kinetic"]):
        problems.append(f"needs Kinetic {m['kinetic']} (this is {kinetic_version()})")
    reqs = []
    for r in m["requires"] if isinstance(m["requires"], list) else [None]:
        if isinstance(r, str):
            r = {"id": r}
        if not isinstance(r, dict) or not isinstance(r.get("id"), str) or not ID.match(r["id"]):
            problems.append("requires: entries are an id, or {id, why, source, version, optional}")
            continue
        if r.get("source") is not None and not isinstance(r["source"], str):
            problems.append(f"requires {r['id']}: source is a link")
        reqs.append({"id": r["id"], "why": str(r.get("why", "")), "source": r.get("source"),
                     "version": r.get("version"), "optional": bool(r.get("optional", False))})
    m["requires"] = reqs
    pages = []
    for p in m["pages"] if isinstance(m["pages"], list) else [None]:
        if not isinstance(p, dict) or not isinstance(p.get("id"), str) or not ID.match(p["id"]) or not isinstance(p.get("qml"), str):
            problems.append("pages: entries are {id, label, about, icon, qml, order}")
            continue
        q = p["qml"]
        if q.startswith("builtin:"):
            if not builtin:
                problems.append(f"page {p['id']}: builtin: pages are Kinetic's own")
        elif q.startswith("/") or ".." in q.split("/") or not q.endswith(".qml"):
            problems.append(f"page {p['id']}: qml is a .qml file inside the module")
        elif not os.path.isfile(os.path.join(d, q)):
            problems.append(f"page {p['id']}: {q} is missing")
        pages.append({"id": p["id"], "label": str(p.get("label", m["name"])), "about": str(p.get("about", "")),
                      "icon": str(p.get("icon", m["icon"])), "qml": q, "order": p.get("order", m["order"]),
                      "hero": bool(p.get("hero", False)), "search": bool(p.get("search", False)),
                      "props": p.get("props") if isinstance(p.get("props"), dict) else {}})
    m["pages"] = pages
    if not isinstance(m["hooks"], list) or any(h not in ("install", "uninstall") for h in m["hooks"]):
        problems.append("hooks: any of install, uninstall")
    if not os.path.isfile(sh):
        problems.append("module.sh is missing")
    if problems:
        raise Refused(f"{mid or d}: " + "; ".join(problems))
    return m


def module_dirs():
    """id -> (folder, builtin) for every installed module; Kinetic's own win."""
    found = {}
    for base, builtin in ((BUILTIN, True), (USER, False)):
        if not os.path.isdir(base):
            continue
        for name in sorted(os.listdir(base)):
            d = os.path.join(base, name)
            if os.path.isfile(os.path.join(d, "module.sh")) and name not in found and ID.match(name):
                found[name] = (d, builtin)
    return found


def installed():
    """id -> manifest (with dir, builtin, record) of every module that loads."""
    out = {}
    for mid, (d, builtin) in module_dirs().items():
        try:
            m = load_manifest(d, builtin)
        except Refused as e:
            m = {"id": mid, "name": mid, "summary": f"(broken: {e})", "version": "0", "icon": "󰀦", "order": 999,
                 "requires": [], "pages": [], "engineGroups": [], "hooks": [], "broken": str(e)}
        m["dir"], m["builtin"] = d, builtin
        try:
            m["record"] = json.load(open(os.path.join(d, RECORD), encoding="utf-8"))
        except (OSError, ValueError):
            m["record"] = None
        out[mid] = m
    return out


def ask_active(mods, ids):
    """Each module's own answer to `active`, sharing one facts snapshot."""
    env = dict(os.environ, KINETIC_HOME=KINETIC_HOME)
    fd, facts = tempfile.mkstemp(prefix="kinetic-facts.")
    os.close(fd)
    env["KINETIC_FACTS"] = facts
    out = {}
    try:
        for mid in ids:
            m = mods[mid]
            try:
                r = subprocess.run([os.path.join(m["dir"], "module.sh"), "active"], capture_output=True, text=True,
                                   timeout=8, env=dict(env, KINETIC_MODULE=mid, KINETIC_MODULE_DIR=m["dir"]))
                a = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else ""
                out[mid] = True if a == "true" else False if a == "false" else None
            except (OSError, subprocess.TimeoutExpired):
                out[mid] = None
    finally:
        os.unlink(facts)
    return out


def listing(with_active=True):
    mods = installed()
    ids = sorted(mods, key=lambda i: (mods[i]["order"], i))
    active = ask_active(mods, ids) if with_active else {}
    rows = []
    for mid in ids:
        m = mods[mid]
        reqs = []
        for r in m["requires"]:
            have = mods.get(r["id"])
            reqs.append(dict(r, installed=have is not None, active=active.get(r["id"]),
                             versionOk=bool(have) and satisfies(have["version"], r.get("version"))))
        pages = []
        for p in m["pages"]:
            p = dict(p)
            if not p["qml"].startswith("builtin:"):
                p["qml"] = os.path.join(m["dir"], p["qml"])
            pages.append(p)
        rows.append({
            "id": mid, "name": m["name"], "summary": m["summary"], "version": m["version"], "icon": m["icon"],
            "author": m.get("author", "Kinetic" if m["builtin"] else ""), "order": m["order"], "builtin": m["builtin"],
            "source": (m["record"] or {}).get("source"), "commit": (m["record"] or {}).get("commit"),
            "requires": reqs, "requiredBy": [o for o in ids if any(r["id"] == mid and not r["optional"] for r in mods[o]["requires"])],
            "pages": pages, "engineGroups": m["engineGroups"], "active": active.get(mid), "broken": m.get("broken"),
        })
    return rows


# ---------- dependency order for switching ----------

def plan_enable(target):
    mods = installed()
    if target not in mods:
        raise Refused(f"no module named '{target}'")
    order, seen = [], set()

    def visit(mid, chain):
        if mid in chain:
            raise Refused("modules need each other in a circle: " + " -> ".join(chain + [mid]))
        if mid in seen:
            return
        seen.add(mid)
        for r in mods[mid]["requires"]:
            if r["optional"]:
                continue
            if r["id"] not in mods:
                how = f": kinetic module install {r['source']}" if r.get("source") else ""
                raise Refused(f"{mid} needs {r['id']}, which is not installed{how}")
            visit(r["id"], chain + [mid])
            order.append(r["id"])

    visit(target, [])
    deduped = list(dict.fromkeys(order))
    active = ask_active(mods, deduped)
    return [m for m in deduped if active.get(m) is False]


def plan_disable(target):
    mods = installed()
    out, seen = [], set()

    def visit(mid):
        for o, m in mods.items():
            if o not in seen and any(r["id"] == mid and not r["optional"] for r in m["requires"]):
                seen.add(o)
                visit(o)
                out.append(o)

    visit(target)
    active = ask_active(mods, out)
    return [m for m in out if active.get(m)]


# ---------- sources ----------

GITHUB = re.compile(r"^https://(github\.com|gitlab\.com|codeberg\.org)/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(\.git)?"
                    r"(/(tree|-/tree|src/branch)/([A-Za-z0-9_./-]+?))?/?$")


def parse_source(src):
    """-> (kind, location, ref, subfolder): kind git, dir, zip or tar."""
    src = src.strip()
    if src.startswith("-") or "\n" in src:
        raise Refused("that is not a link or a path")
    if src.startswith("file://"):
        if os.environ.get("KINETIC_ALLOW_FILE_URLS") != "1":
            src = src[7:]  # a dropped file: a path
        else:
            return "git", src, None, ""
    m = GITHUB.match(src)
    if m:
        host, owner, repo, _, _, _, rest = m.groups()
        ref, sub = None, ""
        if rest:  # tree/<ref>/<sub...>: the ref is the first part
            parts = rest.split("/")
            ref, sub = parts[0], "/".join(parts[1:])
        return "git", f"https://{host}/{owner}/{repo}.git", ref, sub
    if re.match(r"^https://[A-Za-z0-9.-]+/[A-Za-z0-9_./~-]+\.git$", src):
        return "git", src, None, ""
    if re.match(r"^[a-z]+://", src):
        raise Refused("only GitHub, GitLab and Codeberg links, https git links ending in .git, folders and archives")
    path = os.path.abspath(os.path.expanduser(src))
    if os.path.isdir(path):
        return "dir", path, None, ""
    low = path.lower()
    if os.path.isfile(path) and low.endswith(".zip"):
        return "zip", path, None, ""
    if os.path.isfile(path) and re.search(r"\.(tar|tar\.gz|tgz|tar\.xz|txz|tar\.bz2)$", low):
        return "tar", path, None, ""
    raise Refused(f"no folder or archive at {path}")


def check_tree(root):
    """No links (nothing may point outside), and a sane size."""
    files = size = 0
    for dirpath, dirnames, filenames in os.walk(root):
        if ".git" in dirnames:
            dirnames.remove(".git")
        for n in dirnames + filenames:
            p = os.path.join(dirpath, n)
            if os.path.islink(p):
                raise Refused(f"it may not contain links ({os.path.relpath(p, root)})")
        files += len(filenames)
        size += sum(os.path.getsize(os.path.join(dirpath, f)) for f in filenames)
        if files > MAX_FILES or size > MAX_BYTES:
            raise Refused("too big (over 4000 files or 64 MB)")


def check_archive(sizes):
    """The size limits, checked before anything is unpacked (an archive can expand a thousandfold)."""
    if len(sizes) > MAX_FILES or sum(sizes) > MAX_BYTES:
        raise Refused("too big (over 4000 files or 64 MB)")


def find_root(d, sub):
    root = os.path.normpath(os.path.join(d, sub)) if sub else d
    if os.path.commonpath([root, d]) != d:  # not startswith: /x/src2 starts with /x/src
        raise Refused("that subfolder leaves the repository")
    if os.path.isfile(os.path.join(root, "module.sh")):
        return root
    inner = [os.path.join(root, n) for n in os.listdir(root) if not n.startswith(".")] if os.path.isdir(root) else []
    if len(inner) == 1 and os.path.isfile(os.path.join(inner[0], "module.sh")):
        return inner[0]  # an archive with one folder in it
    raise Refused("no module.sh there: a module is a folder with module.sh and module.json")


def fetch(src, into, find=None):
    """Fetches a source into `into` (runs nothing from it); -> (root, record).
    find(base, sub) picks the root in what was fetched (a module's, by default)."""
    kind, loc, ref, sub = parse_source(src)
    os.makedirs(into)
    record = {"source": src, "fetched": int(time.time())}
    if kind == "git":
        if not shutil.which("git"):
            raise Refused("needs git (omarchy pkg add git)")
        cmd = ["git", "-c", "core.hooksPath=/dev/null", "clone", "--quiet", "--depth", "1", "--no-recurse-submodules"]
        if ref:
            cmd += ["--branch", ref]
        r = subprocess.run(cmd + ["--", loc, os.path.join(into, "src")], capture_output=True, text=True, timeout=180,
                           env=dict(os.environ, GIT_TERMINAL_PROMPT="0"))
        if r.returncode:
            raise Refused(f"could not fetch {loc}: {r.stderr.strip().splitlines()[-1] if r.stderr.strip() else 'git failed'}")
        c = subprocess.run(["git", "-C", os.path.join(into, "src"), "rev-parse", "HEAD"], capture_output=True, text=True)
        record["commit"] = c.stdout.strip()
        record["ref"] = ref
        base = os.path.join(into, "src")
    elif kind == "dir":
        base = os.path.join(into, "src")
        shutil.copytree(loc, base, symlinks=True, ignore=shutil.ignore_patterns(".git"))
    elif kind == "zip":
        base = os.path.join(into, "src")
        with zipfile.ZipFile(loc) as z:
            for info in z.infolist():
                name = info.filename
                if name.startswith("/") or ".." in name.split("/") or (info.external_attr >> 16) & 0o170000 == 0o120000:
                    raise Refused(f"the archive has an unsafe entry: {name}")
            check_archive([i.file_size for i in z.infolist() if not i.is_dir()])
            z.extractall(base)
    else:
        base = os.path.join(into, "src")
        with tarfile.open(loc) as t:
            check_archive([m.size for m in t.getmembers() if m.isfile()])
            t.extractall(base, filter="data")  # refuses paths outside, absolute paths and outside links
    check_tree(base)
    root = (find or find_root)(base, sub)
    shutil.rmtree(os.path.join(root, ".git"), ignore_errors=True)
    return root, record


def contents(root, m):
    sh = open(os.path.join(root, "module.sh"), encoding="utf-8", errors="replace").read()
    files = sum(len(f) for _, _, f in os.walk(root))
    return {"files": files, "pages": [p["label"] for p in m["pages"]], "hooks": m["hooks"],
            "commands": sorted(set(re.findall(r"^\s+([a-z][a-z-]*)\)", sh, re.M)) - {"help"})[:20]}


# ---------- inspect: everything a yes would install ----------

def inspect(src):
    os.makedirs(STAGING, exist_ok=True)
    stage = tempfile.mkdtemp(prefix="stage-", dir=STAGING)
    have = installed()
    plan, optional = [], []
    try:
        def visit(source, reason, depth, chain):
            if depth > MAX_DEPTH:
                raise Refused("dependencies nest too deep")
            root, record = fetch(source, os.path.join(stage, f"{len(plan)}-{depth}"))
            m = load_manifest(root)
            mid = m["id"]
            if mid in chain:
                raise Refused("modules need each other in a circle: " + " -> ".join(chain + [mid]))
            if mid in have and have[mid]["builtin"]:
                raise Refused(f"'{mid}' is one of Kinetic's own modules: a module you add needs another id")
            if any(p["id"] == mid for p in plan):
                return
            for r in m["requires"]:
                there = have.get(r["id"]) or next((p["manifest"] for p in plan if p["id"] == r["id"]), None)
                if there:
                    if not satisfies(there["version"], r.get("version")):
                        raise Refused(f"{mid} needs {r['id']} {r['version']}, and {there['version']} is here")
                    continue
                if r["optional"]:
                    optional.append({"id": r["id"], "for": mid, "why": r["why"], "source": r.get("source")})
                    continue
                if not r.get("source"):
                    raise Refused(f"{mid} needs {r['id']}, which is not installed and has no link to fetch it from")
                visit(r["source"], f"needed by {m['name']}" + (f": {r['why']}" if r["why"] else ""), depth + 1, chain + [mid])
            plan.append({"id": mid, "name": m["name"], "version": m["version"], "summary": m["summary"],
                         "author": m.get("author", ""), "icon": m["icon"], "source": record["source"],
                         "commit": record.get("commit"), "reason": reason, "replaces": have[mid]["version"] if mid in have else None,
                         "contains": contents(root, m), "path": root, "record": record, "manifest": m})

        visit(src, "requested", 0, [])
    except Refused:
        shutil.rmtree(stage, ignore_errors=True)
        raise
    except (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile, subprocess.TimeoutExpired) as e:
        shutil.rmtree(stage, ignore_errors=True)
        raise Refused(f"could not read that module: {e}")
    result = {"staging": stage, "modules": [{k: v for k, v in p.items() if k != "manifest"} for p in plan],
              "optional": optional,
              "warning": "Installing runs these modules' code on your computer, with your permissions. "
                         "Only install modules from people you trust."}
    json.dump(result, open(os.path.join(stage, "plan.json"), "w"), indent=2)
    return result


def describe_plan(plan):
    lines = []
    for p in plan["modules"]:
        what = ("update " + p["replaces"] + " -> " if p["replaces"] else "") + p["version"]
        lines.append(f"  {p['name']} ({p['id']} {what}) {p['reason']}")
        lines.append(f"      from {p['source']}" + (f" @ {p['commit'][:10]}" if p.get("commit") else ""))
        c = p["contains"]
        bits = [f"{c['files']} files"] + ([f"app pages: {', '.join(c['pages'])}"] if c["pages"] else []) \
            + ([f"runs on {'/'.join(c['hooks'])}"] if c["hooks"] else [])
        lines.append("      " + ", ".join(bits))
    for o in plan["optional"]:
        lines.append(f"  optional, not installed: {o['id']} (for {o['for']}{': ' + o['why'] if o['why'] else ''})")
    return "\n".join(lines)


def confirm(question, yes):
    if yes:
        return True
    if not (sys.stdin.isatty() and sys.stderr.isatty()):
        print(f"{question} (no terminal to ask: add --yes)", file=sys.stderr)
        return False
    sys.stderr.write(question + " [y/N] ")
    sys.stderr.flush()
    return sys.stdin.readline().strip().lower() in ("y", "yes")


def run_hook(d, mid, hook):
    env = dict(os.environ, KINETIC_HOME=KINETIC_HOME, KINETIC_MODULE=mid, KINETIC_MODULE_DIR=d)
    r = subprocess.run([os.path.join(d, "module.sh"), hook], env=env, timeout=600)
    return r.returncode == 0


def install_staged(stage, yes, quiet=False):
    try:
        plan = json.load(open(os.path.join(stage, "plan.json"), encoding="utf-8"))
    except (OSError, ValueError):
        raise Refused("nothing staged there (inspect it first)")
    if os.path.realpath(stage).rsplit("/", 1)[0] != os.path.realpath(STAGING):
        raise Refused("not a Kinetic staging folder")
    if not quiet:
        print("Kinetic would install:\n" + describe_plan(plan) + "\n\n" + plan["warning"], file=sys.stderr)
    if not confirm("Install?", yes):
        shutil.rmtree(stage, ignore_errors=True)
        raise Refused("not installed")
    os.makedirs(USER, exist_ok=True)
    done = []
    for p in plan["modules"]:  # dependencies first
        dest = os.path.join(USER, p["id"])
        old = dest + ".old"
        shutil.rmtree(old, ignore_errors=True)
        if os.path.exists(dest):
            os.rename(dest, old)
        shutil.copytree(p["path"], dest)
        sh = os.path.join(dest, "module.sh")
        os.chmod(sh, os.stat(sh).st_mode | 0o755)
        rec = dict(p["record"], installed=int(time.time()), version=p["version"],
                   requiredBy=None if p["reason"] == "requested" else p["reason"])
        json.dump(rec, open(os.path.join(dest, RECORD), "w"), indent=2)
        # the app's own files, for the module's pages: import "../kinetic-ui/components"
        if p["contains"]["pages"]:
            try:
                os.symlink(APP_UI, os.path.join(dest, "kinetic-ui"))
            except OSError:
                pass
        if "install" in p["contains"]["hooks"] and not run_hook(dest, p["id"], "install"):
            shutil.rmtree(dest, ignore_errors=True)
            if os.path.exists(old):
                os.rename(old, dest)
            raise Refused(f"{p['id']}: its install step failed; nothing of it was kept"
                          + (f" (installed before it: {', '.join(done)})" if done else ""))
        shutil.rmtree(old, ignore_errors=True)
        done.append(p["id"])
    shutil.rmtree(stage, ignore_errors=True)
    return done


def remove(mid, yes, force):
    mods = installed()
    if mid not in mods:
        raise Refused(f"no module named '{mid}'")
    m = mods[mid]
    if m["builtin"]:
        raise Refused(f"'{mid}' is part of Kinetic: switch it off instead (kinetic {mid} disable)")
    users = [o for o, x in mods.items() if any(r["id"] == mid and not r["optional"] for r in x["requires"])]
    if users and not force:
        raise Refused(f"{', '.join(users)} need{'s' if len(users) == 1 else ''} {mid}: remove "
                      f"{'it' if len(users) == 1 else 'them'} first, or add --force")
    if not confirm(f"Remove {m['name']} ({mid}) and delete {m['dir']}?", yes):
        raise Refused("not removed")
    env = dict(os.environ, KINETIC_HOME=KINETIC_HOME, KINETIC_MODULE=mid, KINETIC_MODULE_DIR=m["dir"])
    subprocess.run([os.path.join(m["dir"], "module.sh"), "disable"], env=env, capture_output=True, timeout=60)
    if "uninstall" in m["hooks"]:
        run_hook(m["dir"], mid, "uninstall")
    shutil.rmtree(m["dir"])
    # dependencies it brought that nothing needs now
    left = installed()
    spare = [o for o, x in left.items() if not x["builtin"] and (x["record"] or {}).get("requiredBy")
             and not any(any(r["id"] == o for r in y["requires"]) for y in left.values())]
    return spare


def switch_on(done, skip):
    """You installed it to use it: the requested module comes on (what it needs first)."""
    if skip or not done:
        return ""
    mid = done[-1]  # dependencies come first, the requested module last
    env = dict(os.environ, KINETIC_HOME=KINETIC_HOME, KINETIC_YES="1")
    r = subprocess.run([os.path.join(KINETIC_HOME, "kinetic"), mid, "enable"], env=env, capture_output=True, text=True, timeout=120)
    if r.returncode:
        tail = (r.stderr.strip().splitlines() or ["it did not say why"])[-1]
        return f"; switching {mid} on did not work: {tail}"
    return f"; {mid} is on"


# ---------- command line ----------

def main(argv):
    args = [a for a in argv[1:] if a not in ("--yes", "-y", "--json", "--force", "--quick", "--no-enable")]
    flags = set(argv[1:]) - set(args)
    yes = "--yes" in flags or "-y" in flags or os.environ.get("KINETIC_YES") == "1"
    as_json = "--json" in flags
    cmd = args[0] if args else "help"
    try:
        if cmd == "list":
            rows = listing(with_active="--quick" not in argv)
            if as_json:
                print(json.dumps(rows))
            else:
                for r in rows:
                    state = {True: "on", False: "off", None: "?"}[r["active"]]
                    where = "" if r["builtin"] else f"  ({r['source'] or 'added'})"
                    print(f"  {r['id']:<10} {state:<4} {r['version']:<8} {r['summary']}{where}")
            return 0
        if cmd == "inspect" and len(args) == 2:
            plan = inspect(args[1])
            print(json.dumps(plan) if as_json else describe_plan(plan) + f"\n\nstaged in {plan['staging']}")
            return 0
        if cmd == "install" and len(args) == 3 and args[1] == "--staged":
            done = install_staged(args[2], yes)
            print("installed: " + ", ".join(done) + switch_on(done, "--no-enable" in flags))
            return 0
        if cmd == "install" and len(args) == 2:
            plan = inspect(args[1])
            done = install_staged(plan["staging"], yes)
            print("installed: " + ", ".join(done) + switch_on(done, "--no-enable" in flags))
            return 0
        if cmd == "remove" and len(args) == 2:
            spare = remove(args[1], yes, "--force" in flags)
            print(f"removed {args[1]}" + (f"; added only for it, now unused: {', '.join(spare)} (kinetic module remove <id>)" if spare else ""))
            return 0
        if cmd == "update" and len(args) == 2:
            mods = installed()
            m = mods.get(args[1])
            if not m or m["builtin"] or not (m["record"] or {}).get("source"):
                raise Refused(f"{args[1]}: only modules added from a link or a folder update")
            plan = inspect(m["record"]["source"])
            done = install_staged(plan["staging"], yes)
            print("updated: " + ", ".join(done))
            return 0
        if cmd == "plan-enable" and len(args) == 2:
            print("\n".join(plan_enable(args[1])))
            return 0
        if cmd == "plan-disable" and len(args) == 2:
            print("\n".join(plan_disable(args[1])))
            return 0
        if cmd == "validate" and len(args) == 2:
            m = load_manifest(os.path.abspath(args[1]), os.path.abspath(args[1]).startswith(BUILTIN + "/"))
            need = [r["id"] for r in m["requires"] if not r["optional"]]
            uses = [r["id"] for r in m["requires"] if r["optional"]]
            print(f"{m['id']}: ok ({m['name']} {m['version']}, needs {', '.join(need) or 'nothing'}"
                  + (f", uses {', '.join(uses)} if there" if uses else "") + ")")
            return 0
    except Refused as e:
        print(f"kinetic module: {e}", file=sys.stderr)
        return 1
    print(__doc__.split("\n\n")[1] if cmd != "help" else __doc__, file=sys.stderr if cmd != "help" else sys.stdout)
    return 0 if cmd == "help" else 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
