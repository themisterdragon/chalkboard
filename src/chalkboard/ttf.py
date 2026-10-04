"""Reading TrueType / OpenType fonts (.ttf, .otf, .ttc) just enough to lay out and embed them.

The PDF engine draws text one byte per character (Windows-1252, like the standard PDF fonts), so a
font only needs its name, its style, its glyph widths for those 224 characters, and the file
itself to embed. No glyph is ever touched: the PDF reader draws them from the embedded file.
"""

import struct

CP1252 = range(32, 256)


def _u16(b, o):
    return struct.unpack_from(">H", b, o)[0]


def _i16(b, o):
    return struct.unpack_from(">h", b, o)[0]


def _u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def faces_in(data):
    """Offsets of the fonts in a file: one for .ttf/.otf, several for a .ttc collection."""
    if data[:4] == b"ttcf":
        n = _u32(data, 8)
        return [_u32(data, 12 + 4 * i) for i in range(min(n, 64))]
    if data[:4] in (b"\x00\x01\x00\x00", b"OTTO", b"true"):
        return [0]
    return []


def _tables(data, off):
    n = _u16(data, off + 4)
    out = {}
    for i in range(n):
        tag, _, at, length = struct.unpack_from(">4sIII", data, off + 12 + 16 * i)
        if at + length > len(data):
            raise ValueError("font file is cut short")
        out[tag.decode("latin-1")] = (at, length)
    return out


def names(data, off=0):
    """{name id: text} from a font's name table (Windows English names preferred)."""
    t = _tables(data, off)
    if "name" not in t:
        return {}
    at, length = t["name"]
    return _names(data[at:at + length])


def _names(table):
    count, strings = _u16(table, 2), _u16(table, 4)
    found = {}
    for i in range(count):
        plat, encid, lang, nid, length, at = struct.unpack_from(">6H", table, 6 + 12 * i)
        raw = table[strings + at:strings + at + length]
        if plat == 3 and encid in (0, 1, 10):
            rank, text = (0 if lang == 0x409 else 1), raw.decode("utf-16-be", "replace")
        elif plat == 1 and encid == 0:
            rank, text = 2, raw.decode("mac_roman", "replace")
        elif plat == 0:
            rank, text = 3, raw.decode("utf-16-be", "replace")
        else:
            continue
        if nid not in found or rank < found[nid][0]:
            found[nid] = (rank, text.strip("\x00 "))
    return {k: v for k, (_, v) in found.items()}


def scan(path):
    """[(index, family, bold, italic)] for the usable fonts in a file, reading only the few tables
    it needs (a big .ttc of Chinese, Japanese, and Korean fonts is mostly glyphs we skip).

    Uses each font's style-linked family name (name id 1), which groups fonts in fours (regular,
    bold, italic, bold italic) like a word processor's font menu: "Noto Sans Light" is its own
    family next to "Noto Sans"."""
    out = []
    with open(path, "rb") as fh:
        head = fh.read(12 + 4 * 64)
        offs = faces_in(head)
        for i, off in enumerate(offs):
            fh.seek(off)
            top = fh.read(12)
            if len(top) < 12:
                break
            n = _u16(top, 4)
            dirs = fh.read(16 * n)
            t = {}
            for k in range(len(dirs) // 16):
                tag, _, at, length = struct.unpack_from(">4sIII", dirs, 16 * k)
                t[tag.decode("latin-1")] = (at, length)
            if not all(k in t for k in ("cmap", "hmtx", "hhea", "head", "name")):
                continue
            got = {}
            for tag in ("name", "OS/2", "head", "cmap"):
                if tag in t:
                    fh.seek(t[tag][0])
                    got[tag] = fh.read(min(t[tag][1], 1 << 20))
            if len(cmap_lookup(got["cmap"], 0, LATIN)) < len(LATIN):
                continue  # no English letters: a font for another writing system, or symbols
            info = _describe(got)
            if info:
                out.append((i,) + info)
    return out


def _describe(tables):
    n = _names(tables["name"])
    family = n.get(1)
    if not family or family.startswith("."):  # Apple's hidden system fonts start with a dot
        return None
    os2 = tables.get("OS/2")
    if os2 and len(os2) >= 64:
        if _u16(os2, 8) == 0x0002:
            return None  # its maker doesn't allow embedding it in documents
        sel = _u16(os2, 62)
        bold, italic = bool(sel & 0x20), bool(sel & 0x01)
    else:
        style = _u16(tables["head"], 44)
        bold, italic = bool(style & 1), bool(style & 2)
    sub = (n.get(2) or "").lower()
    return family, bold or "bold" in sub, italic or "italic" in sub or "oblique" in sub


class Face:
    """One font from a file: metrics for laying text out, and the bytes to embed in a PDF."""

    def __init__(self, data, off=0, name=""):
        t = self.t = _tables(data, off)
        self.data, self.off = data, off
        head, hhea = t["head"][0], t["hhea"][0]
        self.upm = _u16(data, head + 18) or 1000
        k = 1000 / self.upm
        self.bbox = [round(_i16(data, head + o) * k) for o in (36, 38, 40, 42)]
        self.ascent, self.descent = round(_i16(data, hhea + 4) * k), round(_i16(data, hhea + 6) * k)
        self.cap = self.ascent
        if "OS/2" in t:
            os2 = t["OS/2"][0]
            if _u16(data, os2) >= 2:
                self.cap = round(_i16(data, os2 + 88) * k) or self.ascent
        self.italic_angle = _i16(data, t["post"][0] + 4) if "post" in t else 0
        self.cff = "CFF " in t or "CFF2" in t
        n = names(data, off)
        ps = n.get(6) or name or n.get(4) or "Font"
        self.ps = "".join(c for c in ps if c.isalnum() or c == "-")[:60] or "Font"
        self.family = n.get(16) or n.get(1) or ps
        cmap = self._cmap()
        adv = self._advances()
        self.widths = []
        for b in CP1252:
            try:
                ch = ord(bytes([b]).decode("cp1252"))
            except UnicodeDecodeError:
                self.widths.append(0)
                continue
            g = cmap.get(ch, 0)
            self.widths.append(round(adv[min(g, len(adv) - 1)] * k) if adv else 500)

    def _advances(self):
        d, t = self.data, self.t
        count = _u16(d, t["hhea"][0] + 34)
        at = t["hmtx"][0]
        return [_u16(d, at + 4 * i) for i in range(count)]

    def _cmap(self):
        """{unicode: glyph} for the characters the PDF engine can draw."""
        want = set()
        for b in CP1252:
            try:
                want.add(ord(bytes([b]).decode("cp1252")))
            except UnicodeDecodeError:
                pass
        return cmap_lookup(self.data, self.t["cmap"][0], want)

    def file(self):
        """The font as a file of its own (one font out of a .ttc collection becomes a plain .ttf)."""
        if self.off == 0 and self.data[:4] != b"ttcf":
            return self.data
        tags = sorted(self.t)
        n = len(tags)
        es = max(k for k in range(16) if (1 << k) <= n)
        head = struct.pack(">4sHHHH", b"OTTO" if self.cff else b"\x00\x01\x00\x00", n,
                           (1 << es) * 16, es, n * 16 - (1 << es) * 16)
        body, pos, dirs = bytearray(), 12 + 16 * n, bytearray()
        for tag in tags:
            at, length = self.t[tag]
            chunk = self.data[at:at + length]
            dirs += struct.pack(">4sIII", tag.encode("latin-1"), _checksum(chunk), pos + len(body), length)
            body += chunk + b"\0" * (-length % 4)
        return bytes(head + dirs + body)


def _checksum(b):
    b = b + b"\0" * (-len(b) % 4)
    return sum(struct.unpack(">%dI" % (len(b) // 4), b)) & 0xFFFFFFFF


def cmap_lookup(d, base, want):
    """{unicode: glyph} for the characters in want, from the cmap table at d[base:]."""
    subs = {}
    for i in range(_u16(d, base + 2)):
        plat, encid, at = struct.unpack_from(">HHI", d, base + 4 + 8 * i)
        subs[(plat, encid)] = base + at
    for key in ((3, 10), (0, 4), (0, 6), (3, 1), (0, 3), (0, 2), (0, 1), (0, 0), (3, 0), (1, 0)):
        if key in subs and subs[key] + 8 <= len(d):
            out = _sub(d, subs[key], want, symbol=key == (3, 0))
            if out:
                return out
    return {}


def _sub(d, at, want, symbol=False):
    fmt = _u16(d, at)
    out = {}
    if fmt == 4:
        seg = _u16(d, at + 6) // 2
        ends, starts = at + 14, at + 16 + 2 * seg
        deltas, ranges = starts + 2 * seg, starts + 4 * seg
        for i in range(seg):
            end, start = _u16(d, ends + 2 * i), _u16(d, starts + 2 * i)
            delta, ro = _u16(d, deltas + 2 * i), _u16(d, ranges + 2 * i)
            for ch in want:
                c = (ch + 0xF000) if symbol else ch
                if start <= c <= end:
                    if ro == 0:
                        g = (c + delta) & 0xFFFF
                    else:
                        p = ranges + 2 * i + ro + 2 * (c - start)
                        g = _u16(d, p) if p + 2 <= len(d) else 0
                        g = (g + delta) & 0xFFFF if g else 0
                    if g:
                        out[ch] = g
    elif fmt == 12:
        for i in range(_u32(d, at + 12)):
            start, end, g0 = struct.unpack_from(">III", d, at + 16 + 12 * i)
            for ch in want:
                if start <= ch <= end:
                    out[ch] = g0 + ch - start
    elif fmt == 0:
        for ch in want:
            if ch < 256 and d[at + 6 + ch]:
                out[ch] = d[at + 6 + ch]
    return out


LATIN = {ord(c) for c in "AEaez09"}


def load(path, index=0):
    with open(path, "rb") as fh:
        data = fh.read()
    offs = faces_in(data)
    if not offs:
        raise ValueError("not a TrueType or OpenType font")
    return Face(data, offs[min(index, len(offs) - 1)])
