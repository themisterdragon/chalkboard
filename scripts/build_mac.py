#!/usr/bin/env python3
"""Build dist/Chalkboard-macOS.zip: a double-clickable Chalkboard.app (runs in Terminal)."""
import pathlib
import re
import runpy
import time
import zipfile

root = pathlib.Path(__file__).resolve().parent.parent
dist = root / "dist"
mac = root / "macos"
runpy.run_path(str(root / "scripts" / "build_pyz.py"))
version = re.search(r'__version__ = "([^"]+)"', (root / "src" / "chalkboard" / "__init__.py").read_text()).group(1)

app = "Chalkboard.app/Contents/"
files = [  # path in zip, bytes, executable
    (app + "Info.plist", (mac / "Info.plist").read_text().replace("@VERSION@", version).encode(), False),
    (app + "PkgInfo", b"APPL????", False),
    (app + "MacOS/Chalkboard", (mac / "launcher.sh").read_bytes(), True),
    (app + "Resources/chalkboard.pyz", (dist / "chalkboard.pyz").read_bytes(), True),
    (app + "Resources/Chalkboard.icns", (mac / "Chalkboard.icns").read_bytes(), False),
    ("Read Me First.txt", (mac / "Read Me First.txt").read_bytes(), False),
]
out = dist / "Chalkboard-macOS.zip"
stamp = time.localtime()[:6]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    dirs = sorted({p.rsplit("/", i)[0] + "/" for p, _, _ in files for i in range(1, p.count("/") + 1)})
    for d in dirs:
        info = zipfile.ZipInfo(d, stamp)
        info.external_attr = (0o40755 << 16) | 0x10
        info.create_system = 3  # unix, so Archive Utility honors the modes
        z.writestr(info, b"")
    for path, data, exe in files:
        info = zipfile.ZipInfo(path, stamp)
        info.external_attr = (0o100755 if exe else 0o100644) << 16
        info.create_system = 3
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, data)
print(f"built {out}")
