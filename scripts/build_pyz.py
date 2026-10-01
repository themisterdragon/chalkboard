#!/usr/bin/env python3
"""Build dist/chalkboard.pyz: a single file that runs anywhere Python 3 + curses exist."""
import pathlib
import shutil
import tempfile
import zipapp

root = pathlib.Path(__file__).resolve().parent.parent
dist = root / "dist"
dist.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
    stage = pathlib.Path(tmp)
    shutil.copytree(root / "src" / "chalkboard", stage / "chalkboard",
                    ignore=shutil.ignore_patterns("__pycache__"))
    (stage / "__main__.py").write_text("from chalkboard.app import main\n\nmain()\n")
    out = dist / "chalkboard.pyz"
    zipapp.create_archive(stage, out, interpreter="/usr/bin/env python3", compressed=True)
shutil.copy2(root / "install.sh", dist / "install.sh")
print(f"built {out}")
