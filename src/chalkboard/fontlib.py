"""Fonts for board slides: the standard PDF fonts, Chalkboard's own pixel fonts, and the fonts
installed on this computer.

A font setting is just a family name. "" means the board's usual Helvetica (Arial in the
slideshow); a name this computer doesn't have (say, a backup opened on another computer) falls
back to it too, so a board slide always comes out.
"""

import os
import sys

from . import ttf
from .export_pdf import WIDTHS, Fonts

DEFAULT = ""                     # the usual board font
STANDARD = {"Helvetica": "Helvetica", "Times": "Times"}   # built into every PDF reader
# the slideshow's name for a standard PDF font (metric-compatible, on every computer)
OFFICE_NAME = {"Helvetica": "Arial", "Times": "Times New Roman"}
EXTS = (".ttf", ".otf", ".ttc", ".otc")
_found = None


def font_dirs():
    home = os.path.expanduser("~")
    if sys.platform == "darwin":
        return ["/System/Library/Fonts", "/System/Library/Fonts/Supplemental", "/Library/Fonts",
                os.path.join(home, "Library", "Fonts")]
    if sys.platform == "win32":
        win = os.environ.get("WINDIR", r"C:\Windows")
        local = os.environ.get("LOCALAPPDATA", os.path.join(home, "AppData", "Local"))
        return [os.path.join(win, "Fonts"), os.path.join(local, "Microsoft", "Windows", "Fonts")]
    xdg = os.environ.get("XDG_DATA_HOME") or os.path.join(home, ".local", "share")
    dirs = [os.path.join(xdg, "fonts"), os.path.join(home, ".fonts")]
    for d in (os.environ.get("XDG_DATA_DIRS") or "/usr/local/share:/usr/share").split(":"):
        if d:
            dirs.append(os.path.join(d, "fonts"))
    return dirs


def system_fonts(refresh=False):
    """{family: {style: (path, index)}} for this computer's fonts; style is 0-3 (R, B, I, BI).
    Scanned once per run (a second or so on a computer with thousands of fonts)."""
    global _found
    if _found is not None and not refresh:
        return _found
    found, seen = {}, set()
    for top in font_dirs():
        for root, _, files in os.walk(top):
            for name in sorted(files):
                if not name.lower().endswith(EXTS):
                    continue
                path = os.path.join(root, name)
                real = os.path.realpath(path)
                if real in seen:
                    continue
                seen.add(real)
                try:
                    faces = ttf.scan(path)
                except (OSError, ValueError, IndexError, UnicodeError, Exception):  # noqa: B014 - any bad file
                    continue
                for index, family, bold, italic in faces:
                    found.setdefault(family, {}).setdefault(int(bold) + 2 * int(italic), (path, index))
    _found = {k: found[k] for k in sorted(found, key=str.lower) if 0 in found[k] or len(found[k]) == 1}
    return _found


def families():
    """Every font a teacher can pick, as (name, kind) with kind 'chalkboard', 'standard', or 'system'."""
    from .pixelfont import FAMILIES
    out = [(n, "chalkboard") for n in FAMILIES]
    out += [(n, "standard") for n in STANDARD]
    out += [(n, "system") for n in system_fonts() if n not in FAMILIES and n not in OFFICE_NAME.values()]
    return out


def label(name):
    return name or "Standard (Helvetica / Arial)"


def available(name):
    from .pixelfont import FAMILIES
    return not name or name in STANDARD or name in FAMILIES or name in system_fonts()


_faces = {}


def faces(name):
    """The four styles of a family: a list of 4 entries, each a standard PDF font name (str) or a
    ttf.Face, plus which styles are faked from another one: [(bold, italic)]."""
    from .pixelfont import FAMILIES, build
    if name in _faces:
        return _faces[name]
    out = None
    if not name or name in STANDARD or not available(name):
        std = {"Times": ("Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic")}.get(
            name, ("Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique"))
        out = (list(std), [(False, False)] * 4)
    elif name in FAMILIES:
        out = ([ttf.Face(build(name, s)) for s in range(4)], [(False, False)] * 4)
    else:
        styles = system_fonts()[name]
        loaded = {}
        for s, (path, index) in styles.items():
            try:
                loaded[s] = ttf.load(path, index)
            except (OSError, ValueError, KeyError, IndexError, Exception):  # noqa: B014
                pass
        if not loaded:
            out = faces("")
        else:
            got, fake = [], []
            for s in range(4):
                if s in loaded:
                    got.append(loaded[s])
                    fake.append((False, False))
                    continue
                # no real bold or italic in this family: draw it from the closest style
                near = next(k for k in (s & 1, s & 2, 0, 1, 2, 3) if k in loaded)
                got.append(loaded[near])
                fake.append((bool(s & 1) and not near & 1, bool(s & 2) and not near & 2))
            out = (got, fake)
    _faces[name] = out
    return out


def office_name(name):
    """The font name a slideshow asks PowerPoint / Keynote / Google Slides for."""
    if not name or not available(name):
        return "Arial"
    return OFFICE_NAME.get(name, name)


# -- putting Chalkboard's fonts on this computer, so PowerPoint and Keynote can use them too

def user_font_dir():
    home = os.path.expanduser("~")
    if sys.platform == "darwin":
        return os.path.join(home, "Library", "Fonts")
    if sys.platform == "win32":
        local = os.environ.get("LOCALAPPDATA", os.path.join(home, "AppData", "Local"))
        return os.path.join(local, "Microsoft", "Windows", "Fonts")
    xdg = os.environ.get("XDG_DATA_HOME") or os.path.join(home, ".local", "share")
    return os.path.join(xdg, "fonts", "chalkboard")


STYLE_NAMES = ("Regular", "Bold", "Italic", "Bold Italic")


def install_fonts():
    """Write every Chalkboard font into this user's font folder (no administrator needed).
    Returns the folder. Programs already open may need a restart to see them."""
    from .pixelfont import FAMILIES, build
    folder = user_font_dir()
    os.makedirs(folder, exist_ok=True)
    written = []
    for fam in FAMILIES:
        for s in range(4):
            fname = f"{fam.replace(' ', '')}-{STYLE_NAMES[s].replace(' ', '')}.ttf"
            path = os.path.join(folder, fname)
            with open(path, "wb") as fh:
                fh.write(build(fam, s))
            written.append((f"{fam} {STYLE_NAMES[s]}".replace(" Regular", ""), path))
    if sys.platform == "win32":  # per-user fonts are listed in the registry (Windows 10 1809 and up)
        try:
            import winreg
            key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows NT\CurrentVersion\Fonts")
            for title, path in written:
                winreg.SetValueEx(key, f"{title} (TrueType)", 0, winreg.REG_SZ, path)
            winreg.CloseKey(key)
        except OSError:
            pass
    global _found
    _found = None
    return folder


def fonts_installed():
    from .pixelfont import FAMILIES
    folder = user_font_dir()
    return all(os.path.exists(os.path.join(folder, f"{fam.replace(' ', '')}-Regular.ttf")) for fam in FAMILIES)


class BoardFonts(Fonts):
    """Fonts for a board slide: styles 0-3 are the text font (R, B, I, BI), 4-7 the heading font.
    Fonts that aren't standard PDF fonts are embedded in the PDF."""

    def __init__(self, text="", head=""):
        tf, tk = faces(text)
        hf, hk = faces(head) if head else (tf, tk)
        self.entries = tf + hf
        self.fakes = tk + hk
        self.office = [office_name(text)] * 4 + [office_name(head or text)] * 4
        self.tables = [WIDTHS[e] if isinstance(e, str) else e.widths for e in self.entries]
        self.names = [e if isinstance(e, str) else e.ps for e in self.entries]

    def pdf_fonts(self, add):
        import zlib
        made, ids = {}, []
        for e in self.entries:
            key = e if isinstance(e, str) else id(e)
            if key not in made:
                if isinstance(e, str):
                    made[key] = add(f"<< /Type /Font /Subtype /Type1 /BaseFont /{e} /Encoding /WinAnsiEncoding >>"
                                    .encode())
                else:
                    raw = e.file()
                    data = zlib.compress(raw)
                    kind = "/Subtype /OpenType " if e.cff else f"/Length1 {len(raw)} "
                    fid = add(f"<< {kind}/Filter /FlateDecode /Length {len(data)} >>\nstream\n".encode()
                              + data + b"\nendstream")
                    flags = 32 + (64 if e.italic_angle else 0)
                    desc = add(f"<< /Type /FontDescriptor /FontName /{e.ps} /Flags {flags} /FontBBox "
                               f"[{' '.join(map(str, e.bbox))}] /ItalicAngle {e.italic_angle} /Ascent {e.ascent} "
                               f"/Descent {e.descent} /CapHeight {e.cap} /StemV 80 "
                               f"/{'FontFile3' if e.cff else 'FontFile2'} {fid} 0 R >>".encode())
                    made[key] = add(f"<< /Type /Font /Subtype /{'Type1' if e.cff else 'TrueType'} /BaseFont /{e.ps} "
                                    f"/FirstChar 32 /LastChar 255 /Widths [{' '.join(map(str, e.widths))}] "
                                    f"/Encoding /WinAnsiEncoding /FontDescriptor {desc} 0 R >>".encode())
            ids.append(made[key])
        return ids
