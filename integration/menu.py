#!/usr/bin/env python3
"""Add or remove Kinetic's rows in ~/.config/omarchy/extensions/omarchy-menu.jsonc.

    menu.py add <menu.jsonc> <block.jsonc>    replace any older Kinetic rows with the block
    menu.py remove <menu.jsonc>               take Kinetic's rows out again

Rows are found by their marker comments and ids, never by position, and the
file is only rewritten when something changes (a backup is kept beside it).
"""
import re
import shutil
import sys
import time

# Kinetic's rows, by id and by marker comment
OLD_IDS = re.compile(r'^\s*"appearance\.(kinetic|top-bar\.background\.pill)[\w.-]*"\s*:')
OLD_COMMENTS = re.compile(r'^\s*//\s*kinetic:(begin|end)')


def strip(lines):
    out = [l for l in lines if not OLD_IDS.match(l) and not OLD_COMMENTS.match(l)]
    # collapse the blank lines left behind
    text = re.sub(r"\n{3,}", "\n\n", "".join(out))
    return text


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("add", "remove"):
        print(__doc__, file=sys.stderr)
        return 2
    path = sys.argv[2]
    with open(path, encoding="utf-8") as f:
        original = f.read()
    text = strip(original.splitlines(keepends=True))
    if sys.argv[1] == "add":
        with open(sys.argv[3], encoding="utf-8") as f:
            block = f.read()
        end = text.rstrip().rfind("}")
        if end < 0:
            print(f"{path} does not look like a menu file", file=sys.stderr)
            return 1
        body = text[:end].rstrip()
        # the last row before ours needs its comma (JSONC allows a trailing one)
        if body and not body.endswith(("{", ",")) and not body.splitlines()[-1].lstrip().startswith("//"):
            body += ","
        text = body + "\n\n" + block.rstrip() + "\n}\n"
    if text != original:
        shutil.copy2(path, f"{path}.bak.{int(time.time())}")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"updated {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
