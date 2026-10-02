#!/usr/bin/env python3
"""Draw macos/Chalkboard.icns from the pixel-art logo (only rerun to change the icon).

The release workflow turns the .icns into the Windows .ico, and the Linux app-menu icon
comes from the same logo, so this one file sets the icon everywhere.
"""
import pathlib
import struct
import sys

root = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / "src"))
from chalkboard.logo import png  # noqa: E402

data = png(size=1024)
# 'ic10' = 1024px (512@2x) PNG; macOS scales it down for smaller sizes
icns = b"ic10" + struct.pack(">I", len(data) + 8) + data
out = root / "macos" / "Chalkboard.icns"
out.write_bytes(b"icns" + struct.pack(">I", len(icns) + 8) + icns)
print(f"wrote {out}")
