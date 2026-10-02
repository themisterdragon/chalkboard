"""Check every look, light and dark, against WCAG 2.1 AA contrast (and Modern with every OS accent color).

python3 scripts/check_contrast.py   (exits 1 if anything misses)
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))

from chalkboard.gui.skin import GNOME_ACCENTS, MAC_ACCENTS, SKINS, contrast_problems, palette  # noqa: E402

accents = [None] + sorted(set(MAC_ACCENTS.values()) | set(GNOME_ACCENTS.values())) + \
          ["#FFFFFF", "#000000", "#FFFF00", "#00FFFF", "#0078D4", "#808080"]
bad = 0
for kind in SKINS:
    for dark in (False, True):
        for accent in accents if kind == "modern" else [None]:
            name = f"{SKINS[kind]['name']} {'dark' if dark else 'light'}" + (f" accent {accent}" if accent else "")
            problems = contrast_problems(palette(kind, dark, accent))
            bad += bool(problems)
            if problems or not accent:
                print(("FAIL " if problems else "ok   ") + name)
            for p in problems:
                print("       " + p)
print(f"{bad} palette(s) miss WCAG AA" if bad else "Every palette meets WCAG 2.1 AA contrast.")
sys.exit(1 if bad else 0)
