"""Chalkboard's logo: a 16x16 pixel-art chalkboard, plus a dependency-free PNG writer for it."""

import struct
import zlib

INK = {"k": "#000000", "w": "#FFFFFF", "n": "#1F5A3A", "t": "#6B4423", "T": "#A87040"}

LOGO = [
    "................",
    "tttttttttttttttt",
    "tTTTTTTTTTTTTTTt",
    "tTnnnnnnnnnnnnTt",
    "tTnnwnnnnnnnnnTt",
    "tTnwnwnnwnnnnnTt",
    "tTnwwwnnwwwnnnTt",
    "tTnwnwnnwnnwnnTt",
    "tTnwnwnnwwwnnnTt",
    "tTnnnnnnnnnnnnTt",
    "tTnnnnnnnnnnnnTt",
    "tTTTTTTTTTwwTTTt",
    "tttttttttttttttt",
    ".tt..........tt.",
    ".tt..........tt.",
    "................",
]
assert len(LOGO) == 16 and all(len(r) == 16 for r in LOGO)


def png(rows=LOGO, ink=INK, size=1024, margin=96):
    """The pixel art scaled up with hard edges, centered on a transparent square. Returns PNG bytes."""
    n = len(rows)
    cell = (size - 2 * margin) // n
    off = (size - cell * n) // 2
    clear = b"\0\0\0\0"
    lines = []
    for y in range(size):
        r = (y - off) // cell if off <= y < off + cell * n else None
        line = bytearray(b"\0")  # filter type: none
        for x in range(size):
            c = (x - off) // cell if r is not None and off <= x < off + cell * n else None
            ch = rows[r][c] if c is not None else "."
            if ch == ".":
                line += clear
            else:
                h = ink[ch]
                line += bytes((int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16), 255))
        lines.append(bytes(line))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(lines), 9)) + chunk(b"IEND", b""))
