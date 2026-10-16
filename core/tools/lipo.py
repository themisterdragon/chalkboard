#!/usr/bin/env python3
"""Join Mac programs for different processors into one "universal" program (what Apple's lipo does),
so Chalkboard for macOS is one file for Apple silicon and Intel. Built with Zig, which has no lipo.
Usage: python3 -I tools/lipo.py OUT IN-arm64 IN-x86_64"""
import struct, sys

CPU = {0x0100000C: (0x0100000C, 0), 0x01000007: (0x01000007, 3)}   # arm64 (subtype ALL), x86_64 (subtype ALL)
ALIGN = 14   # 16 KB, as Apple's lipo does for arm64

out, inputs = sys.argv[1], sys.argv[2:]
slices = []
for path in inputs:
    data = open(path, "rb").read()
    magic, cputype, cpusub = struct.unpack("<III", data[:12])
    if magic != 0xFEEDFACF:
        sys.exit(f"{path}: not a 64-bit Mac program")
    slices.append((cputype, cpusub & 0x00FFFFFF, data))
header = struct.pack(">II", 0xCAFEBABE, len(slices))
offset = 1 << ALIGN
entries, body = b"", b""
for cputype, cpusub, data in slices:
    entries += struct.pack(">iiIII", cputype, cpusub, offset, len(data), ALIGN)
    pad = (-offset - len(data)) % (1 << ALIGN)
    body += data + b"\0" * pad
    offset += len(data) + pad
first = header + entries
with open(out, "wb") as f:
    f.write(first + b"\0" * ((1 << ALIGN) - len(first)) + body)
