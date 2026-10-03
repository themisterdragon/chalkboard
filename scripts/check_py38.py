#!/usr/bin/env python3
"""Catch syntax newer than Chalkboard's oldest supported Python (3.8) that `ast` lets through:
inside an f-string's {...}, a backslash or the f-string's own quote character (both 3.12+ only).
The Mac terminal app runs on the system python3, which can be 3.9.

python3 scripts/check_py38.py   (exits 1 if anything is found)
"""
import pathlib
import sys
import tokenize

root = pathlib.Path(__file__).resolve().parent.parent
bad = []
for f in sorted((root / "src").rglob("*.py")):
    quotes = []  # quote of each f-string we're inside
    with open(f, "rb") as fh:
        for tok in tokenize.tokenize(fh.readline):
            name = tokenize.tok_name[tok.type]
            if name == "FSTRING_START":
                quotes.append(tok.string.lstrip("rRbBfF")[:3] if tok.string.lstrip("rRbBfF")[:3] in ('"""', "'''")
                              else tok.string.lstrip("rRbBfF")[:1])
            elif name == "FSTRING_END":
                quotes.pop()
            elif quotes and name == "STRING":
                q = quotes[-1]
                body = tok.string.lstrip("rRbBuU")
                if "\\" in tok.string or body.startswith(q):
                    bad.append(f"{f.relative_to(root)}:{tok.start[0]}: {tok.string[:40]} inside an f-string expression")
    for n, text in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
        if "__package__" in text and not text.lstrip().startswith("#"):
            bad.append(f"{f.relative_to(root)}:{n}: __package__ is None during import from a .pyz on Python 3.9")
if bad:
    sys.exit("needs Python 3.12+:\n  " + "\n  ".join(bad))
print("ok: no 3.12-only f-string syntax")
