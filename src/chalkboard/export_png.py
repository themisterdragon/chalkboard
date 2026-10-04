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

from .export_pdf import B, I, R, Canvas, _write
from .export_pptx import SlideCanvas, render_pptx
from .fontlib import BoardFonts
from .markup import plain

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
HB = 4 + B          # the heading font, bold (styles 4-7 are the heading font; see fontlib.BoardFonts)
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


_LAID = {}  # (id(blocks), size, width) -> lay() result; cleared for each slide deck


def lay(blocks, f, size, width):
    key = (id(blocks), size, width)
    if key not in _LAID:
        _LAID[key] = (_lay(blocks, f, size, width), blocks)  # holding blocks keeps its id from being reused
    return _LAID[key][0]


def _lay(blocks, f, size, width):
    """Lay out text blocks; returns (height, [(dx, baseline_dy, [(piece, style, underline)])])."""
    out, y = [], 0.0
    lead = size * 1.24
    prev = None
    for b in blocks:
        if prev:
            y += size * (0.22 if b["t"] == prev == "bullet" else 0.55)
        prev = b["t"]
        if b["t"] == "bullet":
            ind = size * (1.05 + 1.05 * b.get("indent", 0))
            lines = f.rich_wrap(b["text"], R, size, width - ind)
            out.append((ind - size * 0.8, y + size, [("•", R, False)]))
            out += [(ind, y + size + i * lead, ln) for i, ln in enumerate(lines)]
        elif b["t"] == "kv":
            label = b["label"] + "  "
            lw = f.width(label, B, size)
            lines = f.rich_wrap(b["text"], R, size, width, first_width=width - lw) if b["text"] else [[]]
            out.append((0, y + size, [(plain(label.strip()), B, False)]))
            out += [(lw if i == 0 else 0, y + size + i * lead, ln) for i, ln in enumerate(lines)]
        else:
            base = B if b.get("style") == "bold" else R
            lines = f.rich_wrap(b["text"], base, size, width)
            out += [(0, y + size + i * lead, ln) for i, ln in enumerate(lines)]
        y += size + (len(lines) - 1) * lead + size * 0.28  # room for descenders
    return y, out


def label_size(s):
    return max(20.0, min(34.0, s * 0.62))


def plan_column(sections, f, width, s):
    """Lay every panel out at base size s; returns (total height, plan). Standards shrink a bit first."""
    inner = width - 2 * INSET
    best = None
    for std in (1.0, 0.85):
        plan, total = [], GAP * (len(sections) - 1)
        for sec in sections:
            if sec.get("short") is sec["blocks"]:  # standards as codes only: short, so full size
                size = s
            else:
                size = s * WEIGHT.get(sec["key"], 0.9) * (std if sec["key"] == "standards" else 1)
            h, items = lay(sec["blocks"], f, size, inner)
            plan.append((sec, size, h, items))
            total += 2 * INSET + label_size(s) * 1.5 + h
        if best is None or total < best[0]:
            best = (total, plan)
    return best


def fit_column(sections, f, width, height, cap=64):
    """Largest base size (14..cap) where every panel fits; returns (size, plan)."""
    if not sections:
        return cap, []
    lo, hi = 14, cap
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if plan_column(sections, f, width, mid)[0] <= height:
            lo = mid
        else:
            hi = mid - 1
    return lo, plan_column(sections, f, width, lo)[1]


def split_columns(secs, f, full, height, layout="auto"):
    """Which panels go in which column, and how wide, for the biggest text both columns can share.
    Returns (size, left, right, left width).

    Panels keep their order. layout "auto": their sides are only the starting point (they win
    ties), so a column crowded with short panels hands some to the roomier one; "sides": every
    panel stays on its side; "one": a single column across the slide."""
    home = [1 if x.get("col") == "right" else 0 for x in secs]
    if len(secs) < 2 or layout == "one" or (layout == "sides" and len(set(home)) < 2):
        return fit_column(secs, f, full, height)[0], list(secs), [], full
    best = None
    masks = [sum(k << i for i, k in enumerate(home))] if layout == "sides" else range(1, (1 << len(secs)) - 1)
    for mask in masks:
        side = [(mask >> i) & 1 for i in range(len(secs))]
        a = [x for x, k in zip(secs, side) if not k]
        b = [x for x, k in zip(secs, side) if k]
        moved = sum(k != h for k, h in zip(side, home))
        for share in (0.56, 0.5, 0.62, 0.44):
            lw = (full - GAP) * share
            size = min(fit_column(a, f, lw, height)[0], fit_column(b, f, full - lw - GAP, height)[0])
            score = (size, -moved, share == 0.56)
            if best is None or score > best[0]:
                best = (score, a, b, lw)
    return best[0][0], best[1], best[2], best[3]


def draw_panel(c, x, y, w, h, col, ls, look):
    """A panel's background: a filled card with a colored edge, an outline, or plain chalk
    (nothing but a line under the heading)."""
    if look == "outline":
        for x1, y1, x2, y2 in ((x, y, x + w, y), (x, y + h, x + w, y + h), (x, y, x, y + h), (x + w, y, x + w, y + h)):
            c.line(x1, y1, x2, y2, 3, col["label"])
    elif look == "chalk":
        c.line(x + INSET, y + h - INSET - ls * 1.12, x + w - INSET, y + h - INSET - ls * 1.12, 2, col["dim"])
    else:
        c.fill(x, y, w, h, col["panel"])
        c.fill(x, y, 8, h, col["label"])


def draw_column(c, f, sections, x, top, width, height, col, cap=64, look="cards"):
    if not sections:
        return
    s, plan = fit_column(sections, f, width, height, cap)
    ls = label_size(s)
    need = [2 * INSET + ls * 1.5 + h for _, _, h, _ in plan]
    spare = max(0.0, height - GAP * (len(plan) - 1) - sum(need)) / len(plan)
    y = top
    for (sec, size, _, items), h in zip(plan, need):
        ph = min(h + spare, y - (top - height))
        draw_panel(c, x, y - ph, width, ph, col, ls, look)
        c.text(x + INSET, y - INSET - ls * 0.85, sec["label"].upper(), HB, ls, col["label"])
        body = y - INSET - ls * 1.5
        if getattr(c, "editable", False):  # the slideshow: one text box PowerPoint wraps itself
            c.blocks(x + INSET, body, width - 2 * INSET, body - (y - ph) - INSET / 2, sec["blocks"], size,
                     col["ink"], name=sec["label"])
            y -= ph + GAP
            continue
        c.clip(x, y - ph, width, ph)
        for dx, dy, line in items:
            xx = x + INSET + dx
            for piece, style, under in line:
                c.text(xx, body - dy, piece, style, size, col["ink"])
                pw = f.width(piece, style, size)
                if under:
                    c.line(xx, body - dy - size * 0.13, xx + pw, body - dy - size * 0.13, size * 0.06, col["ink"])
                xx += pw
        c.unclip()
        y -= ph + GAP


def draw_codes(c, f, sec, x_right, base, col, size=34, x_left=None):
    """Class codes on one line in a bottom corner: 'Class Code: abc123   Remind: @xyz'.
    Right-aligned to x_right, or left-aligned from x_left when it's given."""
    parts = []
    for b in sec["blocks"]:
        if b["t"] == "kv":
            parts.append((b["label"] + " ", b["text"]))
        else:
            parts.append(("", b["text"]))
    if len(parts) == 1 and not parts[0][0]:
        parts = [("Class Code: ", parts[0][1])]
    while size > 20:
        w = sum(f.width(lab, B, size) + f.width(txt, B, size) for lab, txt in parts) + 40 * (len(parts) - 1)
        if w <= W * 0.6:
            break
        size -= 2
    x = x_right - w if x_left is None else x_left
    if getattr(c, "editable", False):
        pieces = []
        for k, (lab, txt) in enumerate(parts):
            pieces += [("   " if k else "", R, size, col["dim"]), (lab, R, size, col["dim"]), (txt, B, size, col["label"])]
        c.textbox(x, base + size * 0.905, None, size * 1.2, [[p for p in pieces if p[0]]], name="Class codes")
        return size
    for lab, txt in parts:
        c.text(x, base, lab, R, size, col["dim"])
        x += f.width(lab, B, size)
        c.text(x, base, txt, B, size, col["label"])
        x += f.width(txt, B, size) + 40
    return size


def draw_banner(c, f, sec, x, top, width, col, look="cards"):
    """One full-width panel with big text (at most two lines); returns its height."""
    text = plain(" ".join(b["text"] for b in sec["blocks"]))
    inner = width - 2 * INSET
    for size in range(46, 25, -2):
        lines = f.wrap(text, B, size, inner)
        if len(lines) <= 2:
            break
    lines = lines[:2]
    ls = label_size(size)
    lead = size * 1.2
    ph = 2 * INSET + ls * 1.5 + size + (len(lines) - 1) * lead
    draw_panel(c, x, top - ph, width, ph, col, ls, look)
    c.text(x + INSET, top - INSET - ls * 0.85, sec["label"].upper(), HB, ls, col["label"])
    body = top - INSET - ls * 1.5
    if getattr(c, "editable", False):
        c.textbox(x + INSET, body - size * 0.9 + size * 0.905, inner, ph - 2 * INSET - ls * 1.5 + size * 0.3,
                  [[(text, B, size, col["ink"])]], name=sec["label"])
        return ph
    for i, ln in enumerate(lines):
        c.text(x + INSET, body - size * 0.9 - i * lead, ln, B, size, col["ink"])
    return ph


MIN_SIZE = 30   # smallest base text size that still reads from the back of a classroom
BIG_SIZE = 40   # ... when the teacher asks for extra-big text (more slides, bigger letters)


def codes_only(secs):
    return [dict(x, blocks=x["short"]) if x.get("short") else x for x in secs]


def plan_slides(secs, f, full, first_h, rest_h, layout="auto", least=MIN_SIZE):
    """Panels for each slide: [(size, left, right, left width)].

    When the text would be smaller than least, first standards drop to their codes, and only then
    do panels continue on another slide (in order, as many per slide as stay readable)."""
    if not secs:
        return [(0, [], [], full)]
    one = split_columns(secs, f, full, first_h, layout)
    if one[0] >= least:
        return [one]
    secs = codes_only(secs)
    one = split_columns(secs, f, full, first_h, layout)
    if one[0] >= least or len(secs) == 1:
        return [one]
    slides, height = [], first_h
    while secs:
        for k in range(len(secs), 0, -1):
            plan = split_columns(secs[:k], f, full, height, layout)
            if plan[0] >= least or k == 1:
                break
        slides.append(plan)
        secs, height = secs[k:], rest_h
    return slides


def draw_header(c, f, doc, col, note=""):
    """Background, logo, title, date and course; returns the y of the rule under it.

    Title on the left with the date and course on the right, or (title_align "center") the
    title centered with the date and course on a line under it."""
    c.fill(0, 0, W, H, col["bg"])
    logo, lw, lh = doc.get("logo"), 0, 0
    if logo:
        k = min(LOGO_H / logo["h"], LOGO_W / logo["w"])
        lw, lh = logo["w"] * k, logo["h"] * k
    on_left = doc.get("logo_place", "left") != "right"
    center = doc.get("title_align") == "center"
    x_title = PAD + (lw + GAP if logo and (on_left or center) else 0)
    x_right = W - PAD - (lw + GAP if logo and (not on_left or center) else 0)
    meta = "  |  ".join(x for x in (doc["meta"], note) if x)
    title = plain(doc["title"])
    y = H - PAD
    if center:
        right = []
        tw = x_right - x_title
    else:
        right = [(doc["date"], HB, 46, col["label"]), (meta, I, 28, col["dim"])]
        right = [r for r in right if r[0]]
        rw = max([f.width(t, st, sz) for t, st, sz, _ in right] + [0])
        tw = x_right - x_title - (rw + GAP if rw else 0)
    for ts in range(76, 39, -2):
        lines = f.wrap(title, HB, ts, tw)
        if len(lines) == 1 or (ts <= 56 and len(lines) <= 2):
            break
    lines = lines[:2]
    title_h = ts * 0.82 + (len(lines) - 1) * ts * 1.1 + ts * 0.25
    if getattr(c, "editable", False):
        c.textbox(x_title, y - ts * 0.82 + ts * 0.905, tw, title_h + ts * 0.3, [[(title, HB, ts, col["ink"])]],
                  name="Title", align="ctr" if center else "l")
    else:
        for i, ln in enumerate(lines):
            lx = x_title + (tw - f.width(ln, HB, ts)) / 2 if center else x_title
            c.text(lx, y - ts * 0.82 - i * ts * 1.1, ln, HB, ts, col["ink"])
    ry = y
    if center:
        sub = [(t, st, sz, colr) for t, st, sz, colr in ((doc["date"], HB, 36, col["label"]), (meta, I, 28, col["dim"]))
               if t]
        ry = y - title_h - 8
        if sub:
            gap = 36
            total = sum(f.width(t, st, sz) for t, st, sz, _ in sub) + gap * (len(sub) - 1)
            sx = x_title + (tw - total) / 2
            ry -= 36 * 0.95
            for t, st, sz, colr in sub:
                c.text(sx, ry, t, st, sz, colr)
                sx += f.width(t, st, sz) + gap
            ry -= 36 * 0.35
        head_h = max(y - ry, lh)
    else:
        for t, st, sz, color in right:
            ry -= sz * 0.95
            c.text(x_right - f.width(t, st, sz), ry, t, st, sz, color)
            ry -= sz * 0.35
        head_h = max(title_h, y - ry, lh)
    if logo:
        c.image(logo, PAD if on_left else W - PAD - lw, y - (head_h + lh) / 2, lw, lh)
    rule_y = y - head_h - 18
    if doc.get("panels") != "outline":
        c.line(PAD, rule_y, W - PAD, rule_y, 2.5, col["label"])
    return rule_y


def draw_footer(c, f, doc, codes, col):
    """Teacher/school in one bottom corner, class codes big in the other (bottom right unless
    codes_place is "left"); returns the bottom of the panel area."""
    left = doc.get("codes_place") == "left"
    foot = doc.get("footer")

    def footer(base):
        if foot:
            x = W - PAD - f.width(foot, I, 22) if left and codes else PAD
            c.text(x, base, foot, I, 22, col["dim"])
    if codes:  # a strip along the bottom, big enough to read from the back row
        size = draw_codes(c, f, codes, W - PAD, PAD - 8, col, x_left=PAD if left else None)
        footer(PAD - 8)
        return PAD + size + 4
    footer(PAD - 30)
    return PAD


def palette(doc):
    if doc.get("style") == "school":
        return school_palette(*doc.get("colors", ()))
    return STYLES.get(doc.get("style"), STYLES["chalk"])


def board_slides(doc, make=Canvas, fonts=None):
    """One slide's canvas, or more when the lesson has too much to read on one.
    make=SlideCanvas lays the same slides out as editable slideshow shapes.
    fonts: reuse another board's fonts, so several boards can go in one PDF."""
    _LAID.clear()
    f = fonts or BoardFonts(doc.get("font", ""), doc.get("head_font", ""))
    col = palette(doc)
    look, layout = doc.get("panels", "cards"), doc.get("layout", "auto")
    least = BIG_SIZE if doc.get("big_text") else MIN_SIZE
    secs = list(doc["left"]) + list(doc["right"])
    codes = next((x for x in secs if x["key"] == "class_codes"), None)
    secs = [x for x in secs if x is not codes]
    if doc.get("order"):  # the teacher's own order, across both sides
        rank = {k: i for i, k in enumerate(doc["order"])}
        secs.sort(key=lambda x: rank.get(x["key"], len(rank)))
    full = W - 2 * PAD

    def canvas():
        c = make()
        c.fakes, c.fonts = f.fakes, f
        return c

    # measure the header, footer, and Essential Question banner once on a scratch canvas
    scratch = canvas()
    top = draw_header(scratch, f, doc, col, "1 of 2") - 30
    bottom = draw_footer(scratch, f, doc, codes, col)
    rest_h = top - bottom
    for sec in doc.get("top") or []:
        top -= draw_banner(scratch, f, sec, PAD, top, full, col, look) + GAP
    slides = plan_slides(secs, f, full, top - bottom, rest_h, layout, least)

    out = []
    for n, (size, left, right, lw) in enumerate(slides):
        c = canvas()
        note = f"{n + 1} of {len(slides)}" if len(slides) > 1 else ""
        top = draw_header(c, f, doc, col, note) - 30
        bottom = draw_footer(c, f, doc, codes, col)
        if n == 0:
            for sec in doc.get("top") or []:  # the Essential Question: a banner across the whole slide
                top -= draw_banner(c, f, sec, PAD, top, full, col, look) + GAP
        height = top - bottom
        if left and right:
            draw_column(c, f, left, PAD, top, lw, height, col, size, look)
            draw_column(c, f, right, PAD + lw + GAP, top, full - lw - GAP, height, col, size, look)
        elif left or right:
            draw_column(c, f, left or right, PAD, top, full, height, col, look=look)
        elif not doc.get("top"):
            msg = "Add a standard, I can statement, or bell ringer to fill this slide."
            c.text((W - f.width(msg, I, 34)) / 2, top - height / 2, msg, I, 34, col["dim"])
        out.append(c)
    return f, out


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
    if sys.platform == "win32":  # Windows 10/11's own PDF renderer, so nothing extra has to be installed
        yield ["powershell", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
               "-EncodedCommand", _windows_script(pdf, png)]


def _windows_script(pdf, png):
    """PowerShell that renders page 1 of pdf to png with Windows.Data.Pdf (base64 UTF-16, as -EncodedCommand wants)."""
    import base64
    q = lambda p: "'" + p.replace("'", "''") + "'"  # noqa: E731 - a PowerShell string literal
    script = f"""
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
$null = [Windows.Data.Pdf.PdfDocument, Windows.Data.Pdf, ContentType = WindowsRuntime]
$ext = [System.WindowsRuntimeSystemExtensions].GetMethods()
$op1 = ($ext | ? {{ $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
                   $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' }})[0]
$act = ($ext | ? {{ $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
                   $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction' }})[0]
function Get($op, $type) {{ $t = $op1.MakeGenericMethod($type).Invoke($null, @($op)); $t.Wait() | Out-Null; $t.Result }}
$file = Get ([Windows.Storage.StorageFile]::GetFileFromPathAsync({q(pdf)})) ([Windows.Storage.StorageFile])
$doc = Get ([Windows.Data.Pdf.PdfDocument]::LoadFromFileAsync($file)) ([Windows.Data.Pdf.PdfDocument])
$dir = Get ([Windows.Storage.StorageFolder]::GetFolderFromPathAsync({q(os.path.dirname(png))})) ([Windows.Storage.StorageFolder])
$out = Get ($dir.CreateFileAsync({q(os.path.basename(png))}, [Windows.Storage.CreationCollisionOption]::ReplaceExisting)) ([Windows.Storage.StorageFile])
$stream = Get ($out.OpenAsync([Windows.Storage.FileAccessMode]::ReadWrite)) ([Windows.Storage.Streams.IRandomAccessStream])
$opts = New-Object Windows.Data.Pdf.PdfPageRenderOptions
$opts.DestinationWidth = {W}
$opts.DestinationHeight = {H}
$act.Invoke($null, @($doc.GetPage(0).RenderToStreamAsync($stream, $opts))).Wait() | Out-Null
$stream.Dispose()
"""
    return base64.b64encode(script.encode("utf-16-le")).decode("ascii")


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
                           stderr=subprocess.DEVNULL, timeout=60, check=True,
                           creationflags=0x08000000 if sys.platform == "win32" else 0)  # no console window flash
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
    """Write the slide to path; a lesson too full for one slide adds 'name 2.png', 'name 3.png'.
    Returns every file written."""
    f, slides = board_slides(doc)
    paths = [path] + [f"{path[:-4]} {i}.png" for i in range(2, len(slides) + 1)]
    with tempfile.TemporaryDirectory(prefix="chalkboard-") as tmp:
        for i, (c, out) in enumerate(zip(slides, paths)):
            pdf = os.path.join(tmp, f"board{i}.pdf")
            _write([c], pdf, f, W, H, doc["title"])
            shutil.copyfile(rasterize(pdf, os.path.join(tmp, f"board{i}.png")), out)
    for i in range(len(slides) + 1, 10):  # a slide left over from an earlier, fuller export
        old = f"{path[:-4]} {i}.png"
        if os.path.exists(old):
            os.remove(old)
    # the same slides as an editable slideshow for PowerPoint, Keynote, or Google Slides
    deck = path[:-4] + ".pptx"
    render_pptx(board_slides(doc, SlideCanvas)[1], deck, doc["title"], palette(doc)["ink"])
    return paths + [deck]


def render_day(docs, base, title="Day Slideshow"):
    """Several boards (a whole day's class periods, in order) as one slideshow, base + ".pptx", and the
    same slides as one PDF, base + ".pdf", for a display that only shows PDFs. Returns both paths."""
    if not docs:
        raise BoardError("NO LESSONS TO PUT IN THE SLIDESHOW")
    f, pages, shapes = None, [], []
    for doc in docs:
        f, slides = board_slides(doc, fonts=f)
        pages += slides
        shapes += board_slides(doc, SlideCanvas)[1]
    _write(pages, base + ".pdf", f, W, H, title)
    render_pptx(shapes, base + ".pptx", title, palette(docs[0])["ink"])
    return [base + ".pptx", base + ".pdf"]


def render_preview(doc, path):
    """Just the first slide, as a PNG at path, for the board designer's preview."""
    f, slides = board_slides(doc)
    with tempfile.TemporaryDirectory(prefix="chalkboard-") as tmp:
        pdf = os.path.join(tmp, "preview.pdf")
        _write(slides[:1], pdf, f, W, H, doc["title"])
        shutil.copyfile(rasterize(pdf, os.path.join(tmp, "preview.png")), path)
    return len(slides)
