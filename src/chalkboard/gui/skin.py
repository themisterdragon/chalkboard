"""The two window looks ("skins"), their fonts and scale, and Chalkboard's own pixel-art icons.

Bevel: gray 3-D controls and dark blue title bars. Pinstripe: black-and-white with striped
title bars. Both are drawn by Chalkboard itself, so they look the same on every OS.
"""

import sys
import tkinter as tk
from tkinter import font as tkfont, ttk

from ..logo import LOGO

MAC = sys.platform == "darwin"

SKINS = {
    "bevel": {
        "name": "Bevel", "about": "gray 3-D buttons, blue title bars",
        "desk": "#008080", "desk2": None,
        "face": "#C0C0C0", "light": "#FFFFFF", "shadow": "#808080", "dark": "#000000",
        "text": "#000000", "dim": "#808080", "field": "#FFFFFF",
        "sel": "#000080", "seltext": "#FFFFFF",
        "title": "#000080", "title_text": "#FFFFFF", "title_off": "#FFFFFF", "title_off_text": "#000000",
        "menu": "#FFFFFF", "window": "#C0C0C0", "status_top": False,
        "families": ["Tahoma", "Verdana", "Arial", "Helvetica", "Liberation Sans", "Nimbus Sans", "DejaVu Sans",
                     "Noto Sans", "Adwaita Sans"],
    },
    "pinstripe": {
        "name": "Pinstripe", "about": "black-and-white, striped title bars",
        "desk": "#A8A8A8", "desk2": "#888888",
        "face": "#FFFFFF", "light": "#FFFFFF", "shadow": "#A0A0A0", "dark": "#000000",
        "text": "#000000", "dim": "#909090", "field": "#FFFFFF",
        "sel": "#000000", "seltext": "#FFFFFF",
        "title": "#FFFFFF", "title_text": "#000000", "title_off": "#FFFFFF", "title_off_text": "#909090",
        "menu": "#FFFFFF", "window": "#FFFFFF", "status_top": True,
        "families": ["Geneva", "Verdana", "Lucida Grande", "Helvetica", "Arial", "Liberation Sans", "Nimbus Sans",
                     "DejaVu Sans", "Noto Sans", "Adwaita Sans"],
    },
}
MONO = ["Courier New", "Courier", "Nimbus Mono PS", "Liberation Mono", "DejaVu Sans Mono", "Menlo", "Consolas"]
TEXT_SIZES = [11, 12, 13, 14, 16, 18]

# Icon colors (16-color-display flavor)
INK = {"k": "#000000", "w": "#FFFFFF", "g": "#C0C0C0", "d": "#808080", "r": "#C00000", "y": "#FFE000",
       "Y": "#C8A000", "b": "#2040C0", "B": "#3060E0", "N": "#108040", "n": "#1F5A3A", "t": "#6B4423",
       "T": "#A87040"}

ICONS = {
    "lessons": [
        "..kkkkkkkkkkkk..",
        ".kwwwwwwwwwwwwk.",
        "kdkwrwwwwwwwwwk.",
        ".kwrbbbbbbbbbwk.",
        "kdkwrwwwwwwwwwk.",
        ".kwrbbbbbbbwwwk.",
        "kdkwrwwwwwwwwwk.",
        ".kwrbbbbbbbbbwk.",
        "kdkwrwwwwwwwwwk.",
        ".kwrbbbbbwwwwwk.",
        "kdkwrwwwwwwwwwk.",
        ".kwrbbbbbbbbwwk.",
        "kdkwrwwwwwwwwwk.",
        ".kwwwwwwwwwwwwk.",
        "..kkkkkkkkkkrkk.",
        "............r...",
    ],
    "assessments": [
        "..kkkkkkkkk.....",
        "..kwwwwwwwkk....",
        "..kwrrwwwwkwk...",
        "..krwwrwwwkkkk..",
        "..krrrrwrwwwwk..",
        "..krwwrrrrwwwk..",
        "..krwwrwrwwwwk..",
        "..kwwwwwwwwwwk..",
        "..kwwwNwdddddk..",
        "..kNwNwwwwwwwk..",
        "..kwNwwwdddddk..",
        "..kwwwwwwwwwwk..",
        "..kwwwNwdddddk..",
        "..kNwNwwwwwwwk..",
        "..kwNwwwdddddk..",
        "..kkkkkkkkkkkk..",
    ],
    "standards": [
        "................",
        ".kkkk...........",
        ".krrk.kkkk......",
        ".krrk.kbbk......",
        ".kwwk.kbbk.kkkk.",
        ".krrk.kwwk.kNNk.",
        ".krrk.kbbk.kNNk.",
        ".krrk.kbbk.kwwk.",
        ".krrk.kbbk.kNNk.",
        ".krrk.kbbk.kNNk.",
        ".kwwk.kwwk.kNNk.",
        ".krrk.kbbk.kNNk.",
        ".krrk.kbbk.kwwk.",
        ".kkkk.kkkk.kkkk.",
        "tttttttttttttttt",
        "TTTTTTTTTTTTTTTT",
    ],
    "settings": [
        "kkkkkkkkkkkkkkkk",
        "kwwwwwwwwwwwwwdk",
        "kwggggggggggggdk",
        "kwggkgggkgggkgdk",
        "kwgbbbggkgggkgdk",
        "kwgbbbggkgggkgdk",
        "kwggkgggkggbbbdk",
        "kwggkgggkggbbbdk",
        "kwggkggbbbggkgdk",
        "kwggkggbbbggkgdk",
        "kwggkgggkgggkgdk",
        "kwggkgggkgggkgdk",
        "kwggggggggggggdk",
        "kddddddddddddddk",
        "kkkkkkkkkkkkkkkk",
        "................",
    ],
    "folder": [
        "................",
        "................",
        ".kkkkk..........",
        "kyyyyyk.........",
        "kyyyyyykkkkkkkk.",
        "kyyyyyyyyyyyyyyk",
        "kYYYYYYYYYYYYYYk",
        "kyyyyyyyyyyyyyyk",
        "kyyyyyyyyyyyyyyk",
        "kyyyyyyyyyyyyyyk",
        "kyyyyyyyyyyyyyyk",
        "kyyyyyyyyyyyyyyk",
        "kyyyyyyyyyyyyyyk",
        "kYYYYYYYYYYYYYYk",
        "kkkkkkkkkkkkkkkk",
        "................",
    ],
    "info": [
        ".....kkkkkk.....",
        "...kkBBBBBBkk...",
        "..kBBBBwwBBBBk..",
        ".kBBBBBwwBBBBBk.",
        ".kBBBBBBBBBBBBk.",
        "kBBBBBwwwBBBBBBk",
        "kBBBBBBwwBBBBBBk",
        "kBBBBBBwwBBBBBBk",
        "kBBBBBBwwBBBBBBk",
        "kBBBBBBwwBBBBBBk",
        "kBBBBBwwwwBBBBBk",
        ".kBBBBBBBBBBBBk.",
        ".kBBBBBBBBBBBBk.",
        "..kBBBBBBBBBBk..",
        "...kkBBBBBBkk...",
        ".....kkkkkk.....",
    ],
    "ask": [
        ".....kkkkkk.....",
        "...kkBBBBBBkk...",
        "..kBBBwwwwBBBk..",
        ".kBBBwwBBwwBBBk.",
        ".kBBBBBBBwwBBBk.",
        "kBBBBBBBwwBBBBBk",
        "kBBBBBBwwBBBBBBk",
        "kBBBBBBwwBBBBBBk",
        "kBBBBBBBBBBBBBBk",
        "kBBBBBBwwBBBBBBk",
        "kBBBBBBwwBBBBBBk",
        ".kBBBBBBBBBBBBk.",
        ".kBBBBBBBBBBBBk.",
        "..kBBBBBBBBBBk..",
        "...kkBBBBBBkk...",
        ".....kkkkkk.....",
    ],
    "warn": [
        ".......kk.......",
        "......kyyk......",
        "......kyyk......",
        ".....kyyyyk.....",
        ".....kykkyk.....",
        "....kyykkyyk....",
        "....kyykkyyk....",
        "...kyyykkyyyk...",
        "...kyyykkyyyk...",
        "..kyyyykkyyyyk..",
        "..kyyyyyyyyyyk..",
        ".kyyyyykkyyyyyk.",
        ".kyyyyykkyyyyyk.",
        "kyyyyyyyyyyyyyyk",
        "kkkkkkkkkkkkkkkk",
        "................",
    ],
}
ICONS["logo"] = LOGO
for _name, _rows in ICONS.items():
    assert len(_rows) == 16 and all(len(r) == 16 for r in _rows), _name


def auto_scale(root):
    """Whole-number UI scale. Xwayland with zero scaling reports a 3000px-wide screen at 96 dpi."""
    if MAC:
        return 1
    dpi = root.winfo_fpixels("1i") / 96
    if dpi < 1.25 and root.winfo_screenwidth() >= 2600:
        dpi = 2
    return max(1, min(3, round(dpi)))


class Skin:
    def __init__(self, root, kind, scale, text_px):
        self.root = root
        self.kind = kind if kind in SKINS else "bevel"
        self.c = SKINS[self.kind]
        self.S = scale
        self.bevel = self.kind == "bevel"
        families = set(tkfont.families(root))
        ui = next((f for f in self.c["families"] if f in families), "TkDefaultFont")
        mono = next((f for f in MONO if f in families), "TkFixedFont")
        px = text_px * scale
        self.f = tkfont.Font(root, family=ui, size=-px)
        self.fb = tkfont.Font(root, family=ui, size=-px, weight="bold")
        self.fsmall = tkfont.Font(root, family=ui, size=-max(9, px - 2 * scale))
        self.ftitle = tkfont.Font(root, family=ui, size=-px, weight="bold")
        self.fbig = tkfont.Font(root, family=ui, size=-(px + 6 * scale), weight="bold")
        self.fhuge = tkfont.Font(root, family=ui, size=-(px * 3), weight="bold")
        self.fmono = tkfont.Font(root, family=mono, size=-px)
        self.line = self.f.metrics("linespace")
        self._images = {}

    def __getitem__(self, key):
        return self.c[key]

    def px(self, n):
        return int(n * self.S)

    def icon(self, name, zoom=3):
        """A 16x16 icon drawn at zoom x the UI scale."""
        z = zoom * self.S
        key = (name, z)
        if key not in self._images:
            rows = ICONS[name]
            img = tk.PhotoImage(master=self.root, width=16, height=16)
            img.put(" ".join("{" + " ".join(INK.get(ch, "#000000") for ch in r) + "}" for r in rows), to=(0, 0))
            for y, r in enumerate(rows):
                for x, ch in enumerate(r):
                    if ch == ".":
                        img.tk.call(img.name, "transparency", "set", x, y, 1)
            self._images[key] = img.zoom(z) if z > 1 else img
        return self._images[key]

    def desk_tile(self):
        """64x64 dither tile for the Pinstripe desktop."""
        if "desk" not in self._images:
            a, b = self.c["desk"], self.c["desk2"]
            n = 64
            img = tk.PhotoImage(master=self.root, width=n, height=n)
            step = self.S
            rows = []
            for y in range(n):
                rows.append("{" + " ".join(b if ((x // step) + (y // step)) % 2 and (y // step) % 2 == 0 else a
                                           for x in range(n)) + "}")
            img.put(" ".join(rows), to=(0, 0))
            self._images["desk"] = img
        return self._images["desk"]

    def style_ttk(self):
        """Treeview, scrollbars, and the few ttk pieces Chalkboard uses."""
        c, S = self.c, self.S
        s = ttk.Style(self.root)
        s.theme_use("clam")
        s.configure(".", font=self.f, background=c["face"], foreground=c["text"])
        s.configure("Treeview", background=c["field"], fieldbackground=c["field"], foreground=c["text"],
                    rowheight=self.line + 6 * S, borderwidth=0, relief="flat", font=self.f)
        s.map("Treeview", background=[("selected", c["sel"])], foreground=[("selected", c["seltext"])])
        s.layout("Treeview", [("Treeview.treearea", {"sticky": "nswe"})])
        head_bg = c["face"] if self.bevel else "#FFFFFF"
        s.configure("Treeview.Heading", background=head_bg, foreground=c["text"], font=self.fb,
                    relief="raised" if self.bevel else "flat", borderwidth=S if self.bevel else 0,
                    padding=(4 * S, 2 * S), lightcolor=c["light"], darkcolor=c["shadow"], bordercolor=c["dark"])
        s.map("Treeview.Heading", background=[("active", head_bg)], relief=[("pressed", "sunken")])
        w = 16 * S
        for orient in ("Vertical", "Horizontal"):
            s.configure(f"{orient}.TScrollbar", background=c["face"] if self.bevel else "#FFFFFF",
                        troughcolor="#E0E0E0" if self.bevel else "#D8D8D8", bordercolor=c["dark"],
                        lightcolor=c["light"], darkcolor=c["shadow"], arrowcolor=c["dark"], gripcount=0,
                        arrowsize=w, width=w, relief="raised")
            s.map(f"{orient}.TScrollbar", background=[("active", c["face"] if self.bevel else "#F0F0F0")])
        return s
