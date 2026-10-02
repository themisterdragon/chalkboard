"""School mascots as 16x16 pixel art for the boot sequence and the export progress bar.

Each sprite uses four phosphor tones so it follows the screen color: '.' background,
'd' dim, 'n' normal, 'h' bright. A row of 8 characters is the left half of a
symmetrical face and gets mirrored. Row 0 stays blank so a sprite can hop up a pixel.
"""

import json
import pkgutil

from .logo import LOGO

# The ten most common US school mascots (most common first), plus dragons and some regional
# favorites. The art lives in data/mascots.json so anyone can add one without touching code.
MASCOTS = {k: (m["name"], m["rows"]) for k, m in
           json.loads(pkgutil.get_data(__package__, "data/mascots.json").decode("utf-8")).items()}

# The Chalkboard logo in phosphor tones, for when no mascot is picked.
LOGO_TONES = {".": ".", "k": ".", "n": ".", "w": "h", "t": "d", "T": "n"}
LOGO_SPRITE = ["".join(LOGO_TONES[c] for c in row) for row in LOGO]


def sprite(key):
    """The 16 rows of a mascot (or the Chalkboard logo for None/unknown keys)."""
    if key not in MASCOTS:
        return LOGO_SPRITE
    return [r if len(r) == 16 else r + r[::-1] for r in MASCOTS[key][1]]


def name(key):
    return MASCOTS[key][0] if key in MASCOTS else ""


def hop(rows):
    """The sprite one pixel higher (row 0 is blank, so nothing is lost)."""
    return rows[1:] + ["." * len(rows[0])]


def valid(rows):
    """True for 16 rows of 16 pixels (or 8, mirrored) in the four tones; row 0 blank so it can hop."""
    if not isinstance(rows, list) or len(rows) != 16 or not all(isinstance(r, str) and len(r) in (8, 16) for r in rows):
        return False
    full = [r if len(r) == 16 else r + r[::-1] for r in rows]
    return set("".join(full)) <= set(".dnh") and full[0] == "." * 16


MASCOTS = {k: v for k, v in MASCOTS.items() if valid(v[1])}  # a bad edit to the file drops one, not the app
