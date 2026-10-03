"""Chalkboard's own fonts, built from the pixel lettering in data/pixelfont.json.

Three looks from the same letters, each with regular, bold, italic, and bold italic:
  Chalkboard Pixel    square pixels, like the terminal app's logo
  Chalkboard Chalk    the same letters drawn in dusty, uneven chalk
  Chalkboard Marquee  round dots, like the lights on a theater sign

build() makes a complete TrueType file in memory (a fraction of a second), so nothing but the
lettering is stored. install_fonts() in fontlib writes them out for PowerPoint and Keynote.
"""

import json
import math
import pkgutil
import struct

# The package name from __name__: the Mac app's Python 3.9 leaves the usual attribute unset in a .pyz.
PACKAGE = __name__.rpartition(".")[0]

FAMILIES = {
    "Chalkboard Pixel": "square pixels, like the terminal app's logo",
    "Chalkboard Chalk": "dusty, hand-drawn chalk",
    "Chalkboard Marquee": "round dots, like a theater sign",
}
U = 100            # font units per pixel (1000 per em)
CAP = 7            # pixel rows from the top of a capital down to the baseline
SLANT = 0.2        # italic lean (about 11 degrees)
VERSION = "1.000"

_data = None
_built = {}


def lettering():
    global _data
    if _data is None:
        raw = json.loads(pkgutil.get_data(PACKAGE, "data/pixelfont.json").decode("utf-8"))
        glyphs = {ch: [r for r in rows.split()] for ch, rows in raw["glyphs"].items()}
        glyphs["ı"] = glyphs.get("ı") or [".", ".", "#", "#", "#", "#", "#"]
        for ch, recipe in raw.get("composed", {}).items():
            base, mark = recipe.split()
            rows = [r for r in glyphs[base]]
            acc = raw["accents"][mark].split()
            w = len(rows[0])
            if w < len(acc[0]):  # a dotless i: widen it so the accent fits over it
                pad = (len(acc[0]) - w) // 2
                rows = ["." * pad + r + "." * (len(acc[0]) - w - pad) for r in rows]
                w = len(acc[0])
            off = (w - len(acc[0])) // 2
            for i, a in enumerate(acc):
                rows[i] = rows[i][:off] + a + rows[i][off + len(a):]
            glyphs[ch] = rows
        for ch, other in raw.get("same", {}).items():
            glyphs[ch] = glyphs[other]
        bold = {ch: rows.split() for ch, rows in raw.get("bold", {}).items()}
        for ch, other in raw.get("same", {}).items():
            if other in bold:
                bold[ch] = bold[other]
        _data = (glyphs, bold)
    return _data[0]


def bold_letters():
    lettering()
    return _data[1]


def pixels(rows, bold, drawn=False):
    """Filled (column, row-from-top) cells; bold thickens every stroke one pixel to the right
    (drawn: these rows are a hand-drawn bold letter already)."""
    cells = {(x, y) for y, r in enumerate(rows) for x, c in enumerate(r) if c == "#"}
    if bold and not drawn:  # (but never into a one-pixel gap, so "m" and "w" don't fill in)
        cells |= {(x + 1, y) for x, y in cells if (x + 2, y) not in cells}
    return cells


def _hash(*k):
    h = 2166136261
    for v in k:
        for b in str(v).encode():
            h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h / 0xFFFFFFFF


def trace(cells, size):
    """Outlines of a set of grid cells (x right, y up, each size units square), merged into
    TrueType contours: outer edges clockwise, holes counterclockwise, no overlaps."""
    edges = {}
    for x, y in cells:
        x0, y0, x1, y1 = x * size, y * size, (x + 1) * size, (y + 1) * size
        if (x - 1, y) not in cells:
            edges.setdefault((x0, y0), []).append((x0, y1))   # left side, going up
        if (x, y + 1) not in cells:
            edges.setdefault((x0, y1), []).append((x1, y1))   # top, going right
        if (x + 1, y) not in cells:
            edges.setdefault((x1, y1), []).append((x1, y0))   # right side, going down
        if (x, y - 1) not in cells:
            edges.setdefault((x1, y0), []).append((x0, y0))   # bottom, going left
    contours = []
    while edges:
        start = next(iter(edges))
        pts, cur, prev_dir = [start], start, None
        while True:
            outs = edges[cur]
            if len(outs) > 1 and prev_dir:  # two cells touching at a corner: turn right, keep them apart
                def turn(p, d=prev_dir, c=cur):
                    nd = (p[0] - c[0], p[1] - c[1])
                    return d[0] * nd[1] - d[1] * nd[0]  # negative = a right turn
                outs.sort(key=turn)
            nxt = outs.pop(0)
            if not outs:
                del edges[cur]
            prev_dir = (nxt[0] - cur[0], nxt[1] - cur[1])
            cur = nxt
            if cur == start:
                break
            pts.append(cur)
        clean = []  # drop points in the middle of straight runs
        for i, p in enumerate(pts):
            a, b = pts[i - 1], pts[(i + 1) % len(pts)]
            if (p[0] - a[0]) * (b[1] - p[1]) - (p[1] - a[1]) * (b[0] - p[0]) != 0:
                clean.append(p)
        contours.append([(x, y, True) for x, y in clean])
    return contours


def outline(family, ch, rows, bold, drawn=False):
    """TrueType contours for one character: [[(x, y, on_curve)]] with the baseline at y=0."""
    cells = pixels(rows, bold, drawn)
    up = {(x, CAP - 1 - y) for x, y in cells}  # rows from the top -> y up from the baseline
    if family == "Chalkboard Marquee":
        out, r = [], U * 0.43
        for x, y in sorted(up):
            cx, cy = x * U + U / 2, y * U + U / 2
            ring = []
            for k in range(8):  # clockwise: 8 on-curve points with a control point between each
                a = -k * math.pi / 4
                ring.append((round(cx + r * math.cos(a)), round(cy + r * math.sin(a)), True))
                a2 = a - math.pi / 8
                rr = r / math.cos(math.pi / 8)
                ring.append((round(cx + rr * math.cos(a2)), round(cy + rr * math.sin(a2)), False))
            out.append(ring)
        return out
    if family == "Chalkboard Chalk":
        n = 4  # each pixel becomes 4x4 grains; some at the edges (and a few inside) are rubbed away
        grains = {(x * n + i, y * n + j) for x, y in up for i in range(n) for j in range(n)}
        # a pixel that only touches the rest of its letter at a corner (the middle of an M) stays whole
        thin = {(x, y) for x, y in up if sum((x + dx, y + dy) in up for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))) < 1}
        keep = set()
        for gx, gy in grains:
            edge = sum((gx + dx, gy + dy) not in grains for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            luck = _hash(ch, gx, gy, bold)
            if (gx // n, gy // n) not in thin and ((edge and luck < 0.36) or (not edge and luck < 0.04)):
                continue
            keep.add((gx, gy))
        return trace(keep, U // n)
    return trace(up, U)


def build(family, style=0):
    """A complete .ttf for one style (0 regular, 1 bold, 2 italic, 3 bold italic)."""
    if (family, style) in _built:
        return _built[(family, style)]
    bold, italic = bool(style & 1), bool(style & 2)
    glyphs = lettering()
    chars = sorted(glyphs)
    # glyph 0 is .notdef, an empty box
    box = [[(50, 0, True), (50, 700, True), (450, 700, True), (450, 0, True)],
           [(100, 50, True), (400, 50, True), (400, 650, True), (100, 650, True)]]
    shapes, advances = [box], [500]
    drawn = bold_letters() if bold else {}
    for ch in chars:
        rows = drawn.get(ch) or glyphs[ch]
        cs = outline(family, ch, rows, bold, ch in drawn) if ch.strip() else []
        if italic:
            cs = [[(round(x + y * SLANT), y, on) for x, y, on in c] for c in cs]
        shapes.append(cs)
        advances.append((len(rows[0]) + 1 + (1 if bold and ch.strip() and ch not in drawn else 0)) * U)
    font = _assemble(family, style, chars, shapes, advances)
    _built[(family, style)] = font
    return font


def _glyf(contours):
    if not contours:
        return b"", (0, 0, 0, 0)
    pts = [p for c in contours for p in c]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    bbox = (min(xs), min(ys), max(xs), max(ys))
    out = struct.pack(">hhhhh", len(contours), *bbox)
    end = -1
    for c in contours:
        end += len(c)
        out += struct.pack(">H", end)
    out += struct.pack(">H", 0)  # no hinting instructions
    out += bytes(1 if on else 0 for _, _, on in pts)
    px = py = 0
    xb, yb = b"", b""
    for x, y, _ in pts:
        xb += struct.pack(">h", x - px)
        yb += struct.pack(">h", y - py)
        px, py = x, y
    out += xb + yb
    return out + b"\0" * (-len(out) % 4), bbox


def _name_table(entries):
    recs, strings = b"", b""
    for nid, text in sorted(entries.items()):
        raw = text.encode("utf-16-be")
        recs += struct.pack(">6H", 3, 1, 0x409, nid, len(raw), len(strings))
        strings += raw
    return struct.pack(">HHH", 0, len(entries), 6 + len(recs)) + recs + strings


def _cmap(codes):
    """Format 4 cmap: {unicode: glyph}, one segment per run of consecutive codes and glyphs."""
    segs = []
    for code in sorted(codes):
        g = codes[code]
        if segs and code == segs[-1][1] + 1 and g == segs[-1][2] + (code - segs[-1][0]):
            segs[-1][1] = code
        else:
            segs.append([code, code, g])
    segs.append([0xFFFF, 0xFFFF, 0])
    n = len(segs)
    es = int(math.log2(n))
    ends = b"".join(struct.pack(">H", s[1]) for s in segs)
    starts = b"".join(struct.pack(">H", s[0]) for s in segs)
    deltas = b"".join(struct.pack(">H", (s[2] - s[0]) & 0xFFFF if s[0] != 0xFFFF else 1) for s in segs)
    ranges = b"\0\0" * n
    body = struct.pack(">HHHH", n * 2, 2 * (1 << es), es, 2 * n - 2 * (1 << es)) + ends + b"\0\0" + starts + deltas + ranges
    sub = struct.pack(">HHH", 4, 6 + len(body), 0) + body
    return struct.pack(">HH", 0, 2) + struct.pack(">HHI", 0, 3, 20) + struct.pack(">HHI", 3, 1, 20) + sub


def _assemble(family, style, chars, shapes, advances):
    bold, italic = bool(style & 1), bool(style & 2)
    sub = ("Regular", "Bold", "Italic", "Bold Italic")[style]
    ps = family.replace(" ", "") + "-" + sub.replace(" ", "")
    glyf, loca, bboxes = b"", [0], []
    for cs in shapes:
        g, bb = _glyf(cs)
        glyf += g
        loca.append(len(glyf))
        bboxes.append(bb)
    inked = [b for b, cs in zip(bboxes, shapes) if cs]
    xmin, ymin = min(b[0] for b in inked), min(b[1] for b in inked)
    xmax, ymax = max(b[2] for b in inked), max(b[3] for b in inked)
    asc, desc = 900, 250
    num = len(shapes)
    npts = max(sum(len(c) for c in cs) for cs in shapes)
    ncont = max(len(cs) for cs in shapes)
    codes = {ord(ch): i + 1 for i, ch in enumerate(chars)}
    t = {}
    mac_style = (1 if bold else 0) | (2 if italic else 0)
    t["head"] = struct.pack(">IIIIHHqqhhhhHHhhh", 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0x000B, 1000,
                            0, 0, xmin, ymin, xmax, ymax, mac_style, 8, 2, 1, 0)
    t["hhea"] = struct.pack(">IhhhHhhhhhhhhhhhH", 0x00010000, asc, -desc, 0, max(advances), min(b[0] for b in inked),
                            0, xmax, 100 if italic else 1, 20 if italic else 0, 0, 0, 0, 0, 0, 0, num)
    t["maxp"] = struct.pack(">IHHHHHHHHHHHHHH", 0x00010000, num, npts, ncont, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0)
    sel = (0x20 if bold else 0) | (0x01 if italic else 0) | (0x40 if not (bold or italic) else 0) | 0x80
    t["OS/2"] = struct.pack(">HhHHHhhhhhhhhhhh10sIIII4sHHHhhhHHIIhhHHH", 4, 500, 700 if bold else 400, 5, 0,
                            650, 600, 0, 75, 650, 600, 0, 350, 50, 300, 0, b"\0" * 10, 3, 0, 0, 0, b"CHLK", sel,
                            min(codes), min(max(codes), 0xFFFF), asc, -desc, 100, asc, desc, 1, 0, 500, 700, 0, 32, 0)
    t["hmtx"] = b"".join(struct.pack(">Hh", a, b[0]) for a, b in zip(advances, bboxes))
    t["cmap"] = _cmap(codes)
    t["loca"] = b"".join(struct.pack(">I", x) for x in loca)
    t["glyf"] = glyf
    t["name"] = _name_table({
        0: "Made for Chalkboard. Free to use and share under the SIL Open Font License 1.1.",
        1: family, 2: sub, 3: f"Chalkboard:{ps}:{VERSION}", 4: f"{family} {sub}" if style else family,
        5: f"Version {VERSION}", 6: ps, 13: "SIL Open Font License, Version 1.1",
        14: "https://openfontlicense.org"})
    angle = int(-math.degrees(math.atan(SLANT)) * 65536) & 0xFFFFFFFF if italic else 0
    t["post"] = struct.pack(">IIhhIIIII", 0x00030000, angle, -120, 60, 0, 0, 0, 0, 0)
    tags = sorted(t)
    n = len(tags)
    es = int(math.log2(n))
    out = struct.pack(">IHHHH", 0x00010000, n, (1 << es) * 16, es, n * 16 - (1 << es) * 16)
    pos, body, head_at = 12 + 16 * n, b"", 0
    for tag in tags:
        data = t[tag]
        if tag == "head":
            head_at = pos + len(body)
        out += struct.pack(">4sIII", tag.encode(), _sum(data), pos + len(body), len(data))
        body += data + b"\0" * (-len(data) % 4)
    font = bytearray(out + body)
    struct.pack_into(">I", font, head_at + 8, (0xB1B0AFBA - _sum(bytes(font))) & 0xFFFFFFFF)
    return bytes(font)


def _sum(b):
    b = b + b"\0" * (-len(b) % 4)
    return sum(struct.unpack(">%dI" % (len(b) // 4), b)) & 0xFFFFFFFF
