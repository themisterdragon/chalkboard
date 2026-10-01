#!/usr/bin/env python3
"""Draw macos/Chalkboard.icns (needs Ghostscript; only rerun to change the icon)."""
import pathlib
import struct
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / "src"))
from chalkboard.export_pdf import B, Canvas, Fonts, _write  # noqa: E402
from chalkboard.export_png import STYLES, png_size  # noqa: E402

S = 1024
col = STYLES["chalk"]


def rounded(c, x, y, w, h, r, color):
    k = 0.5523 * r
    c.ops.append(
        "q %.3f %.3f %.3f rg " % color
        + f"{x + r} {y} m {x + w - r} {y} l {x + w - r + k} {y} {x + w} {y + r - k} {x + w} {y + r} c "
        + f"{x + w} {y + h - r} l {x + w} {y + h - r + k} {x + w - r + k} {y + h} {x + w - r} {y + h} c "
        + f"{x + r} {y + h} l {x + r - k} {y + h} {x} {y + h - r + k} {x} {y + h - r} c "
        + f"{x} {y + r} l {x} {y + r - k} {x + r - k} {y} {x + r} {y} c f Q")


f = Fonts("Helvetica")
c = Canvas()
rounded(c, 100, 100, 824, 824, 185, (0.545, 0.369, 0.224))   # wooden frame
rounded(c, 140, 140, 744, 744, 150, col["bg"])                 # slate
text = "C ]["
size = 300
c.text((S - f.width(text, B, size)) / 2, 470, text, B, size, col["ink"])
c.fill(250, 360, 524, 16, col["label"])                        # chalk line
c.fill(300, 300, 190, 30, (0.96, 0.95, 0.91))                  # chalk stick

with tempfile.TemporaryDirectory() as tmp:
    pdf = f"{tmp}/icon.pdf"
    _write([c], pdf, f, S, S, "Chalkboard")
    png = f"{tmp}/icon.png"
    subprocess.run(["gs", "-q", "-dSAFER", "-dBATCH", "-dNOPAUSE", "-sDEVICE=pngalpha", "-r72",
                    "-dTextAlphaBits=4", "-dGraphicsAlphaBits=4", "-sOutputFile=" + png, pdf], check=True)
    assert png_size(png) == (S, S)
    data = pathlib.Path(png).read_bytes()
# 'ic10' = 1024px (512@2x) PNG; macOS scales it down for smaller sizes
icns = b"ic10" + struct.pack(">I", len(data) + 8) + data
out = root / "macos" / "Chalkboard.icns"
out.write_bytes(b"icns" + struct.pack(">I", len(icns) + 8) + icns)
print(f"wrote {out}")
