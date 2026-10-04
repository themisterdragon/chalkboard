"""Dependency-free PDF output using the standard Times/Helvetica fonts."""

import re
import zlib

from .images import pdf_objects

from .fontmetrics import WIDTHS
from .markup import links, plain, runs
from .organizers import shapes

FAMILIES = {
    "Times": ("Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic"),
    "Helvetica": ("Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique"),
}
PAGES = {"Letter": (612, 792), "A4": (595, 842)}
R, B, I, BI = 0, 1, 2, 3
RED = (0.72, 0.06, 0.06)
GRAY = (0.45, 0.45, 0.45)
LINK = (0.02, 0.39, 0.76)
MARGIN = 54
SIZE = 11
LINE_GAP = 25      # writing-line spacing (~1/3 inch)
QIND = 22          # question text indent


# Characters in the standards that the standard PDF fonts (cp1252) can't draw.
SPELL = str.maketrans({"≤": "<=", "≥": ">=", "≠": "=/=", "−": "-", "θ": "theta", "π": "pi", "ⁿ": "^n",
                       "★": "*", "●": "•", "→": "->", "≈": "~"})


def enc(s):
    return s.replace("\t", " ").translate(SPELL).encode("cp1252", "replace")


def esc(b):
    out = []
    for c in b:
        if c in (40, 41, 92):
            out.append("\\" + chr(c))
        elif 32 <= c < 127:
            out.append(chr(c))
        else:
            out.append("\\%03o" % c)
    return "".join(out)


class Fonts:
    """The fonts of one document: the standard PDF fonts, styles R, B, I, BI.
    (fontlib.BoardFonts adds embedded fonts and a second family for headings.)"""
    fakes = None  # per style: (bold, italic) drawn by thickening or slanting another style

    def __init__(self, family):
        self.names = FAMILIES.get(family, FAMILIES["Times"])
        self.tables = [WIDTHS[n] for n in self.names]

    def width(self, s, style, size):
        w = self.tables[style]
        return sum(w[c - 32] for c in enc(s) if c >= 32) * size / 1000

    def pdf_fonts(self, add):
        """Write the font objects with add(bytes) -> object number; returns the numbers, F1, F2, ..."""
        return [add(f"<< /Type /Font /Subtype /Type1 /BaseFont /{name} /Encoding /WinAnsiEncoding >>".encode())
                for name in self.names]

    def wrap(self, text, style, size, width, first_width=None):
        lines, cur = [], ""
        limit = first_width if first_width is not None else width
        for word in text.split():
            trial = word if not cur else cur + " " + word
            if self.width(trial, style, size) <= limit:
                cur = trial
                continue
            if cur:
                lines.append(cur)
                limit = width
            cur = ""
            while self.width(word, style, size) > limit and len(word) > 1:
                k = len(word) - 1
                while k > 1 and self.width(word[:k], style, size) > limit:
                    k -= 1
                lines.append(word[:k])
                word = word[k:]
                limit = width
            cur = word
        if cur or not lines:
            lines.append(cur)
        return lines

    def rich_wrap(self, text, style, size, width, first_width=None):
        """Like wrap, for text with **bold**, *italic*, __underline__ marks.
        Returns lines of [(piece, style, underline)]; draw them with draw_rich."""
        words, cur = [], []   # a word is [(char, style, underline)]
        for piece, bold, italic, under in runs(text):
            st = mixed(style, bold, italic)
            for chunk in re.split(r"(\s+)", piece):
                if not chunk:
                    continue
                if chunk.isspace():
                    if cur:
                        words.append(cur)
                        cur = []
                    continue
                cur += [(c, st, under) for c in chunk]
        if cur:
            words.append(cur)

        def w(chars):
            return sum(self.width(c, st, size) for c, st, _ in chars)
        space = self.width(" ", R, size)
        lines, line = [], []
        limit = first_width if first_width is not None else width
        for word in words:
            trial = w(line) + (space if line else 0) + w(word)
            if trial <= limit:
                line += ([(" ", R, line[-1][2] and word[0][2])] if line else []) + word
                continue
            if line:
                lines.append(line)
                limit = width
            line = []
            while w(word) > limit and len(word) > 1:
                k = len(word) - 1
                while k > 1 and w(word[:k]) > limit:
                    k -= 1
                lines.append(word[:k])
                word = word[k:]
                limit = width
            line = word
        if line or not lines:
            lines.append(line)
        out = []
        for chars in lines:   # merge runs of one style back into pieces
            pieces = []
            for c, st, u in chars:
                if pieces and pieces[-1][1] == st and pieces[-1][2] == u:
                    pieces[-1][0] += c
                else:
                    pieces.append([c, st, u])
            out.append([tuple(p) for p in pieces])
        return out


def mixed(base, bold, italic):
    """A base style (R, B, I, BI, or a heading style, 4 more) with **bold** / *italic* marks added."""
    face, base = base - base % 4, base % 4
    return face + (B if bold or base in (B, BI) else R) + (I if italic or base in (I, BI) else 0)


class Canvas:
    fakes = None  # set from Fonts.fakes when a font is missing a bold or italic

    def __init__(self):
        self.ops = []
        self.images = {}  # name -> image from images.read_image
        self.links = []   # (x1, y1, x2, y2, url): clickable areas on this page

    def link(self, x, base, w, size, url):
        self.links.append((x, base - size * 0.25, x + w, base + size * 0.85, url))

    def image(self, img, x, y, w, h):
        name = self.images.setdefault(id(img), (f"Im{len(self.images) + 1}", img))[0]
        self.ops.append(f"q {w:.2f} 0 0 {h:.2f} {x:.2f} {y:.2f} cm /{name} Do Q")

    def text(self, x, y, s, style=R, size=SIZE, color=None):
        if not s:
            return
        col = "%.3f %.3f %.3f rg " % color if color else ""
        bold, italic = self.fakes[style] if self.fakes else (False, False)
        if bold:  # a font with no bold of its own: outline the letters too, in the same color
            col += ("%.3f %.3f %.3f RG " % color if color else "") + f"{size * 0.035:.2f} w 2 Tr "
        at = f"1 0 0.2 1 {x:.2f} {y:.2f} Tm" if italic else f"{x:.2f} {y:.2f} Td"
        self.ops.append(f"q {col}BT /F{style + 1} {size:.2f} Tf {at} ({esc(enc(s))}) Tj ET Q")

    def line(self, x1, y1, x2, y2, w=0.5, color=(0, 0, 0)):
        self.ops.append(f"q {w:.2f} w %.3f %.3f %.3f RG {x1:.2f} {y1:.2f} m {x2:.2f} {y2:.2f} l S Q" % color)

    def rect(self, x, y, w, h, lw=0.6):
        self.ops.append(f"q {lw:.2f} w {x:.2f} {y:.2f} {w:.2f} {h:.2f} re S Q")

    def fill(self, x, y, w, h, color):
        self.ops.append(f"q %.3f %.3f %.3f rg {x:.2f} {y:.2f} {w:.2f} {h:.2f} re f Q" % color)

    def clip(self, x, y, w, h):
        """Clip everything until unclip() to this rectangle."""
        self.ops.append(f"q {x:.2f} {y:.2f} {w:.2f} {h:.2f} re W n")

    def unclip(self):
        self.ops.append("Q")

    def path(self, pts, w=0.8, color=(0, 0, 0)):
        o = [f"q {w:.2f} w 1 j %.3f %.3f %.3f RG" % color, "%.2f %.2f m" % pts[0]]
        o += ["%.2f %.2f l" % p for p in pts[1:]]
        self.ops.append(" ".join(o + ["S Q"]))

    def polygon(self, pts, color=(0, 0, 0)):
        o = ["q %.3f %.3f %.3f rg" % color, "%.2f %.2f m" % pts[0]]
        o += ["%.2f %.2f l" % p for p in pts[1:]]
        self.ops.append(" ".join(o + ["h f Q"]))

    def ellipse(self, cx, cy, rx, ry, color=RED, lw=1.1, fill=None):
        k = 0.5523
        o = [f"q {lw:.2f} w %.3f %.3f %.3f RG" % color, f"{cx + rx:.2f} {cy:.2f} m"]
        if fill:
            o.insert(1, "%.3f %.3f %.3f rg" % fill)
        for (x1, y1, x2, y2, x3, y3) in [
            (cx + rx, cy + k * ry, cx + k * rx, cy + ry, cx, cy + ry),
            (cx - k * rx, cy + ry, cx - rx, cy + k * ry, cx - rx, cy),
            (cx - rx, cy - k * ry, cx - k * rx, cy - ry, cx, cy - ry),
            (cx + k * rx, cy - ry, cx + rx, cy - k * ry, cx + rx, cy),
        ]:
            o.append(f"{x1:.2f} {y1:.2f} {x2:.2f} {y2:.2f} {x3:.2f} {y3:.2f} c")
        o.append("B Q" if fill else "S Q")
        self.ops.append(" ".join(o))


def draw_shapes(c, f, items, x0, top):
    """Draw organizers.shapes() output with its top-left corner at (x0, top)."""
    for s in items:
        k = s["k"]
        if k == "rect":
            y = top - s["y"] - s["h"]
            if s["fill"]:
                c.fill(x0 + s["x"], y, s["w"], s["h"], s["fill"])
            if s["lw"]:
                c.rect(x0 + s["x"], y, s["w"], s["h"], s["lw"])
        elif k == "oval":
            c.ellipse(x0 + s["cx"], top - s["cy"], s["rx"], s["ry"], (0, 0, 0), s["lw"], s["fill"])
        elif k == "line":
            pts = [(x0 + x, top - y) for x, y in s["pts"]]
            if s["arrow"]:
                (ax, ay), (bx, by) = pts[-2], pts[-1]
                d = max(((bx - ax) ** 2 + (by - ay) ** 2) ** 0.5, 0.01)
                ux, uy = (bx - ax) / d, (by - ay) / d
                head = 4 + s["lw"] * 2.5
                pts[-1] = (bx - ux * head * 0.8, by - uy * head * 0.8)
                c.polygon([(bx, by), (bx - ux * head - uy * head * 0.55, by - uy * head + ux * head * 0.55),
                           (bx - ux * head + uy * head * 0.55, by - uy * head - ux * head * 0.55)])
            c.path(pts, s["lw"])
        elif k == "text":
            style, size = (B if s["bold"] else R), s["size"]
            for i, ln in enumerate(f.wrap(plain(s["text"]), style, size, s["w"])[:3]):
                x = x0 + s["x"]
                if s["align"] == "center":
                    x += (s["w"] - f.width(ln, style, size)) / 2
                c.text(x, top - s["y"] - size * (1 + 1.15 * i), ln, style, size)


class Row:
    """h is the row's height; a fill row stretches to the bottom of its page and its
    draw(c, top, h) is told the final height."""
    __slots__ = ("h", "draw", "glue", "droptop", "brk", "fill")

    def __init__(self, h, draw=None, glue=False, droptop=False, brk=False, fill=False):
        self.h, self.draw, self.glue, self.droptop, self.brk, self.fill = h, draw, glue, droptop, brk, fill


def build_rows(blocks, f, x0, width, body_h):
    rows = []
    lead = 1.3

    def text_rows(lines, x, style, size, align="left", color=None, glue=False):
        out = []
        for ln in lines:
            def draw(c, top, ln=ln):
                xx = x + (width - f.width(ln, style, size)) / 2 if align == "center" else x
                c.text(xx, top - size, ln, style, size, color)
            out.append(Row(size * lead, draw, glue))
        return out

    def space(h):
        return Row(h, None, droptop=True)

    def draw_rich(c, x, base, line, size, color=None):
        for piece, st, under in line:
            at = x
            for part, url in links(piece):  # web addresses: blue, underlined, and clickable
                w = f.width(part, st, size)
                if url:
                    c.text(at, base, part, st, size, LINK)
                    c.line(at, base - size * 0.13, at + w, base - size * 0.13, max(0.5, size * 0.055), LINK)
                    c.link(at, base, w, size, url)
                else:
                    c.text(at, base, part, st, size, color)
                at += w
            pw = f.width(piece, st, size)
            if under:
                c.line(x, base - size * 0.13, x + pw, base - size * 0.13, max(0.5, size * 0.055), color or (0, 0, 0))
            x += pw

    def emit(b):
        t = b["t"]
        if t == "title":
            rows.extend(text_rows(f.wrap(plain(b["text"]), B, 17, width), x0, B, 17, b.get("align", "center"), glue=True))
            rows.append(Row(2, None, glue=True))
        elif t == "subtitle":
            rows.extend(text_rows(f.wrap(plain(b["text"]), I, 10.5, width), x0, I, 10.5, b.get("align", "center"), GRAY))
            rows.append(space(8))
        elif t == "fields":
            parts = list(zip(b["items"], b.get("shares", (0.55, 0.27, 0.18))))

            def draw(c, top, parts=parts):
                x = x0 + (width - width * sum(s for _, s in parts)) / 2 if b.get("center") else x0
                base = top - 15
                for lab, share in parts:
                    seg = width * share
                    lw = f.width(lab + ":", B, 10.5)
                    c.text(x, base, lab + ":", B, 10.5)
                    c.line(x + lw + 4, base - 2, x + seg - 12, base - 2, 0.6)
                    x += seg
            rows.append(Row(24, draw))
            rows.append(space(6))
        elif t == "h1":
            rows.append(space(12))
            rows.extend(text_rows(f.wrap(plain(b["text"]), B, 12.5, width), x0, B, 12.5, glue=True))

            def draw(c, top):
                c.line(x0, top - 2, x0 + width, top - 2, 0.7)
            rows.append(Row(7, draw if b.get("rule", True) else None, glue=True))
        elif t == "check":
            rows.append(space(6))
            for i, ln in enumerate(f.wrap(plain(b["text"]), B, 11.5, width - 18)):
                def draw(c, top, ln=ln, i=i):
                    base = top - 11.5
                    if i == 0:
                        c.rect(x0, base - 1, 10, 10, 0.8)
                    c.text(x0 + 18, base, ln, B, 11.5)
                rows.append(Row(11.5 * lead + 2, draw, glue=True))
        elif t in ("p", "kv", "bullet", "answer"):
            style, size, color, indent = R, SIZE, None, 0
            if t == "p":
                st = b.get("style", "normal")
                style = {"italic": I, "bold": B, "small": I}.get(st, R)
                size = 9.5 if st == "small" else SIZE
                color = GRAY if st == "small" else None
            if t == "answer":
                style, color, indent = I, RED, QIND
            if t == "bullet":
                indent = 14 + 14 * b.get("indent", 0)
            indent += b.get("pad", 0)
            label = ""
            if t == "kv":
                label = b["label"] + " "
            elif t == "answer":
                label = "Answer: "
            lstyle = BI if t == "answer" else B
            lw = f.width(label, lstyle, size) if label else 0
            avail = width - indent
            hang = 0 if t != "bullet" else 0
            lines = f.rich_wrap(b["text"], style, size, avail - hang, first_width=avail - lw) if b["text"] else [[]]
            for i, ln in enumerate(lines):
                def draw(c, top, ln=ln, i=i):
                    base = top - size
                    x = x0 + indent
                    if i == 0 and t == "bullet":
                        c.text(x - 10, base, "•", R, size)
                    if i == 0 and label:
                        c.text(x, base, label.strip(), lstyle, size, color)
                        x += lw
                    draw_rich(c, x, base, ln, size, color)
                rows.append(Row(size * lead, draw, glue=(len(lines) <= 3 and i < len(lines) - 1)))
            rows.append(space(3 if t == "bullet" else 5))
        elif t == "q":
            text = b["text"] + ("  " + b["points"] if b.get("points") else "")
            for i, ln in enumerate(f.rich_wrap(text, R, SIZE, width - QIND)):
                def draw(c, top, ln=ln, i=i):
                    base = top - SIZE
                    if i == 0:
                        c.text(x0, base, b["num"], B, SIZE)
                    draw_rich(c, x0 + QIND, base, ln, SIZE)
                rows.append(Row(SIZE * lead, draw, glue=True))
            rows.append(Row(3, None, glue=True))
        elif t == "choice":
            lx, tx = x0 + QIND + 6, x0 + QIND + 26
            for i, ln in enumerate(f.rich_wrap(b["text"], R, SIZE, x0 + width - tx)):
                def draw(c, top, ln=ln, i=i):
                    base = top - SIZE
                    if i == 0:
                        c.text(lx, base, b["label"], R, SIZE, RED if b["correct"] else None)
                        if b["correct"]:
                            lw = f.width(b["label"], R, SIZE)
                            c.ellipse(lx + lw / 2 - 0.5, base + SIZE * 0.32, max(lw / 2 + 4, 8), SIZE * 0.68)
                    draw_rich(c, tx, base, ln, SIZE, RED if b["correct"] else None)
                rows.append(Row(SIZE * lead + 1, draw, glue=True))
            if b.get("last"):
                rows[-1].glue = False
        elif t == "choice_inline":
            def draw(c, top, b=b):
                base = top - SIZE
                for i, item in enumerate(b["items"]):
                    x = x0 + QIND + 6 + i * 90
                    hit = b["correct"] == i
                    c.text(x, base, item, R, SIZE, RED if hit else None)
                    if hit:
                        w = f.width(item, R, SIZE)
                        c.ellipse(x + w / 2, base + SIZE * 0.32, w / 2 + 7, SIZE * 0.72)
            rows.append(Row(SIZE * lead + 4, draw))
        elif t == "lines":
            for i in range(b["n"]):
                def draw(c, top):
                    c.line(x0 + QIND, top - LINE_GAP + 3, x0 + width, top - LINE_GAP + 3, 0.5, (0.3, 0.3, 0.3))
                rows.append(Row(LINE_GAP, draw, glue=(i == 0)))
        elif t == "blank":
            for i in range(b["n"]):
                rows.append(Row(LINE_GAP, None, glue=(i == 0)))
        elif t == "box":
            h = min(b["n"] * LINE_GAP, body_h - 60)

            def draw(c, top, h=h):
                c.rect(x0 + QIND, top - h + 2, width - QIND, h - 6)
            rows.append(Row(h, draw))
        elif t == "match":
            lw = (width - QIND) * 0.42
            rx = x0 + QIND + lw + 18
            rw = x0 + width - rx
            n = max(len(b["left"]), len(b["right"]))
            for i in range(n):
                left = b["left"][i] if i < len(b["left"]) else ""
                right = b["right"][i] if i < len(b["right"]) else ""
                ll = f.rich_wrap(left, R, SIZE, lw - 40) if left else []
                rl = f.rich_wrap(right, R, SIZE, rw - 18) if right else []
                h = max(len(ll), len(rl), 1) * SIZE * lead + 5

                def draw(c, top, i=i, ll=ll, rl=rl, left=left, right=right):
                    base = top - SIZE
                    if left:
                        c.line(x0 + QIND, base - 2, x0 + QIND + 30, base - 2, 0.6)
                        if b["key"]:
                            c.text(x0 + QIND + 11, base, b["key"][i], B, SIZE, RED)
                    for j, ln in enumerate(ll):
                        draw_rich(c, x0 + QIND + 38, base - j * SIZE * lead, ln, SIZE)
                    if right:
                        c.text(rx, base, f"{'ABCDEFGHIJ'[i] if i < 10 else '?'}.", B, SIZE)
                    for j, ln in enumerate(rl):
                        draw_rich(c, rx + 18, base - j * SIZE * lead, ln, SIZE)
                rows.append(Row(h, draw, glue=(i < n - 1)))
        elif t == "organizer":
            ow = width - QIND
            h = min(b["n"] * LINE_GAP, body_h - 60)

            def draw(c, top, h=h, b=b, ow=ow):
                draw_shapes(c, f, shapes(b, ow, h - 8), x0 + QIND, top - 4)
            rows.append(Row(h, draw, fill=True) if b.get("fill") else Row(h, draw))
        elif t == "passage":
            rows.append(space(10))
            if b.get("title"):
                rows.extend(text_rows(f.wrap(plain(b["title"]), B, 12, width), x0, B, 12, "center", glue=True))
                rows.append(Row(4, None, glue=True))
            gutter = 30 if b.get("numbered", True) else 0
            count = 0
            paras = [p.strip() for p in (b.get("text") or "").split("\n") if p.strip()]
            for para in paras:
                lines = f.rich_wrap(para, R, SIZE, width - gutter - 10, first_width=width - gutter - 28)
                for i, ln in enumerate(lines):
                    count += 1

                    def draw(c, top, ln=ln, i=i, n=count):
                        base = top - SIZE
                        if gutter and n % 5 == 0:
                            c.text(x0 + 4, base, str(n), R, 8, GRAY)
                        draw_rich(c, x0 + gutter + (18 if i == 0 else 0), base, ln, SIZE)
                    rows.append(Row(SIZE * 1.42, draw))
            rows.append(space(8))
        elif t == "grid":
            cols, n, head = b["cols"], b["rows"], 22

            def draw(c, top, h, cols=cols, n=n):
                rh = (h - head - 4) / n
                bottom_y = top - head - n * rh
                c.fill(x0, top - head, width, head, (0.86, 0.86, 0.86))
                x = x0
                for i, (label, share) in enumerate(cols):
                    cw = width * share
                    c.text(x + (cw - f.width(label, B, 11)) / 2, top - head + 7, label, B, 11)
                    if i:
                        c.line(x, top, x, bottom_y, 0.8)
                    x += cw
                for r in range(n + 1):
                    y = top - head - r * rh
                    c.line(x0, y, x0 + width, y, 0.8 if r == 0 else 0.6)
                c.rect(x0, bottom_y, width, top - bottom_y, 1.1)
            rows.append(Row(head + n * 28, draw, fill=True))
        elif t == "days":
            days, gap = b["days"], 10

            def draw(c, top, h, days=days):
                bh = (h - gap * (len(days) - 1)) / len(days)
                date_w = f.width("Date:", B, 10)
                for i, (label, prompt) in enumerate(days):
                    bt = top - i * (bh + gap)
                    c.rect(x0, bt - bh, width, bh, 0.9)
                    c.fill(x0, bt - 20, width, 20, (0.90, 0.90, 0.90))
                    c.line(x0, bt - 20, x0 + width, bt - 20, 0.6)
                    c.text(x0 + 8, bt - 14.5, label, B, 12)
                    c.text(x0 + width - 150, bt - 14, "Date:", B, 10)
                    c.line(x0 + width - 146 + date_w, bt - 16, x0 + width - 10, bt - 16, 0.6)
                    y = bt - 20
                    for ln in (f.wrap(plain(prompt), I, 10.5, width - 20) if prompt.strip() else [])[:4]:
                        y -= 10.5 * lead
                        c.text(x0 + 10, y + 2, ln, I, 10.5)
                    y -= LINE_GAP - 3
                    while y > bt - bh + 8:
                        c.line(x0 + 10, y, x0 + width - 10, y, 0.5, (0.45, 0.45, 0.45))
                        y -= LINE_GAP - 3
            rows.append(Row(len(days) * 70, draw, fill=True))
        elif t == "space":
            rows.append(space(b.get("h", 8)))
        elif t == "rule":
            def draw(c, top):
                c.line(x0, top - 4, x0 + width, top - 4, 0.5)
            rows.append(Row(8, draw))
        elif t == "pagebreak":
            rows.append(Row(0, brk=True))

    for b in blocks:
        emit(b)
    return rows


def render_pdf(doc, path, family="Times", page="Letter"):
    f = Fonts(family)
    W, H = PAGES.get(page, PAGES["Letter"])
    width = W - 2 * MARGIN
    top0, bottom = H - MARGIN, MARGIN + 12
    body_h = top0 - bottom
    rows = build_rows(doc["blocks"], f, MARGIN, width, body_h)

    pages = [Canvas()]
    y = top0
    for i, r in enumerate(rows):
        at_top = y == top0
        if r.brk:
            if not at_top:
                pages.append(Canvas())
                y = top0
            continue
        if r.droptop and at_top:
            continue
        run, j = r.h, i
        while rows[j].glue and j + 1 < len(rows) and run <= body_h:
            j += 1
            run += rows[j].h
        if run > body_h:  # can't keep it all together; only require this row to fit
            run = r.h
        if run > y - bottom and not at_top:
            pages.append(Canvas())
            y = top0
            if r.droptop:
                continue
        if r.fill:
            h = max(r.h, y - bottom)
            r.draw(pages[-1], y, h)
            y -= h
            continue
        if r.draw:
            r.draw(pages[-1], y)
        y -= r.h

    n = len(pages)
    for i, c in enumerate(pages, 1):
        if doc.get("footer"):
            c.text(MARGIN, MARGIN - 18, doc["footer"], I, 8.5, GRAY)
        label = f"Page {i} of {n}"
        c.text(W - MARGIN - f.width(label, R, 8.5), MARGIN - 18, label, R, 8.5, GRAY)
    _write(pages, path, f, W, H, doc.get("title", ""))
    return n


def _write(pages, path, f, W, H, title):
    objs = [None, None]

    def add(b):
        objs.append(b)
        return len(objs)

    font_ids = f.pdf_fonts(add)
    fonts = " ".join(f"/F{i + 1} {fid} 0 R" for i, fid in enumerate(font_ids))
    kids = []
    for c in pages:
        data = zlib.compress("\n".join(c.ops).encode("latin-1"))
        cid = add(b"<< /Length %d /Filter /FlateDecode >>\nstream\n" % len(data) + data + b"\nendstream")
        xobj = []
        for name, img in getattr(c, "images", {}).values():
            head, body, mask = pdf_objects(img)
            smask = ""
            if mask:  # (built outside the f-string: Python before 3.12 allows no backslash in one)
                mid = add(mask[0].encode() + b"\nstream\n" + mask[1] + b"\nendstream")
                smask = f" /SMask {mid} 0 R"
            iid = add(f"<< {head}{smask} /Length {len(body)} >>\nstream\n".encode() + body + b"\nendstream")
            xobj.append(f"/{name} {iid} 0 R")
        res = f"/Font << {fonts} >>" + (f" /XObject << {' '.join(xobj)} >>" if xobj else "")
        annots = [add(f"<< /Type /Annot /Subtype /Link /Rect [{x:.2f} {y:.2f} {x2:.2f} {y2:.2f}] /Border [0 0 0] "
                      f"/A << /S /URI /URI ({esc(enc(url))}) >> >>".encode())
                  for x, y, x2, y2, url in getattr(c, "links", [])]
        annot = f" /Annots [{' '.join(f'{a} 0 R' for a in annots)}]" if annots else ""
        kids.append(add(f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {W} {H}] "
                        f"/Resources << {res} >> /Contents {cid} 0 R{annot} >>".encode()))
    objs[0] = b"<< /Type /Catalog /Pages 2 0 R >>"
    objs[1] = f"<< /Type /Pages /Kids [{' '.join(f'{k} 0 R' for k in kids)}] /Count {len(kids)} >>".encode()
    info = add(f"<< /Title ({esc(enc(title))}) /Creator (Chalkboard) /Producer (Chalkboard) >>".encode())

    out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for i, o in enumerate(objs, 1):
        offsets.append(len(out))
        out += f"{i} 0 obj\n".encode() + o + b"\nendobj\n"
    xref = len(out)
    out += f"xref\n0 {len(objs) + 1}\n0000000000 65535 f \n".encode()
    for off in offsets:
        out += f"{off:010d} 00000 n \n".encode()
    out += f"trailer\n<< /Size {len(objs) + 1} /Root 1 0 R /Info {info} 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
    with open(path, "wb") as fh:
        fh.write(out)
