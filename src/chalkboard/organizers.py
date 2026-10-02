"""Graphic organizers as plain shapes, so every exporter draws the same picture.

shapes() lays an organizer out in a w x h box (points, origin top-left, y down) and
returns a list of:

  {"k": "rect", "x", "y", "w", "h", "lw", "fill"}        fill: an (r, g, b) or None
  {"k": "oval", "cx", "cy", "rx", "ry", "lw", "fill"}
  {"k": "line", "pts": [(x, y), ...], "lw", "arrow"}     arrow: a head on the last point
  {"k": "text", "x", "y", "w", "text", "bold", "size", "align"}   y is the top of the text

Later shapes are drawn over earlier ones; a filled shape hides what is under it.
"""

import math

from .store import FRAYER_HEADS, PLOT_HEADS

HEAD_FILL = (0.86, 0.86, 0.86)
WHITE = (1, 1, 1)
LINE_GAP = 25   # one writing line, about 1/3 inch


def clamp(v, lo, hi):
    try:
        v = int(v)
    except (TypeError, ValueError):
        v = lo
    return max(lo, min(hi, v))


def heads_of(b, n, defaults=()):
    heads = list(b.get("heads") or [])
    defaults = list(defaults)
    return [(heads[i] if i < len(heads) and heads[i].strip() else (defaults[i] if i < len(defaults) else ""))
            for i in range(n)]


def table_cols(b):
    return max(clamp(b.get("cols"), 1, 8), len([h for h in b.get("heads") or []][:8]))


def table_rows(b):
    return max(clamp(b.get("rows"), 1, 20), len((b.get("side") or [])[:20]))


def has_heads(b):
    return any((h or "").strip() for h in b.get("heads") or []) or b.get("preset") == "tchart"


def auto_lines(b):
    """A sensible height, in writing lines, when the teacher leaves Size on Automatic."""
    lay = b.get("layout", "table")
    if lay == "table":
        return max(12, table_rows(b) * 3)
    if lay == "sequence":
        n = clamp(b.get("rows"), 2, 8)
        return 9 if n <= 3 else n * 4
    return {"venn": 15, "web": 18, "frayer": 18, "plot": 18}.get(lay, 15)


def text(x, y, w, s, bold=True, size=10.5, align="left"):
    return {"k": "text", "x": x, "y": y, "w": w, "text": s, "bold": bold, "size": size, "align": align}


def rect(x, y, w, h, lw=0.8, fill=None):
    return {"k": "rect", "x": x, "y": y, "w": w, "h": h, "lw": lw, "fill": fill}


def oval(cx, cy, rx, ry, lw=1.0, fill=None):
    return {"k": "oval", "cx": cx, "cy": cy, "rx": rx, "ry": ry, "lw": lw, "fill": fill}


def line(pts, lw=0.8, arrow=False):
    return {"k": "line", "pts": pts, "lw": lw, "arrow": arrow}


def shapes(b, w, h):
    return {"table": _table, "venn": _venn, "web": _web, "sequence": _sequence,
            "frayer": _frayer, "plot": _plot}.get(b.get("layout"), _table)(b, w, h)


def _table(b, w, h):
    cols, rows = table_cols(b), table_rows(b)
    heads = heads_of(b, cols)
    side = (b.get("side") or [])[:rows]
    sidecol = b.get("sidecol") or any(s.strip() for s in side)
    head = 24 if has_heads(b) else 0
    sw = w * (0.24 if cols <= 3 else 0.18) if sidecol else 0
    cw = (w - sw) / cols
    rh = (h - head) / rows
    out = []
    if head:
        out.append(rect(sw, 0, w - sw, head, 0, HEAD_FILL))
        for i, label in enumerate(heads):
            out.append(text(sw + i * cw + 4, 6, cw - 8, label, size=10.5 if cols <= 4 else 9.5, align="center"))
    for i in range(1, cols):
        out.append(line([(sw + i * cw, 0), (sw + i * cw, h)]))
    if sidecol:
        out.append(line([(sw, 0), (sw, h)], 1.0))
        for r in range(rows):
            label = side[r] if r < len(side) else ""
            if label.strip():
                out.append(text(5, head + r * rh + 5, sw - 10, label, size=10))
    for r in range(rows + 1):
        y = head + r * rh
        if 0 < y < h:
            out.append(line([(0, y), (w, y)], 1.0 if r == 0 else 0.6))
    out.append(rect(0, 0, w, h, 1.2))
    return out


def _venn(b, w, h):
    n = 3 if clamp(b.get("circles"), 2, 3) == 3 else 2
    heads = heads_of(b, n + 1)
    top = 20   # label room above the circles
    out = []
    if n == 2:
        r = min((h - top - 4) / 2, w / 3.25)
        d = r * 1.15
        cx, cy = w / 2, top + r + 2
        centers = [(cx - d / 2, cy), (cx + d / 2, cy)]
        for (x, y) in centers:
            out.append(oval(x, y, r, r, 1.2))
        out.append(text(centers[0][0] - r, 2, r * 1.4, heads[0], size=11, align="center"))
        out.append(text(centers[1][0] - r * 0.4, 2, r * 1.4, heads[1], size=11, align="center"))
        if heads[2].strip():
            out.append(text(cx - r * 0.42, cy - r * 0.62, r * 0.84, heads[2], size=9.5, align="center"))
        return out
    r = min((h - 2 * top) / (2 + 0.866), w / 3.1)
    d = r * 1.05
    cx = w / 2
    y1 = top + r
    centers = [(cx - d / 2, y1), (cx + d / 2, y1), (cx, y1 + d * 0.866)]
    for (x, y) in centers:
        out.append(oval(x, y, r, r, 1.2))
    out.append(text(centers[0][0] - r * 1.1, 2, r * 1.4, heads[0], size=11, align="center"))
    out.append(text(centers[1][0] - r * 0.3, 2, r * 1.4, heads[1], size=11, align="center"))
    out.append(text(cx - r, centers[2][1] + r + 4, r * 2, heads[2], size=11, align="center"))
    if heads[3].strip():
        out.append(text(cx - r * 0.3, y1 + d * 0.27 - 6, r * 0.6, heads[3], size=8.5, align="center"))
    return out


def _edge(cx, cy, rx, ry, ang):
    """The point on an ellipse's edge in direction ang (radians) from its center."""
    c, s = math.cos(ang), math.sin(ang)
    t = 1 / math.sqrt((c / rx) ** 2 + (s / ry) ** 2)
    return cx + c * t, cy + s * t


def _web(b, w, h):
    n = clamp(b.get("rows"), 3, 8)
    heads = heads_of(b, n)
    cx, cy = w / 2, h / 2
    crx, cry = min(w * 0.17, 95), min(h * 0.13, 42)
    scale = 1 if n <= 4 else 0.86 if n <= 6 else 0.74
    brx, bry = min(w * 0.16, 92) * scale, min(h * 0.14, 50) * scale
    ring_x, ring_y = w / 2 - brx - 2, h / 2 - bry - 2
    out = []
    bubbles = []
    for i in range(n):
        ang = -math.pi / 2 + 2 * math.pi * i / n
        bx, by = cx + ring_x * math.cos(ang), cy + ring_y * math.sin(ang)
        to = math.atan2(by - cy, bx - cx)
        out.append(line([_edge(cx, cy, crx, cry, to), _edge(bx, by, brx, bry, to + math.pi)], 0.9))
        bubbles.append((bx, by, heads[i]))
    for bx, by, label in bubbles:
        out.append(oval(bx, by, brx, bry, 1.0, WHITE))
        if label.strip():
            out.append(text(bx - brx * 0.7, by - bry + 7, brx * 1.4, label, size=9.5, align="center"))
    out.append(oval(cx, cy, crx, cry, 1.6, WHITE))
    center = (b.get("center") or "").strip()
    if center:
        out.append(text(cx - crx * 0.8, cy - 7, crx * 1.6, center, size=12, align="center"))
    return out


def _sequence(b, w, h):
    n = clamp(b.get("rows"), 2, 8)
    heads = heads_of(b, n)
    gap = 26
    out = []
    across = n <= 3
    for i in range(n):
        if across:
            bw = (w - gap * (n - 1)) / n
            x, y, bh = i * (bw + gap), 0, h
        else:
            bw, bh = w, (h - gap * (n - 1)) / n
            x, y = 0, i * (bh + gap)
        out.append(rect(x, y, bw, bh, 1.0))
        label = heads[i].strip() or f"{i + 1}."
        out.append(text(x + 7, y + 6, bw - 14, label, size=10.5))
        if i < n - 1:
            if across:
                out.append(line([(x + bw + 4, h / 2), (x + bw + gap - 4, h / 2)], 1.4, arrow=True))
            else:
                out.append(line([(w / 2, y + bh + 3), (w / 2, y + bh + gap - 3)], 1.4, arrow=True))
    return out


def _frayer(b, w, h):
    heads = heads_of(b, 4, FRAYER_HEADS)
    out = [rect(0, 0, w, h, 1.2), line([(w / 2, 0), (w / 2, h)], 1.0), line([(0, h / 2), (w, h / 2)], 1.0)]
    rx, ry = min(w * 0.17, 100), min(h * 0.11, 44)
    for i, label in enumerate(heads):
        x, y = (i % 2) * w / 2, (i // 2) * h / 2
        if i == 3:  # the bottom-right label starts past the center oval
            out.append(text(w / 2 + rx + 8, y + 7, w / 2 - rx - 16, label, size=11))
        else:
            out.append(text(x + 8, y + 7, w / 2 - (rx if i == 2 else 0) - 16, label, size=11))
    out.append(oval(w / 2, h / 2, rx, ry, 1.4, WHITE))
    word = (b.get("center") or "").strip()
    if word:
        out.append(text(w / 2 - rx * 0.85, h / 2 - 8, rx * 1.7, word, size=13, align="center"))
    return out


def _plot(b, w, h):
    heads = heads_of(b, 5, PLOT_HEADS)
    base, peak = h * 0.6, h * 0.27
    pts = [(0, base), (w * 0.2, base), (w * 0.5, peak), (w * 0.8, base), (w, base)]
    boxes = [(0, h * 0.66, w * 0.48, h * 0.34),                   # exposition, under the left flat
             (0, h * 0.16, w * 0.25, h * 0.36),                   # rising action, left of the climb
             (w * 0.31, 0, w * 0.38, h * 0.22),                   # climax, over the peak
             (w * 0.75, h * 0.16, w * 0.25, h * 0.36),            # falling action
             (w * 0.52, h * 0.66, w * 0.48, h * 0.34)]            # resolution
    out = [line(pts, 2.0)]
    for (x, y, bw, bh), label in zip(boxes, heads):
        out.append(rect(x, y, bw, bh, 0.8))
        out.append(text(x + 6, y + 5, bw - 12, label, size=10))
    return out


# ---------------------------------------------------------------- plain text

def text_lines(b, width):
    """A rough plain-text version for the preview and the .txt export."""
    lay = b.get("layout", "table")
    out = []
    ind = "    "
    w = width - len(ind)
    if lay == "table":
        cols, rows = table_cols(b), table_rows(b)
        heads = heads_of(b, cols)
        side = (b.get("side") or [])
        sidecol = b.get("sidecol") or any(s.strip() for s in side)
        labels = ([""] if sidecol else []) + heads
        n = len(labels)
        cw = max(5, (w - n - 1) // n)
        sep = "+" + "+".join("-" * cw for _ in labels) + "+"
        out.append(ind + sep)
        if has_heads(b):
            out += [ind + "|" + "|".join(lab[:cw].center(cw) for lab in labels) + "|", ind + sep]
        for r in range(rows):
            first = [(side[r] if r < len(side) else "")[:cw - 1].ljust(cw)] if sidecol else []
            cells = first + [" " * cw] * cols
            out += [ind + "|" + "|".join(cells) + "|", ind + "|" + "|".join(" " * cw for _ in labels) + "|", ind + sep]
        return out
    if lay == "venn":
        n = 3 if clamp(b.get("circles"), 2, 3) == 3 else 2
        heads = [h or "ABC"[i] for i, h in enumerate(heads_of(b, n))]
        if n == 2:
            regions = [f"{heads[0]} only", "Both", f"{heads[1]} only"]
        else:
            a, b_, c = heads
            regions = [f"{a} only", f"{b_} only", f"{c} only", f"{a} & {b_}", f"{a} & {c}", f"{b_} & {c}",
                       "All three"]
        out.append(ind + "(VENN DIAGRAM)")
        for reg in regions:
            out += ["", ind + reg + ":", ind + "_" * w, ind + "_" * w]
        return out
    if lay == "web":
        n = clamp(b.get("rows"), 3, 8)
        out.append(ind + "(IDEA WEB)  Center: " + ((b.get("center") or "").strip() or "_" * 24))
        for i, label in enumerate(heads_of(b, n), 1):
            out += ["", ind + f"  {i}. {label}".rstrip(), ind + "     " + "_" * (w - 5)]
        return out
    if lay == "sequence":
        n = clamp(b.get("rows"), 2, 8)
        for i, label in enumerate(heads_of(b, n), 1):
            out += [ind + "+" + "-" * (w - 2) + "+", ind + "| " + (label or f"{i}.")[:w - 4].ljust(w - 3) + "|",
                    ind + "|" + " " * (w - 2) + "|", ind + "+" + "-" * (w - 2) + "+"]
            if i < n:
                out.append(ind + "v".center(w).rstrip())
        return out
    heads = heads_of(b, 4, FRAYER_HEADS) if lay == "frayer" else heads_of(b, 5, PLOT_HEADS)
    if lay == "frayer":
        out.append(ind + "(FRAYER MODEL)  Word: " + ((b.get("center") or "").strip() or "_" * 24))
    else:
        out.append(ind + "(PLOT DIAGRAM)")
    for label in heads:
        out += ["", ind + label + ":", ind + "_" * w, ind + "_" * w]
    return out
