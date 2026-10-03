"""Chibi school mascots (24x24, full color) for the window app, wearing the school's colors.

The terminal app keeps its 16x16 phosphor sprites (mascots.py); these are the window app's
version of the same mascots. The art lives in data/chibis.json.
"""

import json
import pkgutil

from .store import parse_hex

# The package name from __name__: the Mac app's Python 3.9 leaves the usual attribute unset in a .pyz.
PACKAGE = __name__.rpartition(".")[0]
SIZE = 24
_data = None


def data():
    global _data
    if _data is None:
        _data = json.loads(pkgutil.get_data(PACKAGE, "data/chibis.json").decode("utf-8"))
        # a bad edit to the file drops one mascot, not the app
        _data["mascots"] = {k: m for k, m in _data["mascots"].items() if valid(m)}
    return _data


def keys():
    return list(data()["mascots"])


def _full(rows):
    return [r if len(r) == SIZE else r + r[::-1] for r in rows]


def frame(key, cheer=False):
    """24 rows of 24 color letters for a mascot ('.' is clear), standing or cheering. None if unknown."""
    d = data()
    m = d["mascots"].get(key)
    if not m:
        return None
    head = _full(m["head"])
    if cheer and "body" not in m:
        body = _full(d["cheer"]["body"])
        head = [list(r) for r in head]
        for i, r in enumerate(_full(d["cheer"]["raise"])):
            row = head[12 + i]
            for x, c in enumerate(r):
                if c != "." and row[x] == ".":
                    row[x] = c
        head = ["".join(r) for r in head]
    else:
        body = _full(m.get("body") or d["body"])
    return head + body


def colors(key, primary="", secondary=""):
    """{letter: "#rrggbb"} for a mascot; its jersey in the school's colors when they're set."""
    d = data()
    out = dict(d["fixed"])
    out.update(d["jersey"])
    out.update(d["mascots"].get(key, {}).get("colors", {}))
    p, s = parse_hex(primary or ""), parse_hex(secondary or "")
    if p:
        out["p"] = p
    if s:
        out["s"] = s
    if p and not s:
        out["s"] = "#ffffff" if _light(p) < 0.5 else "#1b1b1f"
    return out


def _light(hexcode):
    h = hexcode.lstrip("#")
    r, g, b = (int(h[i:i + 2], 16) / 255 for i in (0, 2, 4))
    return 0.299 * r + 0.587 * g + 0.114 * b


def valid(m):
    """True for a mascot entry with a 15-row head of 12 or 24 pixels in known colors."""
    if not isinstance(m, dict) or not isinstance(m.get("head"), list) or len(m["head"]) != 15:
        return False
    rows = m["head"] + (m.get("body") or [])
    if not all(isinstance(r, str) and len(r) in (12, SIZE) for r in rows):
        return False
    known = set(".kwrpsabcd")
    return set("".join(rows)) <= known and (not m.get("body") or len(m["body"]) == 9)
