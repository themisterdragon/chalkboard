"""Read a PNG or JPEG (a school logo) into something the PDF engine can draw, with no dependencies.

JPEGs go into the PDF as they are. PNGs are decoded here (every color type, bit depth, and
interlacing), shrunk if they're huge, and stored as compressed pixels plus a transparency mask.
"""

import base64
import struct
import zlib

MAX_SIDE = 800           # PNGs bigger than this are shrunk; the slide shows a logo ~140px tall
MAX_PIXELS = 36_000_000  # refuse anything this big before decoding it
ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]


class ImageError(ValueError):
    pass


def read_image(path):
    """A logo for the board slide: {"w", "h", "filter", "colors", "data", "alpha", "inverted"}."""
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError as e:
        raise ImageError(f"COULDN'T OPEN IT ({e.strerror or e})")
    if raw[:8] == b"\x89PNG\r\n\x1a\n":
        return read_png(raw)
    if raw[:2] == b"\xff\xd8":
        return read_jpeg(raw)
    raise ImageError("THAT ISN'T A PNG OR JPEG PICTURE")


# ------------------------------------------------------------------ JPEG --

def read_jpeg(raw):
    i, adobe = 2, False
    while i + 4 <= len(raw):
        if raw[i] != 0xFF:
            raise ImageError("THE JPEG IS DAMAGED")
        marker = raw[i + 1]
        if marker == 0xFF:  # fill byte
            i += 1
            continue
        if marker in (0x01, *range(0xD0, 0xD8)):
            i += 2
            continue
        (seglen,) = struct.unpack(">H", raw[i + 2:i + 4])
        if marker == 0xEE and raw[i + 4:i + 9] == b"Adobe":
            adobe = True
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            depth, h, w, comps = struct.unpack(">BHHB", raw[i + 4:i + 10])
            if marker in (0xC3, 0xC7, 0xCB, 0xCF) or depth != 8:
                raise ImageError("THAT KIND OF JPEG ISN'T SUPPORTED; SAVE IT AS A PNG")
            if comps not in (1, 3, 4) or not w or not h:
                raise ImageError("THE JPEG IS DAMAGED")
            return {"w": w, "h": h, "filter": "DCTDecode", "colors": comps, "data": raw, "alpha": None,
                    "inverted": comps == 4 and adobe}
        i += 2 + seglen
    raise ImageError("THE JPEG IS DAMAGED")


# ------------------------------------------------------------------- PNG --

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def _unfilter(data, pos, w, h, bpp, stride):
    """Undo PNG row filters for one (sub)image; returns (rows, next position)."""
    rows, prev = [], bytearray(stride)
    for _ in range(h):
        if pos + 1 + stride > len(data):
            raise ImageError("THE PNG IS CUT OFF")
        ft = data[pos]
        row = bytearray(data[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if ft == 1:
            for x in range(bpp, stride):
                row[x] = (row[x] + row[x - bpp]) & 255
        elif ft == 2:
            row = bytearray((a + b) & 255 for a, b in zip(row, prev))
        elif ft == 3:
            for x in range(stride):
                row[x] = (row[x] + ((row[x - bpp] if x >= bpp else 0) + prev[x]) // 2) & 255
        elif ft == 4:
            for x in range(stride):
                if x >= bpp:
                    row[x] = (row[x] + _paeth(row[x - bpp], prev[x], prev[x - bpp])) & 255
                else:
                    row[x] = (row[x] + prev[x]) & 255
        elif ft != 0:
            raise ImageError("THE PNG IS DAMAGED")
        rows.append(row)
        prev = row
    return rows, pos


def _samples(row, w, ch, depth):
    """One unfiltered row -> a list of w*ch samples (0-255; palette indexes stay as they are)."""
    n = w * ch
    if depth == 8:
        return row[:n]
    if depth == 16:
        return row[0:2 * n:2]
    per = 8 // depth
    mask = (1 << depth) - 1
    out = bytearray(n)
    for i in range(n):
        out[i] = (row[i // per] >> (8 - depth * (i % per + 1))) & mask
    return out


def read_png(raw):
    pos, idat, plte, trns = 8, [], None, None
    ihdr = None
    while pos + 8 <= len(raw):
        length, kind = struct.unpack(">I4s", raw[pos:pos + 8])
        body = raw[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body[:13])
        elif kind == b"PLTE":
            plte = body
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    if not ihdr or not idat:
        raise ImageError("THE PNG IS DAMAGED")
    w, h, depth, ctype, _, _, interlace = ihdr
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if not ch or depth not in (1, 2, 4, 8, 16) or not w or not h or (ctype == 3 and not plte):
        raise ImageError("THE PNG IS DAMAGED")
    if w * h > MAX_PIXELS:
        raise ImageError(f"THE PICTURE IS HUGE ({w}x{h}); SAVE A SMALLER COPY")
    try:
        data = zlib.decompress(b"".join(idat))
    except zlib.error:
        raise ImageError("THE PNG IS DAMAGED")

    colors = 1 if ctype in (0, 4) else 3
    pix = bytearray(w * h * colors)
    alpha = bytearray(b"\xff" * (w * h))
    scale = 255 // ((1 << depth) - 1) if depth < 8 and ctype != 3 else 1
    key = None
    if trns and ctype == 0 and len(trns) >= 2:
        key = (struct.unpack(">H", trns[:2])[0] >> (8 if depth == 16 else 0),)
    elif trns and ctype == 2 and len(trns) >= 6:
        key = tuple(v >> (8 if depth == 16 else 0) for v in struct.unpack(">HHH", trns[:6]))
    pal = [tuple(plte[i:i + 3]) for i in range(0, len(plte or b"") - 2, 3)]
    pal_a = list(trns or b"") + [255] * (len(pal) - len(trns or b""))

    bpp = max(1, ch * depth // 8)
    p = 0
    for x0, y0, dx, dy in (ADAM7 if interlace else [(0, 0, 1, 1)]):
        pw, ph = (w - x0 + dx - 1) // dx, (h - y0 + dy - 1) // dy
        if pw <= 0 or ph <= 0:
            continue
        rows, p = _unfilter(data, p, pw, ph, bpp, (pw * ch * depth + 7) // 8)
        s = b"".join(bytes(_samples(row, pw, ch, depth)) for row in rows)
        # split into color + alpha for this pass, all at once
        n = pw * ph
        if ctype == 3:
            if pal and max(s) >= len(pal):
                raise ImageError("THE PNG IS DAMAGED")
            lut = [bytes(c[i] for c in pal) + bytes(256 - len(pal)) for i in range(3)]
            planes = [s.translate(t) for t in lut]
            a = s.translate(bytes(pal_a) + bytes(256 - len(pal_a)))
        else:
            planes = [s[c::ch] for c in range(colors)]
            if scale != 1:
                t = bytes(min(255, v * scale) for v in range(256))
                planes = [pl.translate(t) for pl in planes]
            if ctype in (4, 6):
                a = s[ch - 1::ch]
            elif key is not None:
                kk = [v * scale for v in key]
                a = bytes(0 if all(planes[c][i] == kk[c] for c in range(colors)) else 255 for i in range(n))
            else:
                a = b"\xff" * n
        packed = bytearray(n * colors)
        for c in range(colors):
            packed[c::colors] = planes[c]
        if not interlace:
            pix, alpha = packed, bytearray(a)
            break
        for r in range(ph):
            y = y0 + r * dy
            for i in range(pw):
                at, j = y * w + x0 + i * dx, r * pw + i
                pix[at * colors:(at + 1) * colors] = packed[j * colors:(j + 1) * colors]
                alpha[at] = a[j]

    if max(w, h) > MAX_SIDE:
        pix, alpha, w, h = _shrink(pix, alpha, w, h, colors, -(-max(w, h) // MAX_SIDE))
    opaque = alpha.count(255) == len(alpha)
    return {"w": w, "h": h, "filter": "FlateDecode", "colors": colors, "data": zlib.compress(bytes(pix), 9),
            "alpha": None if opaque else zlib.compress(bytes(alpha), 9), "inverted": False}


def _shrink(pix, alpha, w, h, colors, k):
    """Shrink by k, averaging a few samples from each k x k block (weighted by opacity, so edges
    don't go dark). Byte slicing keeps it quick in pure Python."""
    nw, nh = w // k, h // k
    offs = sorted({k // 4, (3 * k) // 4})
    planes = [bytes(pix[c::colors]) for c in range(colors)]
    out, oa = bytearray(nw * nh * colors), bytearray(nw * nh)
    for ny in range(nh):
        pts = []  # (alpha samples, [channel samples]) for each sample point in this row of blocks
        for sy in offs:
            row = (ny * k + sy) * w
            for sx in offs:
                sl = slice(row + sx, row + sx + k * nw, k)
                pts.append((alpha[sl], [pl[sl] for pl in planes]))
        m = len(pts)
        for nx in range(nw):
            tot = sum(pt[0][nx] for pt in pts)
            j = ny * nw + nx
            oa[j] = tot // m
            if tot:
                for c in range(colors):
                    out[j * colors + c] = min(255, sum(pt[0][nx] * pt[1][c][nx] for pt in pts) // tot)
    return out, oa, nw, nh


# --------------------------------------------------------------- storage --

def to_json(img):
    d = dict(img)
    d["data"] = base64.b64encode(img["data"]).decode("ascii")
    d["alpha"] = base64.b64encode(img["alpha"]).decode("ascii") if img.get("alpha") else None
    return d


def from_json(d):
    try:
        img = dict(d)
        img["data"] = base64.b64decode(d["data"])
        img["alpha"] = base64.b64decode(d["alpha"]) if d.get("alpha") else None
        int(img["w"]), int(img["h"])
        if img["filter"] not in ("DCTDecode", "FlateDecode") or img["colors"] not in (1, 3, 4):
            raise ValueError
        return img
    except (KeyError, TypeError, ValueError):
        raise ImageError("THE SAVED LOGO IS DAMAGED")


def pdf_objects(img):
    """(image dict + stream, mask dict + stream or None) for the PDF writer."""
    space = {1: "/DeviceGray", 3: "/DeviceRGB", 4: "/DeviceCMYK"}[img["colors"]]
    decode = " /Decode [1 0 1 0 1 0 1 0]" if img.get("inverted") else ""
    head = (f"/Type /XObject /Subtype /Image /Width {img['w']} /Height {img['h']} /ColorSpace {space} "
            f"/BitsPerComponent 8 /Filter /{img['filter']}{decode}")
    mask = None
    if img.get("alpha"):
        mask = (f"<< /Type /XObject /Subtype /Image /Width {img['w']} /Height {img['h']} /ColorSpace /DeviceGray "
                f"/BitsPerComponent 8 /Filter /FlateDecode /Length {len(img['alpha'])} >>", img["alpha"])
    return head, img["data"], mask
