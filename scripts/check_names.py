"""Check that every name the code uses is defined or imported somewhere in its module.

The window app swallows errors in button handlers, so a missing import makes a button quietly do
nothing (that's how Settings > Sections & School Colors broke in 2.0). This catches it at build time.
"""
import builtins
import os
import symtable
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "chalkboard")
KNOWN = set(dir(builtins)) | {"__file__", "__name__", "__package__", "__spec__"}


def check(path):
    with open(path, encoding="utf-8") as f:
        top = symtable.symtable(f.read(), path, "exec")
    defined = KNOWN | {s.get_name() for s in top.get_symbols()
                       if s.is_assigned() or s.is_imported() or s.is_namespace()}
    missing = []

    def walk(t):
        for s in t.get_symbols():
            # not is_local(): symtable calls a function named "top" global, mixing it up with the module
            if s.is_referenced() and (t is top or s.is_global() and not s.is_local()) and s.get_name() not in defined:
                missing.append(f"{os.path.relpath(path, ROOT)}: {t.get_name()}: {s.get_name()}")
        for c in t.get_children():
            walk(c)
    walk(top)
    return missing


missing = []
for folder, _, files in os.walk(ROOT):
    for name in sorted(files):
        if name.endswith(".py"):
            missing += check(os.path.join(folder, name))
if missing:
    sys.exit("names used but never defined or imported:\n  " + "\n  ".join(missing))
print("ok: every name is defined or imported")
