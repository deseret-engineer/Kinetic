"""A module's options, resolved against its schema.json: the command-line twin of
lib/schema.js (which the bar and the panel use). tests/schema.test.js runs
both over the same inputs, so the two cannot drift apart.

    python3 lib/schema.py normalize <schema.json> [<raw.json>]   effective options, as JSON
    python3 lib/schema.py check <schema.json> <key> <value>       the value as JSON, or an error
"""
import json
import math
import re
import sys

HEX = re.compile(r"^#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$")
MISSING = object()


def _js_key(v):
    """A value as JavaScript would spell it as an object key."""
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float) and v == int(v):
        return str(int(v))
    return str(v)


def resolve_default(opt, resolved):
    d = opt.get("default")
    if not isinstance(d, dict):
        return d
    if "by" in d:
        key = _js_key(resolved.get(d["by"]))
        values = d.get("values") or {}
        return values[key] if key in values else d.get("else")
    if "from" in d:
        try:
            base = float(resolved.get(d["from"]))
        except (TypeError, ValueError):
            base = 0.0
        if not math.isfinite(base):
            base = 0.0
        v = base * d.get("factor", 1)
        return max(d.get("min", -math.inf), min(d.get("max", math.inf), v))
    return None


def _number(v):
    if isinstance(v, bool) or v is None or v == "":
        return None
    try:
        n = float(v)
    except (TypeError, ValueError):
        return None
    return n if math.isfinite(n) else None


def coerce(opt, v):
    """One value checked against its option; MISSING when it does not fit."""
    if v is None:
        return MISSING
    t = opt["type"]
    if t == "choice":
        return v if v in opt["options"] else MISSING
    if t == "bool":
        if v is True or v == "true":
            return True
        if v is False or v == "false":
            return False
        return MISSING
    if t == "color":
        s = str(v).strip()
        if s in (opt.get("options") or []):
            return s
        if s and not s.startswith("#"):
            s = "#" + s
        return s.lower() if HEX.match(s) else MISSING
    if t in ("int", "float"):
        if "special" in opt and v == opt["special"]:
            return v
        n = _number(v)
        if n is None:
            return MISSING
        n = max(opt["min"], min(opt["max"], n))
        if t == "int":
            # JavaScript's Math.round: halves go up
            return int(math.floor(n + 0.5))
        return int(n) if n == int(n) else n
    return MISSING


def normalize(raw, schema):
    raw = raw if isinstance(raw, dict) else {}
    out = {}
    for opt in schema["options"]:
        v = coerce(opt, raw[opt["key"]]) if opt["key"] in raw else MISSING
        out[opt["key"]] = v if v is not MISSING else resolve_default(opt, out)
    return out


def option(schema, key):
    return next((o for o in schema["options"] if o["key"] == key), None)


def check(schema, key, text):
    """A value typed on the command line, converted; raises ValueError with a readable reason."""
    opt = option(schema, key)
    if opt is None:
        raise ValueError(f"unknown option: {key}")
    t = opt["type"]
    v = text
    if t == "bool":
        v = {"on": "true", "off": "false", "yes": "true", "no": "false"}.get(text, text)
    c = coerce(opt, v)
    if c is MISSING:
        if t in ("choice",):
            raise ValueError(f"{key} is one of: {', '.join(opt['options'])}")
        if t == "bool":
            raise ValueError(f"{key} is true or false")
        if t == "color":
            raise ValueError(f"{key} is {', '.join(opt.get('options') or [])} or a color like #7aa2f7")
        raise ValueError(f"{key} takes a number from {opt['min']} to {opt['max']}")
    return c


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def main(argv):
    if len(argv) >= 3 and argv[1] == "normalize":
        raw = load(argv[3]) if len(argv) > 3 else {}
        print(json.dumps(normalize(raw, load(argv[2]))))
        return 0
    if len(argv) == 5 and argv[1] == "check":
        try:
            print(json.dumps(check(load(argv[2]), argv[3], argv[4])))
        except ValueError as e:
            print(e, file=sys.stderr)
            return 1
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
