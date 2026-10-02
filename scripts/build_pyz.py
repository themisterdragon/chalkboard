#!/usr/bin/env python3
"""Build dist/chalkboard.pyz (terminal, needs curses) and dist/chalkboard-gui.pyz (window, needs Tk)."""
import pathlib
import shutil
import tempfile
import zipapp

root = pathlib.Path(__file__).resolve().parent.parent
dist = root / "dist"
dist.mkdir(exist_ok=True)
for name, module in (("chalkboard", "app"), ("chalkboard-gui", "gui")):
    with tempfile.TemporaryDirectory() as tmp:
        stage = pathlib.Path(tmp)
        shutil.copytree(root / "src" / "chalkboard", stage / "chalkboard",
                        ignore=shutil.ignore_patterns("__pycache__"))
        (stage / "__main__.py").write_text(f"from chalkboard.{module} import main\n\nmain()\n")
        out = dist / f"{name}.pyz"
        zipapp.create_archive(stage, out, interpreter="/usr/bin/env python3", compressed=True)
    print(f"built {out}")
shutil.copy2(root / "install.sh", dist / "install.sh")
