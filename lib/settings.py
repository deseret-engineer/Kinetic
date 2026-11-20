#!/usr/bin/env python3
"""kinetic settings: the app's own settings, the ones that belong to no module.

    kinetic settings [--json]          every setting, its value and its default
    kinetic settings get <key>
    kinetic settings set <key> <value>
    kinetic settings unset <key>       back to its default

They live in ~/.config/kinetic/settings.json; the app's Settings page shows them
in the same shape as a module's options, and `kinetic update` reads them.
"""
import json
import os
import re
import sys

CONFIG = os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"), "kinetic", "settings.json")
# what install.sh recorded about where this install came from (remote, branch, commit)
INSTALLED = os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"), "kinetic", "install.json")
BRANCH = re.compile(r"^(?!-)(?!.*\.\.)[A-Za-z0-9._/-]{1,100}(?<![./])$")

# key -> type, default, label, description (and options for a choice)
SCHEMA = {
    "updateBranch": {"type": "branch", "default": "main", "label": "Update from",
                     "description": "main for releases, dev for the newest work. Until you choose, "
                                    "the branch Kinetic was installed from"},
    "updateCheck": {"type": "bool", "default": True, "label": "Check for updates",
                    "description": "Once a day, when the app opens"},
}


def fail(msg, code=1):
    print(f"kinetic settings: {msg}", file=sys.stderr)
    sys.exit(code)


def read():
    try:
        with open(CONFIG, encoding="utf-8") as f:
            c = json.load(f)
        return c if isinstance(c, dict) else {}
    except (OSError, ValueError):
        return {}


def write(c):
    os.makedirs(os.path.dirname(CONFIG), exist_ok=True)
    tmp = CONFIG + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(c, f, indent=2, sort_keys=True)
        f.write("\n")
    os.replace(tmp, CONFIG)


def check(key, value):
    """A value as given on the command line -> what is stored, or raises ValueError."""
    t = SCHEMA[key]["type"]
    if t == "bool":
        if value in ("true", "on", "1", True):
            return True
        if value in ("false", "off", "0", False):
            return False
        raise ValueError(f"{key} is true or false")
    if t == "branch":
        if not isinstance(value, str) or not BRANCH.match(value):
            raise ValueError(f"{key} is a branch name, like main or dev")
        return value
    raise ValueError(f"unknown type for {key}")


def installed():
    try:
        with open(INSTALLED, encoding="utf-8") as f:
            i = json.load(f)
        return i if isinstance(i, dict) else {}
    except (OSError, ValueError):
        return {}


def default(key):
    if key == "updateBranch":  # where this install came from, if that was a branch
        b = installed().get("branch")
        if isinstance(b, str) and BRANCH.match(b) and b != "HEAD":
            return b
    return SCHEMA[key]["default"]


def get(key, c=None):
    c = read() if c is None else c
    if key in c:
        try:
            return check(key, c[key])
        except ValueError:
            pass  # a bad value by hand: the default
    return default(key)


def items():
    c = read()
    return [dict(SCHEMA[k], key=k, value=get(k, c), default=default(k)) for k in SCHEMA]


def main(argv):
    cmd = argv[1] if len(argv) > 1 else "list"
    args = argv[2:]
    if cmd in ("list", "--json") or (cmd == "list" and args == ["--json"]):
        if cmd == "--json" or "--json" in args:
            print(json.dumps(items()))
        else:
            for i in items():
                print(f"{i['key']:<14} {json.dumps(i['value']):<10} {i['description']}")
        return 0
    if cmd in ("help", "-h", "--help"):
        print(__doc__.split("\n\n", 1)[1].rstrip())
        return 0
    if cmd not in ("get", "set", "unset"):
        fail(f"no command {cmd} (kinetic settings help)", 2)
    if not args or args[0] not in SCHEMA:
        fail(f"no setting named {args[0] if args else '(none)'} ({', '.join(SCHEMA)})")
    key = args[0]
    if cmd == "get":
        print(json.dumps(get(key)))
        return 0
    c = read()
    if cmd == "unset":
        c.pop(key, None)
    else:
        if len(args) < 2:
            fail(f"set {key} needs a value")
        try:
            c[key] = check(key, args[1])
        except ValueError as e:
            fail(str(e))
    write(c)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
