"""Local storage for lessons, assessments, settings, and the standards library."""

import csv
import io
import json
import os
import pkgutil
import random
import re
import shutil
import sys
import time
import uuid

STANDARDS_DIR = "data/standards"
DEFAULT_SUBJECT = "Reading & Writing"
ALL = "ALL"
# grade filters offered in the standards picker, in menu order
GRADE_CHOICES = ["K", "1", "2", "3", "4", "5", "6", "7", "8", "9-10", "11-12", "9-12", ALL]
GOOD_THINGS_FILE = "data/sel_prompts.json"
GOOD_THINGS_PREFIX = "SEL: "


def data_dir():
    if sys.platform == "win32":
        base = os.environ.get("APPDATA") or os.path.expanduser("~")
    elif sys.platform == "darwin":
        base = os.path.expanduser("~/Library/Application Support")
    else:
        base = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
    return os.path.join(base, "chalkboard")


def default_export_dir():
    docs = os.path.expanduser("~/Documents")
    return os.path.join(docs if os.path.isdir(docs) else os.path.expanduser("~"), "Chalkboard")


DEFAULT_SETTINGS = {
    "teacher": "",
    "school": "",
    "course": "English 10",
    "grades": "9-10",
    "subject": DEFAULT_SUBJECT,
    "font": "Times",
    "page": "Letter",
    "theme": "green",
    "uppercase": True,
    "boot": True,
    "export_dir": "",
    "export_format": "PDF",
    "export_include": "BOTH",
    "export_versions": 1,
    "lesson_export_format": "PDF",
    "board_style": "chalk",
    "board_sections": ["standards", "targets", "success", "bell_ringer", "materials", "homework"],
    "board_std_text": True,
    "primary_color": "",
    "secondary_color": "",
    "text_color": "",
    "default_materials": "",
    "lesson_sort": "updated",
    "assess_sort": "updated",
}

# list sort orders: key -> menu label
SORTS = {"updated": "DATE MODIFIED", "created": "DATE CREATED", "title": "TITLE (A-Z)", "unit": "UNIT"}

BOARD_SECTIONS = [
    # lesson key, heading on the slide, column
    ("standards", "Standards", "left"),
    ("targets", "I Can...", "left"),
    ("success", "Success Criteria", "left"),
    ("bell_ringer", "Bell Ringer", "right"),
    ("materials", "Materials", "right"),
    ("homework", "Homework", "right"),
    ("closure", "Exit Ticket", "right"),
]
BOARD_STYLES = {"chalk": "CHALKBOARD (DARK GREEN)", "white": "WHITEBOARD (WHITE)", "school": "SCHOOL COLORS"}


def grade_span(g):
    """'K' -> (0, 0), '9-10' -> (9, 10), 'K-12' -> (0, 12); None for anything else (custom)."""
    m = re.fullmatch(r"(K|\d+)(?:-(K|\d+))?", (g or "").strip())
    if not m:
        return None
    lo = 0 if m.group(1) == "K" else int(m.group(1))
    hi = lo if m.group(2) is None else (0 if m.group(2) == "K" else int(m.group(2)))
    return lo, hi


def grades_match(std_grades, wanted):
    """True when a standard's grades overlap the grade filter (custom standards always match)."""
    if wanted == ALL:
        return True
    a, b = grade_span(std_grades), grade_span(wanted)
    return a is None or b is None or (a[0] <= b[1] and b[0] <= a[1])


def parse_hex(s):
    """'#7a0019', '7A0019', or '#a01' -> '#7A0019'; None if it isn't a hex color."""
    s = (s or "").strip().lstrip("#")
    if len(s) == 3:
        s = "".join(c * 2 for c in s)
    return "#" + s.upper() if re.fullmatch(r"[0-9a-fA-F]{6}", s) else None

CSV_COLUMNS = ("subject", "code", "grades", "strand", "cluster", "part_of", "text")


def clean_path(path):
    """A path typed or dragged into the terminal: drop surrounding quotes and backslash-escaped spaces."""
    path = (path or "").strip()
    if len(path) > 1 and path[0] == path[-1] and path[0] in "'\"":
        path = path[1:-1]
    return os.path.expanduser(path.replace("\\ ", " "))


def subject_filename(subject, folder):
    """A file name for a subject that isn't already taken in folder."""
    base = re.sub(r"[^a-z0-9]+", "_", subject.lower()).strip("_") or "standards"
    name, n = base + ".json", 2
    while os.path.exists(os.path.join(folder, name)):
        name, n = f"{base}_{n}.json", n + 1
    return name


def read_standards_file(path):
    """Parse a standards CSV or JSON file into [{"subject", "source", "standards": [...]}].

    CSV: a header row with at least `code` and `text`; optional subject, grades, strand,
    cluster, and part_of (the code of the standard a lettered or numbered part belongs to).
    JSON: one subject object, or a list of them, in Chalkboard's own format.
    Raises ValueError with a message for the user.
    """
    path = clean_path(path)
    if not os.path.isfile(path):
        raise ValueError(f"NO FILE AT {path}")
    stem = os.path.splitext(os.path.basename(path))[0]
    source = f"Imported from {os.path.basename(path)} on {time.strftime('%Y-%m-%d')}."
    try:
        with open(path, encoding="utf-8-sig") as f:
            raw = f.read()
    except UnicodeDecodeError:
        with open(path, encoding="cp1252") as f:  # Excel's "CSV" (not "CSV UTF-8") on Windows and Mac
            raw = f.read()
    if path.lower().endswith(".json"):
        try:
            data = json.loads(raw)
        except ValueError as e:
            raise ValueError(f"NOT VALID JSON ({e})")
        return [json_subject(d, stem, source) for d in (data if isinstance(data, list) else [data])]
    return csv_subjects(raw, stem, source)


def json_subject(d, stem, source):
    if not isinstance(d, dict) or not isinstance(d.get("standards"), list):
        raise ValueError('JSON NEEDS A "standards" LIST (SEE THE README)')
    out = {"subject": str(d.get("subject") or stem).strip(), "source": str(d.get("source") or source), "standards": []}
    seen = set()
    for i, s in enumerate(d["standards"], 1):
        if not isinstance(s, dict) or not str(s.get("code", "")).strip() or not str(s.get("text", "")).strip():
            raise ValueError(f'STANDARD #{i} NEEDS A "code" AND A "text"')
        code = str(s["code"]).strip()
        if code in seen:
            raise ValueError(f"CODE {code} IS IN THE FILE TWICE")
        seen.add(code)
        subs = [[str(k).strip(), " ".join(str(v).split())] for k, v in s.get("subs") or []]
        out["standards"].append({"code": code, "grades": str(s.get("grades") or "").strip(),
                                 "strand": str(s.get("strand") or "").strip(),
                                 "cluster": str(s.get("cluster") or "").strip(),
                                 "text": " ".join(str(s["text"]).split()), "subs": subs})
    if not out["standards"]:
        raise ValueError("THE FILE HAS NO STANDARDS IN IT")
    return out


def csv_subjects(raw, stem, source):
    try:
        dialect = csv.Sniffer().sniff(raw[:4096], delimiters=",;\t")
    except csv.Error:
        dialect = csv.excel
    rows = list(csv.reader(io.StringIO(raw), dialect))
    if not rows:
        raise ValueError("THE FILE IS EMPTY")
    head = [h.strip().lower().replace(" ", "_") for h in rows[0]]
    if "code" not in head or "text" not in head:
        raise ValueError("THE FIRST ROW NEEDS COLUMN NAMES, INCLUDING code AND text")
    docs, tops, parts, seen = {}, {}, [], set()
    for n, row in enumerate(rows[1:], 2):
        r = {k: (row[i].strip() if i < len(row) else "") for i, k in enumerate(head)}
        if not any(r.values()):
            continue
        code, text = r.get("code", ""), " ".join(r.get("text", "").split())
        if not code or not text:
            raise ValueError(f"ROW {n} NEEDS A code AND A text")
        subj = r.get("subject") or stem
        if code in seen:
            raise ValueError(f"ROW {n}: CODE {code} IS IN THE FILE TWICE")
        seen.add(code)
        doc = docs.setdefault(subj, {"subject": subj, "source": source, "standards": []})
        parent = r.get("part_of") or r.get("parent") or ""
        if parent:
            parts.append((code, parent, text, n))
            continue
        tops[code] = {"code": code, "grades": r.get("grades", ""), "strand": r.get("strand", ""),
                      "cluster": r.get("cluster", ""), "text": text, "subs": []}
        doc["standards"].append(tops[code])
    for code, parent, text, n in parts:
        p = tops.get(parent)
        if not p:
            raise ValueError(f"ROW {n}: part_of {parent} ISN'T A CODE IN THE FILE")
        # lettered parts read RL.9-10.1a, everything else PARENT.LABEL
        rest = code[len(parent):] if code.startswith(parent) else ""
        label = rest[1:] if rest.startswith(".") else rest
        if not label or not (rest == "." + label or (len(label) == 1 and label.islower())):
            raise ValueError(f"ROW {n}: A PART OF {parent} NEEDS A CODE LIKE {parent}a OR {parent}.1")
        p["subs"].append([label, text])
    out = [d for d in docs.values() if d["standards"]]
    if not out:
        raise ValueError("THE FILE HAS NO STANDARDS IN IT")
    return out


LESSON_FIELDS = [
    # key, label, kind
    ("title", "Title", "line"),
    ("unit", "Unit", "line"),
    ("course", "Course", "line"),
    ("date", "Date(s)", "line"),
    ("duration", "Duration", "line"),
    ("standards", "Standards", "standards"),
    ("targets", "Learning Targets (I can...)", "text"),
    ("success", "Success Criteria", "text"),
    ("materials", "Materials & Texts", "text"),
    ("bell_ringer", "Bell Ringer / Warm-Up", "text"),
    ("instruction", "Direct Instruction (I Do)", "text"),
    ("guided", "Guided Practice (We Do)", "text"),
    ("independent", "Independent Practice (You Do)", "text"),
    ("closure", "Closure / Exit Ticket", "text"),
    ("differentiation", "Differentiation & Supports", "text"),
    ("checks", "Assessment / Checks for Understanding", "text"),
    ("assessments", "Assessments & Worksheets", "attached"),
    ("homework", "Homework", "text"),
    ("notes", "Notes & Reflection", "text"),
]

ASSESSMENT_KINDS = ["Quiz", "Test", "Assignment", "Worksheet", "Exit Ticket", "Homework", "Unit Exam"]
# Fixed-layout handouts: no questions or answer key, their own editor and page layout.
ANNOTATION = "Annotation Sheet"
BELL_SHEET = "Bell Ringer Sheet"
SHEET_KINDS = [ANNOTATION, BELL_SHEET]
ANNOTATION_COLS = [("Line", 0.36), ("Symbol", 0.14), ("Reason for Annotating", 0.50)]
WEEKDAYS = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday"]

QUESTION_TYPES = [
    # type, label, description
    ("mc", "MULTIPLE CHOICE", "prompt + lettered choices"),
    ("tf", "TRUE / FALSE", "statement + True/False"),
    ("short", "SHORT ANSWER", "prompt + a few writing lines"),
    ("essay", "EXTENDED RESPONSE", "prompt + lined, blank, or boxed space"),
    ("fill", "FILL IN THE BLANK", "use ___ for each blank"),
    ("match", "MATCHING", "terms + shuffled definitions"),
    ("passage", "READING PASSAGE", "a text block with line numbers (not scored)"),
    ("section", "SECTION HEADER", "titled part with directions (not scored)"),
]
TYPE_LABEL = {t: label for t, label, _ in QUESTION_TYPES}
TYPE_TAG = {"mc": "MC", "tf": "T/F", "short": "SA", "essay": "ER", "fill": "FIB",
            "match": "MAT", "passage": "TEXT", "section": "SECT"}


def new_id():
    return uuid.uuid4().hex[:10]


def now():
    return time.strftime("%Y-%m-%d %H:%M")


def new_lesson(settings):
    d = {k: "" for k, _, kind in LESSON_FIELDS if kind in ("text", "line")}
    d.update(id=new_id(), standards=[], assessments=[], course=settings.get("course", ""),
             grades=settings.get("grades", "9-10"), materials=settings.get("default_materials", ""),
             created=now(), updated=now())
    return d


def new_assessment(settings, kind="Quiz"):
    a = {
        "id": new_id(), "title": "", "kind": kind, "unit": "", "course": settings.get("course", ""),
        "grades": settings.get("grades", "9-10"), "standards": [], "instructions": "",
        "show_name": True, "show_points": True, "show_standards": False,
        "questions": [], "created": now(), "updated": now(),
    }
    if kind == ANNOTATION:
        a.update(rows=10)
    elif kind == BELL_SHEET:
        a.update(weeks=["", ""], days=[""] * 10)
    return a


def new_question(qtype, default_std=""):
    q = {"type": qtype, "prompt": "", "points": 1, "standard": default_std}
    if qtype == "mc":
        q.update(choices=[], answer=None)
    elif qtype == "tf":
        q.update(answer=None)
    elif qtype == "short":
        q.update(lines=3, answer="")
    elif qtype == "essay":
        q.update(lines=12, space="lines", answer="", points=10)
    elif qtype == "fill":
        q.update(answer="")
    elif qtype == "match":
        q.update(pairs=[], points=0)
    elif qtype == "passage":
        q.update(title="", text="", numbered=True, points=0)
    elif qtype == "section":
        q.update(title="", points=0)
    return q


def points_of(q):
    if q["type"] in ("passage", "section"):
        return 0
    if q["type"] == "match" and not q.get("points"):
        return len(q.get("pairs", []))
    try:
        return float(q.get("points") or 0)
    except (TypeError, ValueError):
        return 0


def fmt_points(p):
    return str(int(p)) if float(p).is_integer() else str(p)


def unit_sort_key(u):
    """'Unit 2' before 'Unit 10'; units without a number after those with one."""
    m = re.search(r"\d+", u or "")
    return (0, int(m.group()), u.lower()) if m else (1, 0, (u or "").lower())


def sort_items(items, how):
    if how == "title":
        return sorted(items, key=lambda x: (x.get("title") or "").lower())
    if how == "unit":
        by_title = sorted(items, key=lambda x: (x.get("title") or "").lower())
        return sorted(by_title, key=lambda x: (not (x.get("unit") or "").strip(), unit_sort_key(x.get("unit"))))
    key = "created" if how == "created" else "updated"
    return sorted(items, key=lambda x: x.get(key) or x.get("updated", ""), reverse=True)


class Store:
    def __init__(self):
        self.dir = data_dir()
        self.path = os.path.join(self.dir, "data.json")
        self.warning = None
        self._backed_up = False
        self.data = {"version": 1, "settings": dict(DEFAULT_SETTINGS), "lessons": [],
                     "assessments": [], "custom_standards": []}
        self.load()
        self.reload_standards()

    @property
    def settings(self):
        return self.data["settings"]

    def load(self):
        try:
            with open(self.path, encoding="utf-8") as f:
                d = json.load(f)
        except FileNotFoundError:
            return
        except (OSError, ValueError) as e:
            bad = f"{self.path}.unreadable-{time.strftime('%Y%m%d-%H%M%S')}"
            try:
                shutil.copy2(self.path, bad)
            except OSError:
                pass
            self.warning = f"?COULD NOT READ DATA FILE ({e}). A COPY WAS SAVED TO {bad}"
            return
        settings = dict(DEFAULT_SETTINGS)
        settings.update(d.get("settings", {}))
        d["settings"] = settings
        for k in ("lessons", "assessments", "custom_standards"):
            d.setdefault(k, [])
        for x in d["lessons"] + d["assessments"]:
            x.setdefault("created", x.get("updated", ""))
        for l in d["lessons"]:
            l.setdefault("assessments", [])
        self.data = d

    def save(self):
        os.makedirs(self.dir, exist_ok=True)
        if not self._backed_up and os.path.exists(self.path):
            shutil.copy2(self.path, self.path + ".bak")
            self._backed_up = True
        tmp = self.path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(self.data, f, indent=1, ensure_ascii=False)
        os.replace(tmp, self.path)

    def export_dir(self):
        return os.path.expanduser(self.settings.get("export_dir") or default_export_dir())

    # -- lessons <-> assessments
    def assessment(self, aid):
        return next((a for a in self.data["assessments"] if a["id"] == aid), None)

    def attached(self, lesson):
        """The assessments and worksheets linked to a lesson, in the lesson's order."""
        return [a for a in map(self.assessment, lesson.get("assessments") or []) if a]

    def units(self, items):
        return sorted({(x.get("unit") or "").strip() for x in items} - {""}, key=unit_sort_key)

    # -- standards
    def standards_dir(self):
        """Where imported standards live: one JSON file per subject."""
        return os.path.join(self.dir, "standards")

    def load_kas(self):
        """Standards sets, one per subject: any bundled with the app, then the ones you imported
        (an imported subject replaces a bundled one of the same name). Cached until reload_standards."""
        if not hasattr(self, "_kas"):
            docs = {}
            try:
                index = json.loads(pkgutil.get_data(__package__, STANDARDS_DIR + "/index.json").decode("utf-8"))
                for s in index["subjects"]:
                    doc = json.loads(pkgutil.get_data(__package__, f"{STANDARDS_DIR}/{s['file']}").decode("utf-8"))
                    docs[doc["subject"]] = doc
            except (OSError, ValueError, KeyError, TypeError):
                pass
            d = self.standards_dir()
            for name in sorted(os.listdir(d)) if os.path.isdir(d) else []:
                if not name.endswith(".json"):
                    continue
                try:
                    with open(os.path.join(d, name), encoding="utf-8") as f:
                        doc = json.load(f)
                    doc["file"] = os.path.join(d, name)
                    docs[doc["subject"]] = doc
                except (OSError, ValueError, KeyError, TypeError):
                    self.warning = f"?COULD NOT READ STANDARDS FILE {os.path.join(d, name)}"
            self._kas = list(docs.values())
        return self._kas

    def imported_subjects(self):
        return [doc["subject"] for doc in self.load_kas() if doc.get("file")]

    def import_standards(self, path):
        """Copy a CSV or JSON standards file into the standards folder (see README, "Standards files").
        Returns [(subject, count, skipped)]; raises ValueError with a message for the user."""
        docs = read_standards_file(path)
        taken = {s["code"]: s["subject"] for s in self.standards}
        os.makedirs(self.standards_dir(), exist_ok=True)
        done = []
        for doc in docs:
            keep, skipped = [], 0
            for s in doc["standards"]:
                if taken.get(s["code"], doc["subject"]) != doc["subject"]:
                    skipped += 1
                else:
                    keep.append(s)
            if not keep:
                raise ValueError(f"EVERY CODE IN {doc['subject']} IS ALREADY USED BY ANOTHER SUBJECT")
            doc["standards"] = keep
            old = next((d["file"] for d in self.load_kas() if d["subject"] == doc["subject"] and d.get("file")), None)
            out = old or os.path.join(self.standards_dir(), subject_filename(doc["subject"], self.standards_dir()))
            tmp = out + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(doc, f, indent=1, ensure_ascii=False)
            os.replace(tmp, out)
            done.append((doc["subject"], len(keep), skipped))
        self.reload_standards(rescan=True)
        return done

    def remove_subject(self, subject):
        """Delete an imported subject's file. Lessons keep the codes they already use."""
        for doc in self.load_kas():
            if doc["subject"] == subject and doc.get("file"):
                os.remove(doc["file"])
        self.reload_standards(rescan=True)

    def reload_standards(self, rescan=False):
        if rescan and hasattr(self, "_kas"):
            del self._kas
        out = []
        self.subjects = []
        for doc in self.load_kas():
            subj, src = doc["subject"], doc.get("source") or "Imported."
            self.subjects.append(subj)
            for s in doc["standards"]:
                s.setdefault("subs", [])
                out.append({"code": s["code"], "grades": s.get("grades", ""), "strand": s.get("strand", ""),
                            "cluster": s.get("cluster", ""), "text": s["text"], "parent": None,
                            "subs": s["subs"], "sub_grades": s.get("sub_grades", False), "subject": subj, "source": src})
                for label, text in s["subs"]:
                    # lettered parts read RF.K.1a; numbered indicators and grade levels read P.EL1.A.1, E-DA-01.K
                    code = s["code"] + label if len(label) == 1 and label.islower() else f"{s['code']}.{label}"
                    out.append({"code": code, "label": label, "grades": label if s.get("sub_grades") else s.get("grades", ""),
                                "strand": s.get("strand", ""), "cluster": s.get("cluster", ""), "text": text, "parent": s["code"],
                                "subs": [], "subject": subj, "source": src})
        for c in self.data["custom_standards"]:
            out.append({"code": c["code"], "grades": c.get("grades") or "Custom", "strand": "Custom",
                        "cluster": "Custom", "text": c["text"], "parent": None, "subs": [], "custom": True,
                        "subject": "Custom", "source": "Added by you."})
        self.standards = out
        self.std_index = {s["code"]: s for s in out}
        self.kas_count = sum(1 for s in out if not s["parent"] and not s.get("custom"))

    def std_text(self, code):
        """Full printable text of a standard (expands 'When writing:' style stems)."""
        s = self.std_index.get(code)
        if not s:
            return ""
        if s["parent"]:
            return s["text"]
        if s["subs"] and s["text"].rstrip().endswith(":"):
            return s["text"] + " " + " ".join(f"({k}) {v}" for k, v in s["subs"])
        return s["text"]

    # -- bell ringers
    def good_thing(self, current=""):
        """A random SEL bell ringer, preferring prompts no lesson is using yet."""
        if not hasattr(self, "_good_things"):
            raw = json.loads(pkgutil.get_data(__package__, GOOD_THINGS_FILE).decode("utf-8"))
            self._good_things = [GOOD_THINGS_PREFIX + p for p in raw["prompts"]]
        used = {(l.get("bell_ringer") or "").strip() for l in self.data["lessons"]} | {(current or "").strip()}
        fresh = [g for g in self._good_things if g not in used]
        return random.choice(fresh or [g for g in self._good_things if g != (current or "").strip()])
