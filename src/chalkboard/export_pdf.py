"""Dependency-free PDF output using the standard Times/Helvetica fonts."""

import zlib

from .fontmetrics import WIDTHS

FAMILIES = {
    "Times": ("Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic"),
    "Helvetica": ("Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique"),
}
PAGES = {"Letter": (612, 792), "A4": (595, 842)}
R, B, I, BI = 0, 1, 2, 3
RED = (0.72, 0.06, 0.06)
GRAY = (0.45, 0.45, 0.45)
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
    def __init__(self, family):
        self.names = FAMILIES.get(family, FAMILIES["Times"])

    def width(self, s, style, size):
        w = WIDTHS[self.names[style]]
        return sum(w[c - 32] for c in enc(s) if c >= 32) * size / 1000

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


class Canvas:
    def __init__(self):
        self.ops = []

    def text(self, x, y, s, style=R, size=SIZE, color=None):
        if not s:
            return
        col = "%.3f %.3f %.3f rg " % color if color else ""
        self.ops.append(f"q {col}BT /F{style + 1} {size:.2f} Tf {x:.2f} {y:.2f} Td ({esc(enc(s))}) Tj ET Q")

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

    def ellipse(self, cx, cy, rx, ry, color=RED, lw=1.1):
        k = 0.5523
        o = [f"q {lw:.2f} w %.3f %.3f %.3f RG" % color, f"{cx + rx:.2f} {cy:.2f} m"]
        for (x1, y1, x2, y2, x3, y3) in [
            (cx + rx, cy + k * ry, cx + k * rx, cy + ry, cx, cy + ry),
            (cx - k * rx, cy + ry, cx - rx, cy + k * ry, cx - rx, cy),
            (cx - rx, cy - k * ry, cx - k * rx, cy - ry, cx, cy - ry),
            (cx + k * rx, cy - ry, cx + rx, cy - k * ry, cx + rx, cy),
        ]:
            o.append(f"{x1:.2f} {y1:.2f} {x2:.2f} {y2:.2f} {x3:.2f} {y3:.2f} c")
        o.append("S Q")
        self.ops.append(" ".join(o))


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

    def emit(b):
        t = b["t"]
        if t == "title":
            rows.extend(text_rows(f.wrap(b["text"], B, 17, width), x0, B, 17, "center", glue=True))
            rows.append(Row(2, None, glue=True))
        elif t == "subtitle":
            rows.extend(text_rows(f.wrap(b["text"], I, 10.5, width), x0, I, 10.5, "center", GRAY))
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
            rows.extend(text_rows(f.wrap(b["text"], B, 12.5, width), x0, B, 12.5, glue=True))

            def draw(c, top):
                c.line(x0, top - 2, x0 + width, top - 2, 0.7)
            rows.append(Row(7, draw, glue=True))
        elif t == "check":
            rows.append(space(6))
            for i, ln in enumerate(f.wrap(b["text"], B, 11.5, width - 18)):
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
            lines = f.wrap(b["text"], style, size, avail - hang, first_width=avail - lw) if b["text"] else [""]
            for i, ln in enumerate(lines):
                def draw(c, top, ln=ln, i=i):
                    base = top - size
                    x = x0 + indent
                    if i == 0 and t == "bullet":
                        c.text(x - 10, base, "•", R, size)
                    if i == 0 and label:
                        c.text(x, base, label.strip(), lstyle, size, color)
                        x += lw
                    c.text(x, base, ln, style, size, color)
                rows.append(Row(size * lead, draw, glue=(len(lines) <= 3 and i < len(lines) - 1)))
            rows.append(space(3 if t == "bullet" else 5))
        elif t == "q":
            text = b["text"] + ("  " + b["points"] if b.get("points") else "")
            for i, ln in enumerate(f.wrap(text, R, SIZE, width - QIND)):
                def draw(c, top, ln=ln, i=i):
                    base = top - SIZE
                    if i == 0:
                        c.text(x0, base, b["num"], B, SIZE)
                    c.text(x0 + QIND, base, ln, R, SIZE)
                rows.append(Row(SIZE * lead, draw, glue=True))
            rows.append(Row(3, None, glue=True))
        elif t == "choice":
            lx, tx = x0 + QIND + 6, x0 + QIND + 26
            for i, ln in enumerate(f.wrap(b["text"], R, SIZE, x0 + width - tx)):
                def draw(c, top, ln=ln, i=i):
                    base = top - SIZE
                    if i == 0:
                        c.text(lx, base, b["label"], R, SIZE, RED if b["correct"] else None)
                        if b["correct"]:
                            lw = f.width(b["label"], R, SIZE)
                            c.ellipse(lx + lw / 2 - 0.5, base + SIZE * 0.32, max(lw / 2 + 4, 8), SIZE * 0.68)
                    c.text(tx, base, ln, R, SIZE, RED if b["correct"] else None)
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
                ll = f.wrap(left, R, SIZE, lw - 40) if left else []
                rl = f.wrap(right, R, SIZE, rw - 18) if right else []
                h = max(len(ll), len(rl), 1) * SIZE * lead + 5

                def draw(c, top, i=i, ll=ll, rl=rl, left=left, right=right):
                    base = top - SIZE
                    if left:
                        c.line(x0 + QIND, base - 2, x0 + QIND + 30, base - 2, 0.6)
                        if b["key"]:
                            c.text(x0 + QIND + 11, base, b["key"][i], B, SIZE, RED)
                    for j, ln in enumerate(ll):
                        c.text(x0 + QIND + 38, base - j * SIZE * lead, ln, R, SIZE)
                    if right:
                        c.text(rx, base, f"{'ABCDEFGHIJ'[i] if i < 10 else '?'}.", B, SIZE)
                    for j, ln in enumerate(rl):
                        c.text(rx + 18, base - j * SIZE * lead, ln, R, SIZE)
                rows.append(Row(h, draw, glue=(i < n - 1)))
        elif t == "passage":
            rows.append(space(10))
            if b.get("title"):
                rows.extend(text_rows(f.wrap(b["title"], B, 12, width), x0, B, 12, "center", glue=True))
                rows.append(Row(4, None, glue=True))
            gutter = 30 if b.get("numbered", True) else 0
            count = 0
            paras = [p.strip() for p in (b.get("text") or "").split("\n") if p.strip()]
            for pi, para in enumerate(paras):
                lines = f.wrap(para, R, SIZE, width - gutter - 10, first_width=width - gutter - 28)
                for i, ln in enumerate(lines):
                    count += 1

                    def draw(c, top, ln=ln, i=i, n=count):
                        base = top - SIZE
                        if gutter and n % 5 == 0:
                            c.text(x0 + 4, base, str(n), R, 8, GRAY)
                        c.text(x0 + gutter + (18 if i == 0 else 0), base, ln, R, SIZE)
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
                    for ln in (f.wrap(prompt, I, 10.5, width - 20) if prompt.strip() else [])[:4]:
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

    font_ids = [add(f"<< /Type /Font /Subtype /Type1 /BaseFont /{name} /Encoding /WinAnsiEncoding >>".encode())
                for name in f.names]
    fonts = " ".join(f"/F{i + 1} {fid} 0 R" for i, fid in enumerate(font_ids))
    kids = []
    for c in pages:
        data = zlib.compress("\n".join(c.ops).encode("latin-1"))
        cid = add(b"<< /Length %d /Filter /FlateDecode >>\nstream\n" % len(data) + data + b"\nendstream")
        kids.append(add(f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {W} {H}] "
                        f"/Resources << /Font << {fonts} >> >> /Contents {cid} 0 R >>".encode()))
    objs[0] = b"<< /Type /Catalog /Pages 2 0 R >>"
    objs[1] = f"<< /Type /Pages /Kids [{' '.join(f'{k} 0 R' for k in kids)}] /Count {len(kids)} >>".encode()
    info = add(f"<< /Title ({esc(enc(title))}) /Creator (Chalkboard) /Producer (Chalkboard) >>".encode())

    out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
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
