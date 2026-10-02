"""The window looks ("skins") in light and dark, their fonts and scale, and Chalkboard's own pixel-art icons.

Bevel: gray 3-D controls and dark blue title bars. Pinstripe: black-and-white with striped
title bars. Both are drawn by Chalkboard itself, so they look the same on every OS.
Modern: the OS's own title bar, font, and accent color, flat controls, and it follows the
OS's light or dark setting.

Every look comes in light and dark, and every pair of colors meets WCAG 2.1 AA contrast:
4.5:1 for text (dim text too), 3:1 for control edges and the keyboard focus ring.
scripts/check_contrast.py checks them all; only disabled controls ("off") are exempt.
"""

import os
import shutil
import subprocess
import sys
import tkinter as tk
from tkinter import font as tkfont, ttk

from ..logo import LOGO

MAC = sys.platform == "darwin"
WIN = sys.platform == "win32"

# Keys every palette has:
#   desk/desk2   the desktop behind the window (desk2: the Pinstripe dither; None = solid)
#   window       window background      card   group boxes      field  text boxes and lists
#   face/light/shadow/dark   button face and its 3-D edges (dark: outlines and rules)
#   edge         the outline that shows where a text box, list, or drop-down is (3:1)
#   text, dim    text and secondary text (both 4.5:1)      off   disabled text (exempt)
#   sel/seltext  selection; selsoft: a selection in a list without the keyboard focus
#   focus        the keyboard focus ring (3:1)       title*  title bars    menu  menus
#   trough/thumb/head   scroll bars and list headings
SKINS = {
    "bevel": {
        "name": "Bevel", "about": "gray 3-D buttons, blue title bars",
        "desk": "#008080", "desk2": None,
        "face": "#C0C0C0", "light": "#FFFFFF", "shadow": "#808080", "dark": "#000000", "edge": "#000000",
        "text": "#000000", "dim": "#3C3C3C", "off": "#808080", "field": "#FFFFFF", "card": "#C0C0C0",
        "sel": "#000080", "seltext": "#FFFFFF", "selsoft": "#000080", "focus": "#000000",
        "title": "#000080", "title_text": "#FFFFFF", "title_off": "#FFFFFF", "title_off_text": "#000000",
        "menu": "#FFFFFF", "window": "#C0C0C0", "trough": "#E0E0E0", "thumb": "#C0C0C0", "head": "#C0C0C0",
        "status_top": False,
        "families": ["Tahoma", "Verdana", "Arial", "Helvetica", "Liberation Sans", "Nimbus Sans", "DejaVu Sans",
                     "Noto Sans", "Adwaita Sans"],
    },
    "pinstripe": {
        "name": "Pinstripe", "about": "black-and-white, striped title bars",
        "desk": "#A8A8A8", "desk2": "#888888",
        "face": "#FFFFFF", "light": "#FFFFFF", "shadow": "#A0A0A0", "dark": "#000000", "edge": "#000000",
        "text": "#000000", "dim": "#5A5A5A", "off": "#909090", "field": "#FFFFFF", "card": "#FFFFFF",
        "sel": "#000000", "seltext": "#FFFFFF", "selsoft": "#000000", "focus": "#000000",
        "title": "#FFFFFF", "title_text": "#000000", "title_off": "#FFFFFF", "title_off_text": "#5A5A5A",
        "menu": "#FFFFFF", "window": "#FFFFFF", "trough": "#D8D8D8", "thumb": "#FFFFFF", "head": "#FFFFFF",
        "status_top": True,
        "families": ["Geneva", "Verdana", "Lucida Grande", "Helvetica", "Arial", "Liberation Sans", "Nimbus Sans",
                     "DejaVu Sans", "Noto Sans", "Adwaita Sans"],
    },
    "modern": {
        "name": "Modern", "about": "your computer's own style, follows light or dark mode",
        "desk": "#F3F3F5", "desk2": None,
        "face": "#FFFFFF", "light": "#FFFFFF", "shadow": "#D0D0D5", "dark": "#76767C", "edge": "#76767C",
        "text": "#1C1C1E", "dim": "#55555A", "off": "#A0A0A6", "field": "#FFFFFF", "card": "#FFFFFF",
        "sel": "#0A5FD0", "seltext": "#FFFFFF", "selsoft": "#DCDCE2", "focus": "#0A5FD0",
        "title": "#FFFFFF", "title_text": "#1C1C1E", "title_off": "#FFFFFF", "title_off_text": "#55555A",
        "menu": "#FFFFFF", "window": "#F3F3F5", "trough": "#F3F3F5", "thumb": "#8A8A90", "head": "#FFFFFF",
        "status_top": False,
        "families": [],  # the OS's own UI font (TkDefaultFont)
    },
}

# Dark versions: only the colors that change.
DARK = {
    "bevel": {
        "desk": "#0B3B3B", "face": "#3C3C3C", "light": "#8C8C8C", "shadow": "#1A1A1A", "dark": "#000000",
        "edge": "#9A9A9A", "text": "#F0F0F0", "dim": "#C4C4C4", "off": "#7A7A7A", "field": "#1E1E1E",
        "card": "#3C3C3C", "sel": "#3557C8", "seltext": "#FFFFFF", "selsoft": "#3557C8", "focus": "#F0F0F0",
        "title": "#22408F", "title_text": "#FFFFFF", "title_off": "#2A2A2A", "title_off_text": "#C4C4C4",
        "menu": "#2A2A2A", "window": "#3C3C3C", "trough": "#262626", "thumb": "#4A4A4A", "head": "#3C3C3C",
    },
    "pinstripe": {  # white ink on black paper: "dark" is the ink, "light" the paper
        "desk": "#3A3A3A", "desk2": "#262626",
        "face": "#141414", "light": "#141414", "shadow": "#6A6A6A", "dark": "#E8E8E8", "edge": "#E8E8E8",
        "text": "#F2F2F2", "dim": "#B0B0B0", "off": "#6E6E6E", "field": "#141414", "card": "#141414",
        "sel": "#E8E8E8", "seltext": "#000000", "selsoft": "#E8E8E8", "focus": "#F2F2F2",
        "title": "#141414", "title_text": "#F2F2F2", "title_off": "#141414", "title_off_text": "#B0B0B0",
        "menu": "#141414", "window": "#141414", "trough": "#2E2E2E", "thumb": "#141414", "head": "#141414",
    },
    "modern": {
        "desk": "#1E1E20", "face": "#3A3A3E", "light": "#3A3A3E", "shadow": "#3E3E43", "dark": "#8E8E94",
        "edge": "#8E8E94", "text": "#F2F2F4", "dim": "#B4B4BA", "off": "#6E6E74", "field": "#161618",
        "card": "#2A2A2D", "sel": "#2F6FD8", "seltext": "#FFFFFF", "selsoft": "#4A4A50", "focus": "#5B9BFF",
        "title": "#2A2A2D", "title_text": "#F2F2F4", "title_off": "#2A2A2D", "title_off_text": "#B4B4BA",
        "menu": "#2A2A2D", "window": "#1E1E20", "trough": "#1E1E20", "thumb": "#8E8E94", "head": "#2A2A2D",
    },
}
assert all(set(d) <= set(SKINS[k]) for k, d in DARK.items())
THEMES = [("system", "Match my computer"), ("light", "Light"), ("dark", "Dark")]

MONO = ["Courier New", "Courier", "Nimbus Mono PS", "Liberation Mono", "DejaVu Sans Mono", "Menlo", "Consolas"]
TEXT_SIZES = [11, 12, 13, 14, 16, 18, 20, 24]

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

# ------------------------------------------------------------------ contrast (WCAG 2.1)
def rgb(color):
    c = color.lstrip("#")
    return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4))

def hexcolor(r, g, b):
    return "#%02X%02X%02X" % tuple(max(0, min(255, round(v))) for v in (r, g, b))

def luminance(color):
    def ch(v):
        v /= 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (ch(v) for v in rgb(color))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b

def contrast(a, b):
    la, lb = sorted((luminance(a), luminance(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)

def mix(a, b, t):
    return hexcolor(*(x + (y - x) * t for x, y in zip(rgb(a), rgb(b))))

def fit(color, against, ratio):
    """color, darkened or lightened just enough to have `ratio` contrast with every color in `against`."""
    toward = "#000000" if sum(luminance(c) for c in against) / len(against) > 0.18 else "#FFFFFF"
    for i in range(21):
        c = mix(color, toward, i / 20)
        if all(contrast(c, a) >= ratio for a in against):
            return c
    return toward

def contrast_problems(c):
    """Pairs in palette c that miss WCAG AA. [] means it passes."""
    text = [("text", b) for b in ("window", "card", "field", "face", "menu", "head")] + \
           [("dim", b) for b in ("window", "card", "field")] + \
           [("seltext", "sel"), ("title_text", "title"), ("title_off_text", "title_off"),
            ("seltext" if c["selsoft"] == c["sel"] else "text", "selsoft")]
    edges = [("edge", "window"), ("edge", "card"), ("focus", "window"), ("focus", "card"), ("focus", "field")]
    if c["name"] == "Modern":  # flat scroll bars have no 3-D edges, so the thumb itself must stand out
        edges.append(("thumb", "trough"))
    out = []
    for pairs, need in ((text, 4.5), (edges, 3.0)):
        for a, b in pairs:
            r = contrast(c[a], c[b])
            if r < need:
                out.append(f"{a} {c[a]} on {b} {c[b]}: {r:.2f}:1, needs {need}:1")
    bar = fit(c["sel"], [c["trough"]], 3.0)  # the export progress bar's fill (widgets.Window.progress)
    if contrast(bar, c["trough"]) < 3.0:
        out.append(f"progress fill {bar} on trough {c['trough']}: needs 3:1")
    return out

def palette(kind, dark=False, accent=None):
    """The colors for a look. Modern takes the OS accent color, adjusted until it has enough contrast."""
    c = dict(SKINS[kind])
    if dark:
        c.update(DARK[kind])
    if kind == "modern" and accent:
        c["sel"] = fit(accent, ["#FFFFFF"], 4.6)
        c["seltext"] = "#FFFFFF"
        c["focus"] = fit(accent, [c["window"], c["card"], c["field"]], 3.2)
    return c

# ------------------------------------------------------------------ the OS's light/dark setting and accent color
def _run(cmd):
    if not shutil.which(cmd[0]):
        return ""
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=2).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ""

def _kde(key):
    path = os.path.expanduser("~/.config/kdeglobals")
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.startswith(key + "="):
                    return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return ""

def _win_reg(path, name):
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as k:
            return winreg.QueryValueEx(k, name)[0]
    except OSError:
        return None

def system_dark():
    """True when the computer is set to dark mode."""
    if MAC:
        return _run(["defaults", "read", "-g", "AppleInterfaceStyle"]).lower() == "dark"
    if WIN:
        return _win_reg(r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", "AppsUseLightTheme") == 0
    if ":dark" in os.environ.get("GTK_THEME", "").lower():
        return True
    scheme = _run(["gsettings", "get", "org.gnome.desktop.interface", "color-scheme"]).strip("'")
    if scheme in ("prefer-dark", "prefer-light"):
        return scheme == "prefer-dark"
    if "dark" in _kde("ColorScheme").lower():
        return True
    return "dark" in _run(["gsettings", "get", "org.gnome.desktop.interface", "gtk-theme"]).lower()

MAC_ACCENTS = {"-1": "#8E8E93", "0": "#FF3B30", "1": "#FF9500", "2": "#FFCC00", "3": "#28CD41", "4": "#007AFF",
               "5": "#AF52DE", "6": "#FF2D55"}
GNOME_ACCENTS = {"blue": "#3584E4", "teal": "#2190A4", "green": "#3A944A", "yellow": "#C88800", "orange": "#ED5B00",
                 "red": "#E62D42", "pink": "#D56199", "purple": "#9141AC", "slate": "#6F8396"}

def system_accent():
    """The computer's accent color as #RRGGBB, or None."""
    if MAC:
        return MAC_ACCENTS.get(_run(["defaults", "read", "-g", "AppleAccentColor"]) or "4")
    if WIN:
        v = _win_reg(r"Software\Microsoft\Windows\DWM", "AccentColor")
        return hexcolor(v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF) if isinstance(v, int) else None
    name = _run(["gsettings", "get", "org.gnome.desktop.interface", "accent-color"]).strip("'")
    if name in GNOME_ACCENTS:
        return GNOME_ACCENTS[name]
    kde = _kde("AccentColor").split(",")
    if len(kde) == 3 and all(p.strip().isdigit() for p in kde):
        return hexcolor(*(int(p) for p in kde))
    return None

def native_title_bar(root, dark, follow_system):
    """Ask the OS for a light or dark title bar to match (Windows 10/11 and macOS; Linux decides for itself)."""
    try:
        if WIN:
            import ctypes
            root.update_idletasks()
            hwnd = ctypes.windll.user32.GetParent(root.winfo_id())
            on = ctypes.c_int(1 if dark else 0)
            for attr in (20, 19):  # DWMWA_USE_IMMERSIVE_DARK_MODE (19 on older Windows 10)
                if ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, attr, ctypes.byref(on), ctypes.sizeof(on)) == 0:
                    break
        elif MAC:
            look = "auto" if follow_system else ("darkaqua" if dark else "aqua")
            root.tk.call("::tk::unsupported::MacWindowStyle", "appearance", root, look)
    except (tk.TclError, AttributeError, OSError):
        pass

def auto_scale(root):
    """Whole-number UI scale. Xwayland with zero scaling reports a 3000px-wide screen at 96 dpi."""
    if MAC:
        return 1
    dpi = root.winfo_fpixels("1i") / 96
    if dpi < 1.25 and root.winfo_screenwidth() >= 2600:
        dpi = 2
    return max(1, min(3, round(dpi)))

class Skin:
    def __init__(self, root, kind, scale, text_px, dark=False, accent=None):
        self.root = root
        self.kind = kind if kind in SKINS else "bevel"
        self.dark = bool(dark)
        self.c = palette(self.kind, self.dark, accent)
        self.S = scale
        self.bevel = self.kind == "bevel"
        self.modern = self.kind == "modern"
        families = set(tkfont.families(root))
        ui = next((f for f in self.c["families"] if f in families), None)
        if ui is None:
            ui = tkfont.nametofont("TkDefaultFont", root).actual("family")
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
                    rowheight=self.line + (10 if self.modern else 6) * S, borderwidth=0, relief="flat", font=self.f)
        # a list without the keyboard focus shows its selection more softly, so you can see where the focus is
        soft_text = c["seltext"] if c["selsoft"] == c["sel"] else c["text"]
        s.map("Treeview", background=[("selected", "focus", c["sel"]), ("selected", c["selsoft"])],
              foreground=[("selected", "focus", c["seltext"]), ("selected", soft_text)])
        s.layout("Treeview", [("Treeview.treearea", {"sticky": "nswe"})])
        flat = not self.bevel
        s.configure("Treeview.Heading", background=c["head"], foreground=c["text"], font=self.fb,
                    relief="flat" if flat else "raised", borderwidth=0 if flat else S,
                    padding=(4 * S, (5 if self.modern else 2) * S), lightcolor=c["light"], darkcolor=c["shadow"],
                    bordercolor=c["dark"])
        s.map("Treeview.Heading", background=[("active", c["head"])], relief=[("pressed", "sunken")])
        w = (14 if self.modern else 16) * S
        for orient in ("Vertical", "Horizontal"):
            name = f"{orient}.TScrollbar"
            if self.modern:  # a slim bar with no arrows
                s.layout(name, [(f"{orient}.Scrollbar.trough", {"sticky": "nswe", "children": [
                    (f"{orient}.Scrollbar.thumb", {"expand": "1", "sticky": "nswe"})]})])
                s.configure(name, background=c["thumb"], troughcolor=c["trough"], bordercolor=c["trough"],
                            lightcolor=c["thumb"], darkcolor=c["thumb"], gripcount=0, width=w, arrowsize=w,
                            relief="flat", borderwidth=3 * S)
                s.map(name, background=[("active", c["text"])], lightcolor=[("active", c["text"])],
                      darkcolor=[("active", c["text"])])
            else:
                s.configure(name, background=c["thumb"], troughcolor=c["trough"], bordercolor=c["dark"],
                            lightcolor=c["light"], darkcolor=c["shadow"], arrowcolor=c["dark"], gripcount=0,
                            arrowsize=w, width=w, relief="raised")
                s.map(name, background=[("active", c["thumb"])])
        return s
