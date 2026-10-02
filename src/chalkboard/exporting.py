"""Export lessons and assessments to files. Shared by the terminal and windowed apps (no UI code here)."""

import os
import re
import subprocess
import sys

from .store import SHEET_KINDS, periods_for
from .doc import LETTERS, assessment_doc, board_doc, lesson_doc, makeup_doc
from .export_pdf import render_pdf
from .export_docx import render_docx
from .export_png import BoardError, render_png
from .export_txt import render_txt

FORMATS = {"PDF": (".pdf", render_pdf), "DOCX": (".docx", render_docx), "TXT": (".txt", render_txt),
           "PNG": (".png", render_png)}
FORMAT_ORDER = {"lesson": ["PDF", "DOCX", "TXT", "PNG", "MAKEUP", "ALL"], "assessment": ["PDF", "DOCX", "TXT", "ALL"]}


class ExportError(Exception):
    pass


def safe_name(s):
    s = re.sub(r'[<>:"/\\|?*\x00-\x1f]+', "", s or "").strip(" .")
    return s[:80] or "Untitled"


def export_subdirs(kind, obj):
    """Class > Unit > Lesson folders, e.g. ['English 10', 'Unit 3', 'The Raven'].

    Assessments stop at the unit folder. A blank course or unit is skipped.
    """
    unit = (obj.get("unit") or "").strip()
    if re.fullmatch(r"\d+[A-Za-z]?", unit):
        unit = "Unit " + unit
    parts = [(obj.get("course") or "").strip(), unit]
    if kind == "lesson":
        parts.append(obj.get("title") or "Untitled")
    return [safe_name(x) for x in parts if x]


def export_folder(store, kind, obj):
    return os.path.join(store.export_dir(), *export_subdirs(kind, obj))


def open_path(path):
    try:
        if sys.platform == "win32":
            os.startfile(path)
        else:
            subprocess.Popen(["open" if sys.platform == "darwin" else "xdg-open", path],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        return True
    except OSError:
        return False


def assessment_docs(store, a):
    """[(file name, doc)] for an assessment, per the student copy/key and versions settings."""
    st = store.settings
    base = safe_name(a.get("title"))
    if a.get("kind") in SHEET_KINDS:
        return [(base, assessment_doc(a, store))]
    docs = []
    versions = st["export_versions"]
    keys = {"STUDENT": [False], "KEY": [True], "BOTH": [False, True]}[st["export_include"]]
    for v in range(versions):
        for key in keys:
            name = base + (f" - Version {LETTERS[v]}" if versions > 1 else "") + (" - Answer Key" if key else "")
            docs.append((name, assessment_doc(a, store, version=v, versions=versions, key=key)))
    return docs


def export(store, kind, obj, fmt):
    """Write the files for a lesson or assessment in one format (or ALL); returns their paths.

    Raises ExportError with a message for the user.
    """
    st = store.settings
    folder = export_folder(store, kind, obj)
    try:
        os.makedirs(folder, exist_ok=True)
    except OSError as e:
        raise ExportError(f"CANNOT CREATE FOLDER: {e}") from e
    base = safe_name(obj.get("title"))
    fmts = [f for f in FORMAT_ORDER[kind] if f != "ALL"] if fmt == "ALL" else [fmt]
    docs = []
    if kind == "lesson":
        if "MAKEUP" in fmts:
            fmts.remove("MAKEUP")
            docs.append((base + " - Make-Up Sheet", makeup_doc(obj, store), ["PDF", "DOCX"]))
        if fmts and fmts != ["PNG"]:
            docs.append((base + " - Lesson Plan", lesson_doc(obj, store)))
        if "PNG" in fmts:
            fmts.remove("PNG")
            periods = periods_for(st, obj) if "class_codes" in (st.get("board_sections") or []) else []
            if periods:  # one slide per class period, each with its own codes
                docs += [(f"{base} - Board ({safe_name(p.get('name') or f'Class {i}')})",
                          board_doc(obj, store, p), ["PNG"]) for i, p in enumerate(periods, 1)]
            else:
                docs.append((base + " - Board", board_doc(obj, store), ["PNG"]))
        if fmt == "ALL":
            for a in store.attached(obj):
                docs += [(name, d, ["PDF", "DOCX"]) for name, d in assessment_docs(store, a)]
    else:
        docs = assessment_docs(store, obj)
    files = []
    try:
        for name, d, *only in docs:
            for f in (only[0] if only else fmts):
                ext, fn = FORMATS[f]
                path = os.path.join(folder, name + ext)
                fn(d, path, family=st["font"], page=st["page"])
                files.append(path)
    except BoardError as e:
        raise ExportError(str(e)) from e
    except OSError as e:
        raise ExportError(f"EXPORT FAILED: {e}") from e
    return files
