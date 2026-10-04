"""Export lessons and assessments to files. Shared by the terminal and windowed apps (no UI code here)."""

import os
import re
import subprocess
import sys

from .store import SHEET_KINDS, fmt_date, periods_for, school_year_label
from .doc import LETTERS, assessment_doc, board_doc, lesson_doc, makeup_doc
from .export_pdf import render_pdf
from .export_docx import render_docx
from .export_png import BoardError, render_day, render_png
from .export_txt import render_txt

FORMATS = {"PDF": (".pdf", render_pdf), "DOCX": (".docx", render_docx), "TXT": (".txt", render_txt),
           "PNG": (".png", render_png)}
FORMAT_ORDER = {"lesson": ["PDF", "DOCX", "TXT", "PNG", "MAKEUP", "ALL"], "assessment": ["PDF", "DOCX", "TXT", "ALL"]}
PLUGIN_FORMATS = {}  # key -> label, for formats plugins add (see plugins.py); ALL leaves them out


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


def export_folder(store, kind, obj, root=None):
    return os.path.join(root or store.export_dir(), *export_subdirs(kind, obj))


# Inside a lesson's (or unit's) folder, the PDFs you print stay on top and the rest go in subfolders.
SUBFOLDERS = {"PNG": "Board Slides", "DOCX": "Word", "TXT": "Text"}
KEYS_FOLDER = "Answer Keys"


def subfolder(name, fmt):
    """The subfolder a file goes in ("" = the item's folder itself)."""
    if fmt in SUBFOLDERS:
        return SUBFOLDERS[fmt]
    return KEYS_FOLDER if fmt == "PDF" and name.endswith(" - Answer Key") else ""


def tidy_old(folder, sub, name, wrote):
    """Before subfolders, every file went straight in the item's folder: remove the old copy of each
    file just rewritten in a subfolder (and an older board's extra "<name> 2.png" slides)."""
    if not sub:
        return
    old = [os.path.join(folder, os.path.basename(p)) for p in wrote]
    if wrote and wrote[0].endswith(".png"):
        pat = re.compile(re.escape(name) + r" \d+\.png$")
        old += [os.path.join(folder, f) for f in os.listdir(folder) if pat.match(f)]
    for p in old:
        if os.path.isfile(p):
            try:
                os.remove(p)
            except OSError:
                pass


def files_folder(files):
    """The one folder that holds every file in files: where "Open Folder" goes."""
    try:
        return os.path.commonpath([os.path.dirname(p) for p in files])
    except ValueError:  # different drives
        return os.path.dirname(files[0])


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


def export(store, kind, obj, fmt, progress=None, root=None):
    """Write the files for a lesson or assessment in one format (or ALL); returns their paths.

    progress(done, total, file name) is called before each file and once more at the end.
    root: the top folder (default: the export folder in Settings).
    Raises ExportError with a message for the user.
    """
    st = store.settings
    folder = export_folder(store, kind, obj, root)
    try:
        os.makedirs(folder, exist_ok=True)
    except OSError as e:
        raise ExportError(f"CANNOT CREATE FOLDER: {e}") from e
    base = safe_name(obj.get("title"))
    fmts = [f for f in FORMAT_ORDER[kind] if f != "ALL" and f not in PLUGIN_FORMATS] if fmt == "ALL" else [fmt]
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
    jobs = [(name, d, f) for name, d, *only in docs for f in (only[0] if only else fmts)]
    files = []
    try:
        for i, (name, d, f) in enumerate(jobs):
            ext, fn = FORMATS[f]
            if progress:
                progress(i, len(jobs), name + ext)
            sub = "" if f in PLUGIN_FORMATS else subfolder(name, f)
            os.makedirs(os.path.join(folder, sub), exist_ok=True)
            path = os.path.join(folder, sub, name + ext)
            wrote = fn(d, path, item=obj) if f in PLUGIN_FORMATS else fn(d, path, family=st["font"], page=st["page"])
            wrote = wrote if isinstance(wrote, list) else [path]
            tidy_old(folder, sub, name, wrote)
            files += wrote
        if progress:
            progress(len(jobs), len(jobs), "")
        from .plugins import after_export
        after_export(files)
    except BoardError as e:
        raise ExportError(str(e)) from e
    except OSError as e:
        raise ExportError(f"EXPORT FAILED: {e}") from e
    return files


def export_day(store, picks, day, root=None):
    """A whole day's board slides, class period by class period, as one slideshow (.pptx) and PDF,
    so the next class's slide is one click away at passing time.
    picks: [(class period, lesson)] in order; a period without a lesson is skipped. day: a date.
    Returns the files written; raises ExportError with a message for the user."""
    docs = [board_doc(l, store, p) for p, l in picks if l]
    if not docs:
        raise ExportError("PICK A LESSON FOR AT LEAST ONE CLASS PERIOD")
    folder = os.path.join(root or store.export_dir(), "Day Slideshows")
    try:
        os.makedirs(folder, exist_ok=True)
        # 2026-10-04 sorts by date and never collides with next year's Oct 4
        files = render_day(docs, os.path.join(folder, f"{day.isoformat()} - Day Slideshow"),
                           f"{fmt_date(day, weekday=True)} - Day Slideshow")
    except BoardError as e:
        raise ExportError(str(e)) from e
    except OSError as e:
        raise ExportError(f"EXPORT FAILED: {e}") from e
    from .plugins import after_export
    after_export(files)
    return files


MAP_FORMATS = {"PDF": "PDF", "DOCX": "Word", "CSV": "Spreadsheet (CSV)"}


def export_map(store, course, fmts, subject="", grades="", root=None, year=None):
    """A class's curriculum map as PDF / Word / CSV, in the class's export folder (course None: every
    class in one map, in the export folder). Returns the files."""
    import csv
    from .doc import curriculum_doc, curriculum_rows
    from .store import classes
    st = store.settings
    if course is None:  # every class, each starting on a new page
        from .doc import curriculum_units
        names = [n for n, count in classes(st, store.data["lessons"]) if count and curriculum_units(store, n, year)]
        if not names:
            raise ExportError("NO LESSONS ARE PLANNED YET")
        docs = [curriculum_doc(store, n, subject, grades, year) for n in names]
        doc = {"title": "Curriculum Map", "footer": " - ".join(x for x in (st.get("teacher"), "Curriculum Map") if x),
               "blocks": [b for i, d in enumerate(docs) for b in ([{"t": "pagebreak"}] if i else []) + d["blocks"]]}
        rows = curriculum_rows(store, names[0], year) + [r for n in names[1:]
                                                         for r in curriculum_rows(store, n, year)[1:]]
        folder, base_name = root or store.export_dir(), "Curriculum Map - All Classes"
    else:
        doc, rows = curriculum_doc(store, course, subject, grades, year), curriculum_rows(store, course, year)
        folder = os.path.join(root or store.export_dir(), *([safe_name(course)] if course else []))
        base_name = "Curriculum Map - " + (course or "No Class")
    if year:
        base_name += f" ({school_year_label(year)})"
    base = os.path.join(folder, safe_name(base_name))
    files = []
    try:
        os.makedirs(folder, exist_ok=True)
        for f in fmts:
            if f == "CSV":
                with open(base + ".csv", "w", newline="", encoding="utf-8-sig") as fh:  # BOM: Excel reads UTF-8
                    csv.writer(fh).writerows(rows)
                files.append(base + ".csv")
            else:
                ext, fn = FORMATS[f]
                fn(doc, base + ext, family=st["font"], page=st["page"])
                files.append(base + ext)
    except OSError as e:
        raise ExportError(f"EXPORT FAILED: {e}") from e
    from .plugins import after_export
    after_export(files)
    return files


def export_everything(store, parent, progress=None):
    """Every lesson and assessment as PDF and Word, plus a backup file, in one new dated folder
    inside parent: a complete copy to keep, or to drag into Google Drive or another cloud folder.
    Returns (folder, files, problems); one item that won't export is listed in problems and the
    rest still go."""
    import time
    parent = os.path.expanduser(parent)
    name, n = f"Chalkboard {time.strftime('%Y-%m-%d')}", 2
    while os.path.exists(os.path.join(parent, name)):
        name, n = f"Chalkboard {time.strftime('%Y-%m-%d')} ({n})", n + 1
    folder = os.path.join(parent, name)
    try:
        os.makedirs(folder)
    except OSError as e:
        raise ExportError(f"CANNOT CREATE FOLDER: {e}") from e
    jobs = [("lesson", x) for x in store.data["lessons"]] + [("assessment", x) for x in store.data["assessments"]]
    files, problems = [], []
    for i, (kind, obj) in enumerate(jobs):
        if progress:
            progress(i, len(jobs) + 1, obj.get("title") or "Untitled")
        for fmt in ("PDF", "DOCX"):
            try:
                files += export(store, kind, obj, fmt, root=folder)
            except ExportError as e:
                problems.append(f"{obj.get('title') or 'Untitled'}: {e}")
                break
    if progress:
        progress(len(jobs), len(jobs) + 1, "backup file")
    try:
        files.append(store.backup(folder, remember=False))
    except OSError as e:
        problems.append(f"BACKUP FILE: {e}")
    if progress:
        progress(len(jobs) + 1, len(jobs) + 1, "")
    return folder, files, problems
