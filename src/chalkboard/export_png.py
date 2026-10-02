"""1920x1080 PNG "board slide" for a classroom display (interactive display, projector, TV).

The slide is laid out with the same dependency-free PDF engine as the printed
lesson plan, then rasterized by a tool the OS already has: `sips` on macOS
(always present), or poppler's pdftoppm / mutool / Ghostscript on Linux.
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile

from .export_pdf import B, I, R, Canvas, Fonts, _write

W, H = 1920, 1080
PAD = 64           # outer margin
GAP = 40           # space between columns and between panels
INSET = 30         # panel padding
LOGO_H, LOGO_W = 150, 380  # the school logo fits in this box in the header
STYLES = {
    "chalk": {"bg": (0.122, 0.231, 0.188), "panel": (0.165, 0.290, 0.239), "ink": (0.965, 0.953, 0.914),
              "label": (0.976, 0.824, 0.420), "dim": (0.690, 0.773, 0.729)},
    "white": {"bg": (1, 1, 1), "panel": (0.937, 0.953, 0.945), "ink": (0.098, 0.098, 0.098),
              "label": (0.075, 0.420, 0.298), "dim": (0.400, 0.420, 0.410)},
}
# Relative text size per section; standards shrink first when space is tight.
WEIGHT = {"targets": 1.12, "bell_ringer": 1.0, "success": 0.85, "materials": 0.85,
          "homework": 0.85, "closure": 0.85, "standards": 0.78}


def _rgb(hexcode):
    h = (hexcode or "").lstrip("#")
    return tuple(int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)) if len(h) == 6 else None


def _lum(c):
    lin = [v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4 for v in c]
    return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]


def _contrast(a, b):
    hi, lo = sorted((_lum(a), _lum(b)), reverse=True)
    return (hi + 0.05) / (lo + 0.05)


def _mix(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def school_palette(primary="", secondary="", text=""):
    """Slide colors from school colors: background, headings, and body text.

    Any color left blank is picked automatically: text is white or near-black,
    whichever reads better on the background, and headings match the text.
    Colors that are set are always used as given.
    """
    bg = _rgb(primary)
    accent = _rgb(secondary)
    ink = _rgb(text)
    if not bg:
        base = dict(STYLES["chalk"])
        if ink:
            base["ink"] = ink
            base["dim"] = _mix(base["bg"], ink, 0.72)
        if accent:
            base["label"] = accent
        return base
    auto = max([(1, 1, 1), (0.098, 0.098, 0.098)], key=lambda c: _contrast(c, bg))
    ink = ink or auto
    panel = _mix(bg, auto, 0.10)
    return {"bg": bg, "panel": panel, "ink": ink, "label": accent or ink, "dim": _mix(bg, ink, 0.72)}


def low_contrast(st):
    """Names of the set slide colors that will be hard to read on the background."""
    bg = school_palette(st.get("primary_color"))["panel"]
    return [name for key, name in (("secondary_color", "HEADINGS"), ("text_color", "TEXT"))
            if _rgb(st.get(key)) and _contrast(_rgb(st[key]), bg) < 3]


class BoardError(RuntimeError):
    pass


def lay(blocks, f, size, width):
    """Lay out text blocks; returns (height, [(dx, baseline_dy, text, style)])."""
    out, y = [], 0.0
    lead = size * 1.24
    prev = None
    for b in blocks:
        if prev:
            y += size * (0.22 if b["t"] == prev == "bullet" else 0.55)
        prev = b["t"]
        if b["t"] == "bullet":
            ind = size * (1.05 + 1.05 * b.get("indent", 0))
            lines = f.wrap(b["text"], R, size, width - ind)
            out.append((ind - size * 0.8, y + size, "•", R))
            out += [(ind, y + size + i * lead, ln, R) for i, ln in enumerate(lines)]
        elif b["t"] == "kv":
            label = b["label"] + "  "
            lw = f.width(label, B, size)
            lines = f.wrap(b["text"], R, size, width, first_width=width - lw) if b["text"] else [""]
            out.append((0, y + size, label.strip(), B))
            out += [(lw if i == 0 else 0, y + size + i * lead, ln, R) for i, ln in enumerate(lines)]
        else:
            lines = f.wrap(b["text"], R, size, width)
            out += [(0, y + size + i * lead, ln, R) for i, ln in enumerate(lines)]
        y += size + (len(lines) - 1) * lead + size * 0.28  # room for descenders
    return y, out


def label_size(s):
    return max(20.0, min(34.0, s * 0.62))


def fit_column(sections, f, width, height):
    """Largest scale where every panel fits; returns (scale, [(section, size, layout_h, items)])."""
    inner = width - 2 * INSET
    plan = None
    for s in range(64, 13, -1):
        for std in (1.0, 0.85):
            plan, total = [], GAP * (len(sections) - 1)
            for sec in sections:
                size = s * WEIGHT.get(sec["key"], 0.9) * (std if sec["key"] == "standards" else 1)
                h, items = lay(sec["blocks"], f, size, inner)
                plan.append((sec, size, h, items))
                total += 2 * INSET + label_size(s) * 1.5 + h
            if total <= height:
                return s, plan
    return 14, plan  # too much text even at the smallest size; panels clip


def draw_column(c, f, sections, x, top, width, height, col):
    if not sections:
        return
    s, plan = fit_column(sections, f, width, height)
    ls = label_size(s)
    need = [2 * INSET + ls * 1.5 + h for _, _, h, _ in plan]
    spare = max(0.0, height - GAP * (len(plan) - 1) - sum(need)) / len(plan)
    y = top
    for (sec, size, _, items), h in zip(plan, need):
        ph = min(h + spare, y - (top - height))
        c.fill(x, y - ph, width, ph, col["panel"])
        c.fill(x, y - ph, 8, ph, col["label"])
        c.text(x + INSET, y - INSET - ls * 0.85, sec["label"].upper(), B, ls, col["label"])
        body = y - INSET - ls * 1.5
        c.clip(x, y - ph, width, ph)
        for dx, dy, text, style in items:
            c.text(x + INSET + dx, body - dy, text, style, size, col["ink"])
        c.unclip()
        y -= ph + GAP


def board_pdf(doc, path):
    f = Fonts("Helvetica")
    if doc.get("style") == "school":
        col = school_palette(*doc.get("colors", ()))
    else:
        col = STYLES.get(doc.get("style"), STYLES["chalk"])
    c = Canvas()
    c.fill(0, 0, W, H, col["bg"])

    # header: [logo] title on the left, date + course/unit on the right [or logo]
    logo, lw, lh = doc.get("logo"), 0, 0
    if logo:
        k = min(LOGO_H / logo["h"], LOGO_W / logo["w"])
        lw, lh = logo["w"] * k, logo["h"] * k
    on_left = doc.get("logo_place", "left") != "right"
    x_title = PAD + (lw + GAP if logo and on_left else 0)
    x_right = W - PAD - (lw + GAP if logo and not on_left else 0)
    right = [(doc["date"], B, 46, col["label"]), (doc["meta"], I, 28, col["dim"])]
    right = [r for r in right if r[0]]
    rw = max([f.width(t, st, sz) for t, st, sz, _ in right] + [0])
    tw = x_right - x_title - (rw + GAP if rw else 0)
    for ts in range(76, 39, -2):
        lines = f.wrap(doc["title"], B, ts, tw)
        if len(lines) == 1 or (ts <= 56 and len(lines) <= 2):
            break
    lines = lines[:2]
    y = H - PAD
    for i, ln in enumerate(lines):
        c.text(x_title, y - ts * 0.82 - i * ts * 1.1, ln, B, ts, col["ink"])
    title_h = ts * 0.82 + (len(lines) - 1) * ts * 1.1 + ts * 0.25
    ry = y
    for t, st, sz, color in right:
        ry -= sz * 0.95
        c.text(x_right - f.width(t, st, sz), ry, t, st, sz, color)
        ry -= sz * 0.35
    head_h = max(title_h, y - ry, lh)
    if logo:
        c.image(logo, PAD if on_left else W - PAD - lw, y - (head_h + lh) / 2, lw, lh)
    rule_y = y - head_h - 18
    c.line(PAD, rule_y, W - PAD, rule_y, 2.5, col["label"])

    bottom = PAD
    if doc.get("footer"):
        c.text(PAD, PAD - 30, doc["footer"], I, 22, col["dim"])
    top = rule_y - 30
    height = top - bottom
    left, rightcol = doc["left"], doc["right"]
    full = W - 2 * PAD
    if left and rightcol:
        lw = (full - GAP) * 0.56
        draw_column(c, f, left, PAD, top, lw, height, col)
        draw_column(c, f, rightcol, PAD + lw + GAP, top, full - lw - GAP, height, col)
    elif left or rightcol:
        draw_column(c, f, left or rightcol, PAD, top, full, height, col)
    else:
        msg = "Add a standard, I can statement, or bell ringer to fill this slide."
        c.text((W - f.width(msg, I, 34)) / 2, top - height / 2, msg, I, 34, col["dim"])
    _write([c], path, f, W, H, doc["title"])


def png_size(path):
    try:
        with open(path, "rb") as fh:
            head = fh.read(24)
    except OSError:
        return None
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", head[16:24])


def _rasterizers(pdf, png):
    tmp = os.path.dirname(png)
    if sys.platform == "darwin":
        yield ["sips", "-s", "format", "png", pdf, "--out", png]
        yield ["qlmanage", "-t", "-s", str(W), "-o", tmp, pdf], pdf + ".png"
    yield ["pdftoppm", "-png", "-singlefile", "-r", "72", "-scale-to-x", str(W), "-scale-to-y", str(H),
           pdf, png[:-4]]
    yield ["mutool", "draw", "-q", "-r", "72", "-o", png, pdf, "1"]
    for gs in ("gs", "gswin64c", "gswin32c"):
        yield [gs, "-q", "-dSAFER", "-dBATCH", "-dNOPAUSE", "-sDEVICE=png16m", "-r72",
               "-dTextAlphaBits=4", "-dGraphicsAlphaBits=4", "-sOutputFile=" + png, pdf]


def rasterize(pdf, png):
    tried = False
    for cmd in _rasterizers(pdf, png):
        cmd, made = (cmd if isinstance(cmd, tuple) else (cmd, png))
        exe = shutil.which(cmd[0])
        if not exe:
            continue
        tried = True
        try:
            subprocess.run([exe] + cmd[1:], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=60, check=True)
        except (OSError, subprocess.SubprocessError):
            continue
        size = png_size(made)
        if size and size != (W, H) and sys.platform == "darwin" and shutil.which("sips"):
            subprocess.run(["sips", "-z", str(H), str(W), made], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=60)
            size = png_size(made)
        if size == (W, H):
            return made
    if not tried:
        raise BoardError("PNG EXPORT NEEDS POPPLER (pdftoppm) OR GHOSTSCRIPT - INSTALL ONE AND TRY AGAIN")
    raise BoardError("COULD NOT CONVERT THE SLIDE TO PNG")


def render_png(doc, path, **_):
    with tempfile.TemporaryDirectory(prefix="chalkboard-") as tmp:
        pdf = os.path.join(tmp, "board.pdf")
        board_pdf(doc, pdf)
        shutil.copyfile(rasterize(pdf, os.path.join(tmp, "board.png")), path)
