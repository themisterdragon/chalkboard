"""Local storage for lessons, assessments, settings, and the standards library."""

import copy
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

from .images import ImageError, from_json, read_image, to_json

# Not __package__: the Mac app's Python 3.9 leaves it None while a module loads from the .pyz.
PACKAGE = __name__.rpartition(".")[0]

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
    "mascot": "",
    "export_dir": "",
    "export_format": "PDF",
    "export_include": "BOTH",
    "export_versions": 1,
    "lesson_export_format": "PDF",
    "board_style": "chalk",
    "board_sections": ["question", "standards", "targets", "success", "vocab", "bell_ringer", "materials", "homework",
                       "class_codes"],
    "board_v": 2,  # 2: Essential Question and Vocabulary exist (older files get them switched on once)
    "lesson_hide": ["question", "vocab"],  # lesson sections hidden in the editors (Settings > Lesson Sections)
    "board_std_text": True,
    "primary_color": "",
    "secondary_color": "",
    "text_color": "",
    "default_materials": "",
    "lesson_sort": "updated",
    "assess_sort": "updated",
    "logo_place": "left",
    # Board Designer: fonts ("" = the standard one) and where things go on a board slide
    "board_font": "",
    "board_head_font": "",       # "" = same as board_font
    "board_title_align": "left",
    "board_codes_place": "right",
    "board_layout": "auto",
    "board_panels": "cards",
    "board_big_text": False,
    "board_sides": {},           # section key -> "left" / "right", where the teacher moved it
    "board_order": [],           # section keys in the teacher's order ([] = the usual order)
    "class_periods": [],
    "backup_dir": "",
    "last_backup": "",
}

BACKUP_KIND = "chalkboard-backup"
# settings that belong to this computer, so importing a backup made on another one keeps them
LOCAL_SETTINGS = ("export_dir", "backup_dir", "last_backup")
MY_STANDARDS = "My Own Standards"  # the custom standards you typed in, when exported to share
SAFETY_BACKUPS = 10  # automatic copies kept in the data folder from before each import

# list sort orders: key -> menu label
SORTS = {"updated": "DATE MODIFIED", "created": "DATE CREATED", "title": "TITLE (A-Z)", "unit": "UNIT"}

BOARD_SECTIONS = [
    # lesson key, heading on the slide, column
    ("question", "Essential Question", "top"),
    ("standards", "Standards", "left"),
    ("targets", "I Can...", "left"),
    ("success", "Success Criteria", "left"),
    ("bell_ringer", "Bell Ringer", "right"),
    ("materials", "Materials", "right"),
    ("vocab", "Words to Know", "right"),
    ("homework", "Homework", "right"),
    ("closure", "Exit Ticket", "right"),
    ("class_codes", "Class Codes", "right"),  # from Settings > Class Periods, not the lesson
]
BOARD_SIDES = {"top": "across the top", "left": "left side", "right": "right side"}
# Board Designer choices: setting -> {value: label}, first value is the default
BOARD_CHOICES = {
    "board_layout": {"auto": "Balanced (moves sections to fit)", "sides": "Keep each section on its side",
                     "one": "One column"},
    "board_panels": {"cards": "Cards", "outline": "Outlined boxes", "chalk": "Plain chalk (no boxes)"},
    "board_title_align": {"left": "Left, date on the right", "center": "Centered, date underneath"},
    "board_codes_place": {"right": "Bottom right", "left": "Bottom left"},
}


def board_sections(settings):
    """BOARD_SECTIONS in the teacher's order, with the sides they picked: [(key, label, side)]."""
    sides = settings.get("board_sides") or {}
    order = settings.get("board_order") or []
    rank = {k: i for i, k in enumerate(order)}
    out = [(k, label, sides.get(k, col) if col != "top" and sides.get(k) in ("left", "right") else col)
           for k, label, col in BOARD_SECTIONS]
    default = [k for k, _, _ in BOARD_SECTIONS]
    return sorted(out, key=lambda x: (x[2] != "top", rank.get(x[0], len(rank) + default.index(x[0]))))


def move_board_section(settings, key, step):
    """Move a section up (-1) or down (+1) in the board's order. Returns True if it moved."""
    keys = [k for k, _, col in board_sections(settings) if col != "top"]
    if key not in keys:
        return False
    i = keys.index(key)
    j = i + step
    if not 0 <= j < len(keys):
        return False
    keys[i], keys[j] = keys[j], keys[i]
    settings["board_order"] = keys
    return True
LOGO_PLACES = {"left": "LEFT OF THE TITLE", "right": "TOP RIGHT CORNER"}
LOGO_FILE = "logo.json"


def periods_for(settings, lesson):
    """The class periods a lesson's board slides are made for: ones with codes, for its course or any course."""
    course = (lesson.get("course") or "").strip().lower()
    return [p for p in settings.get("class_periods") or []
            if (p.get("codes") or "").strip() and (p.get("course") or "").strip().lower() in ("", course)]
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


def clean_path(path):
    """A path typed or dragged into the terminal: drop surrounding quotes and backslash-escaped spaces."""
    path = (path or "").strip()
    if len(path) > 1 and path[0] == path[-1] and path[0] in "'\"":
        path = path[1:-1]
    return os.path.expanduser(path.replace("\\ ", " "))


def default_backup_dir():
    return os.path.join(default_export_dir(), "Backups")


def write_private(path, obj):
    """Write JSON atomically, readable only by this user (lessons, names, school)."""
    tmp = path + ".tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with open(fd, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=1, ensure_ascii=False)
    try:
        os.chmod(tmp, 0o600)  # in case the .tmp was left over from before
    except OSError:
        pass
    os.replace(tmp, path)


def normalize_data(d):
    """Fill in anything an older (or hand-copied) data file is missing."""
    settings = dict(DEFAULT_SETTINGS)
    if isinstance(d.get("settings"), dict) and d["settings"]:
        d["_raw_settings"] = d["settings"]
    settings.update(d.get("settings") or {})
    d["settings"] = settings
    for k in ("lessons", "assessments", "custom_standards"):
        if not isinstance(d.get(k), list):
            d[k] = []
    for x in d["lessons"] + d["assessments"]:
        x.setdefault("created", x.get("updated", ""))
    for l in d["lessons"]:
        l.setdefault("assessments", [])
    old = d.get("_raw_settings")
    if old is not None and old.get("board_v", 1) < 2:
        on = settings["board_sections"]
        for key, after in (("question", None), ("vocab", "success")):
            if key not in on:
                on.insert(on.index(after) + 1 if after in on else (0 if after is None else len(on)), key)
    d.pop("_raw_settings", None)
    return d


def read_backup(path):
    """A backup file, or a plain data.json copied from another computer -> {"data", "standards", "made"}.
    Raises ValueError with a message for the user."""
    path = clean_path(path)
    if not os.path.isfile(path):
        raise ValueError(f"NO FILE AT {path}")
    try:
        with open(path, encoding="utf-8-sig") as f:
            raw = json.load(f)
    except (OSError, UnicodeDecodeError, ValueError) as e:
        raise ValueError(f"COULDN'T READ {os.path.basename(path)}: {e}")
    if isinstance(raw, dict) and raw.get("kind") == BACKUP_KIND and isinstance(raw.get("data"), dict):
        data, standards, made = raw["data"], raw.get("standards") or [], raw.get("made", "")
    elif isinstance(raw, dict) and ("lessons" in raw or "assessments" in raw):
        data, standards, made = raw, [], ""
    else:
        raise ValueError(f"{os.path.basename(path)} ISN'T A CHALKBOARD BACKUP")
    if not all(isinstance(d, dict) and d.get("subject") and isinstance(d.get("standards"), list) for d in standards):
        raise ValueError(f"THE STANDARDS IN {os.path.basename(path)} ARE DAMAGED")
    logo = raw.get("logo") if data is not raw else None
    try:
        logo = from_json(logo) if logo else None
    except ValueError:
        logo = None  # a damaged logo shouldn't keep the lessons from coming back
    return {"data": normalize_data(data), "standards": standards, "made": made, "logo": logo}


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
    ("question", "Essential Question", "text"),
    ("targets", "Learning Targets (I can...)", "text"),
    ("success", "Success Criteria", "text"),
    ("vocab", "Vocabulary", "vocab"),
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
    ("chart", "CHART / ORGANIZER", "chart, Venn diagram, idea web, sequence, Frayer, plot"),
    ("passage", "READING PASSAGE", "a text block with line numbers (not scored)"),
    ("section", "SECTION HEADER", "titled part with directions (not scored)"),
]
TYPE_LABEL = {t: label for t, label, _ in QUESTION_TYPES}
TYPE_TAG = {"mc": "MC", "tf": "T/F", "short": "SA", "essay": "ER", "fill": "FIB",
            "match": "MAT", "chart": "ORG", "passage": "TEXT", "section": "SECT"}

# Graphic organizers students fill in. Each starting point sets a layout and its labels;
# the teacher can change any of them afterward.
#   layout: table (rows & columns, optional row-label column), venn, web, sequence, frayer, plot
#   cols/rows: table size; rows is also the number of web bubbles or sequence boxes
#   heads: column headings / circle / bubble / box / corner / stage labels; side: row labels
FRAYER_HEADS = ["Definition", "Characteristics", "Examples", "Non-Examples"]
PLOT_HEADS = ["Exposition", "Rising Action", "Climax", "Falling Action", "Resolution"]
CHART_PRESETS = [
    # key, label, settings
    ("chart", "Chart (rows & columns)", {"layout": "table", "cols": 3, "rows": 4, "heads": [], "sidecol": False}),
    ("matrix", "Matrix (labeled rows & columns)", {"layout": "table", "cols": 3, "rows": 3, "heads": [],
                                                    "sidecol": True}),
    ("tchart", "T-Chart", {"layout": "table", "cols": 2, "rows": 1, "heads": ["", ""], "sidecol": False}),
    ("kwl", "K-W-L", {"layout": "table", "cols": 3, "rows": 1, "sidecol": False,
                      "heads": ["What I Know", "What I Want to Know", "What I Learned"]}),
    ("cause", "Cause & Effect", {"layout": "table", "cols": 2, "rows": 4, "heads": ["Cause", "Effect"],
                                 "sidecol": False}),
    ("swbst", "Somebody-Wanted-But-So-Then", {"layout": "table", "cols": 5, "rows": 1, "sidecol": False,
                                              "heads": ["Somebody", "Wanted", "But", "So", "Then"]}),
    ("venn2", "Venn diagram (2 circles)", {"layout": "venn", "circles": 2, "heads": []}),
    ("venn3", "Venn diagram (3 circles)", {"layout": "venn", "circles": 3, "heads": []}),
    ("web", "Idea web", {"layout": "web", "rows": 6, "heads": [], "center": ""}),
    ("sequence", "Sequence / flow chart", {"layout": "sequence", "rows": 4, "heads": ["First", "Next", "Then", "Last"]}),
    ("frayer", "Frayer model", {"layout": "frayer", "heads": list(FRAYER_HEADS), "center": ""}),
    ("plot", "Plot diagram", {"layout": "plot", "heads": list(PLOT_HEADS)}),
]
CHART_DIRECTIONS = {"table": "Complete the chart.", "venn": "Compare and contrast using the Venn diagram.",
                    "web": "Complete the idea web.", "sequence": "Fill in each step in order.",
                    "frayer": "Complete the Frayer model.", "plot": "Complete the plot diagram."}
CHART_SIZES = [0, -1, 6, 8, 10, 12, 15, 18, 20, 24, 30]   # 0 = automatic, -1 = fill the rest of the page


def chart_preset(q, key):
    """Apply a starting point to a chart question (keeps its directions, size, points, standard)."""
    for k, _, settings in CHART_PRESETS:
        if k == key:
            q.update(copy.deepcopy(settings), preset=key)
    return q


def chart_size_label(n):
    return {0: "Automatic", -1: "Fill the rest of the page"}.get(n, f"{n} lines (about {round(n / 3, 1):g} in)")


def new_id():
    return uuid.uuid4().hex[:10]


def now():
    return time.strftime("%Y-%m-%d %H:%M")


# Always in the editors; every other lesson section can be hidden in Settings > Lesson Sections.
FIXED_FIELDS = ("title", "unit", "course", "date", "duration", "standards")
VOCAB_HINT = "One word per line: word: definition"


def has_content(lesson, key):
    v = lesson.get(key)
    return bool(v) if isinstance(v, list) else bool((v or "").strip())


def shown_fields(settings, lesson):
    """The lesson sections the editors show: hidden ones come back while they have something in them,
    so nothing that prints is ever out of sight."""
    hide = set(settings.get("lesson_hide") or [])
    return [f for f in LESSON_FIELDS if f[0] not in hide or has_content(lesson, f[0])]


def vocab_pairs(text):
    """'word: definition' lines (or 'word - definition') -> [(word, definition)]. Bullets are fine."""
    out = []
    for line in (text or "").split("\n"):
        s = re.sub(r"^[-*•]\s+", "", line.strip())
        if not s:
            continue
        m = re.match(r"^(.+?)\s*(?::|\s[-–—]\s)\s*(.*)$", s)
        word, definition = (m.group(1), m.group(2)) if m else (s, "")
        out.append((word.strip(), definition.strip()))
    return out


def new_lesson(settings):
    d = {k: "" for k, _, kind in LESSON_FIELDS if kind in ("text", "line", "vocab")}
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
    elif qtype == "chart":
        q.update(layout="table", cols=3, rows=4, circles=2, heads=[], side=[], sidecol=False, center="",
                 lines=0, answer="", points=0, preset="chart")
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


def parse_points(s, old):
    """Typed points as an int or float; old if it isn't a number."""
    try:
        v = float(s)
        return int(v) if v.is_integer() else v
    except (TypeError, ValueError):
        return old


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
        self.data = normalize_data(d)

    def save(self):
        os.makedirs(self.dir, exist_ok=True)
        if not self._backed_up and os.path.exists(self.path):
            shutil.copy2(self.path, self.path + ".bak")
            self._backed_up = True
        write_private(self.path, self.data)
        if self._backed_up and os.path.exists(self.path + ".bak"):
            try:
                os.chmod(self.path + ".bak", 0o600)
            except OSError:
                pass

    def export_dir(self):
        return os.path.expanduser(self.settings.get("export_dir") or default_export_dir())

    # -- school logo (board slides)
    def logo(self):
        """The school logo for board slides, or None. Cached."""
        if not hasattr(self, "_logo"):
            self._logo = None
            try:
                with open(os.path.join(self.dir, LOGO_FILE), encoding="utf-8") as f:
                    self._logo = from_json(json.load(f))
            except FileNotFoundError:
                pass
            except (OSError, ValueError) as e:
                self.warning = f"?COULD NOT READ THE SCHOOL LOGO ({e})"
        return self._logo

    def set_logo(self, path=None, img=None):
        """Use a PNG or JPEG file (or an already-read image) as the logo; raises ValueError."""
        if img is None:
            path = clean_path(path)
            if not os.path.isfile(path):
                raise ImageError(f"NO FILE AT {path}")
            img = read_image(path)
            img["name"] = os.path.basename(path)
        os.makedirs(self.dir, exist_ok=True)
        write_private(os.path.join(self.dir, LOGO_FILE), to_json(img))
        self._logo = img
        return img

    def remove_logo(self):
        try:
            os.remove(os.path.join(self.dir, LOGO_FILE))
        except FileNotFoundError:
            pass
        self._logo = None

    # -- backups
    def backup_dir(self):
        return os.path.expanduser(self.settings.get("backup_dir") or default_backup_dir())

    def backup(self, folder=None, remember=True):
        """Save everything (lessons, assessments, settings, your standards) into one dated file in folder.
        Returns its path; raises OSError."""
        from . import __version__
        folder = clean_path(folder) if folder else self.backup_dir()
        if os.path.exists(folder) and not os.path.isdir(folder):
            raise OSError(f"{folder} IS A FILE, NOT A FOLDER")
        os.makedirs(folder, exist_ok=True)
        stamp = time.strftime("%Y-%m-%d-%H%M")
        name, n = f"chalkboard-backup-{stamp}.json", 2
        while os.path.exists(os.path.join(folder, name)):
            name, n = f"chalkboard-backup-{stamp}-{n}.json", n + 1
        path = os.path.join(folder, name)
        if remember:
            self.settings["backup_dir"] = folder
            self.settings["last_backup"] = now()
        standards = [{k: v for k, v in doc.items() if k != "file"} for doc in self.load_kas() if doc.get("file")]
        logo = self.logo()
        write_private(path, {"kind": BACKUP_KIND, "format": 1, "app": __version__, "made": now(),
                             "data": self.data, "standards": standards, "logo": to_json(logo) if logo else None})
        if remember:
            self.save()
        return path

    def import_backup(self, path, replace=False):
        """Bring in a backup. replace=False adds lessons, assessments, and standards you don't have (and newer
        copies of ones you do); replace=True swaps everything for the backup. Either way, what you had is
        backed up first into the data folder's backups folder. Returns a summary dict; raises ValueError."""
        b = read_backup(path)
        try:
            safety = self.backup(os.path.join(self.dir, "backups"), remember=False)
            folder = os.path.dirname(safety)
            old = sorted((os.path.join(folder, n) for n in os.listdir(folder) if n.startswith("chalkboard-backup-")),
                         key=os.path.getmtime)
            for p in old[:-SAFETY_BACKUPS]:
                os.remove(p)
        except OSError as e:
            raise ValueError(f"COULDN'T BACK UP YOUR WORK FIRST, SO NOTHING WAS IMPORTED ({e})")
        new, mine = b["data"], self.data
        got = {"lessons": 0, "assessments": 0, "updated": 0, "standards": 0, "subjects": 0, "safety": safety}
        if replace:
            keep = {k: mine["settings"][k] for k in LOCAL_SETTINGS if k in mine["settings"]}
            keep.update({k: v for k, v in mine["settings"].items() if k.startswith("gui_")})
            # change the dicts in place: the screens hold on to the settings dict
            mine["settings"].clear()
            mine["settings"].update(new["settings"], **keep)
            for k in ("lessons", "assessments", "custom_standards"):
                mine[k] = new[k]
            for k, v in new.items():
                if k not in ("settings", "lessons", "assessments", "custom_standards"):
                    mine[k] = v
            got.update(lessons=len(new["lessons"]), assessments=len(new["assessments"]),
                       standards=len(new["custom_standards"]))
            for doc in self.load_kas():
                if doc.get("file"):
                    os.remove(doc["file"])
            self.reload_standards(rescan=True)
        else:
            for k in ("lessons", "assessments"):
                have = {x["id"]: i for i, x in enumerate(mine[k]) if x.get("id")}
                for x in new[k]:
                    i = have.get(x.get("id"))
                    if i is None:
                        mine[k].append(x)
                        got[k] += 1
                    elif (x.get("updated") or "") > (mine[k][i].get("updated") or ""):
                        mine[k][i] = x
                        got["updated"] += 1
            codes = {c["code"] for c in mine["custom_standards"]}
            for c in new["custom_standards"]:
                if c.get("code") and c["code"] not in codes:
                    mine["custom_standards"].append(c)
                    codes.add(c["code"])
                    got["standards"] += 1
        have = set(self.imported_subjects())
        os.makedirs(self.standards_dir(), exist_ok=True)
        for doc in b["standards"]:
            if doc["subject"] in have:
                continue
            doc = {k: v for k, v in doc.items() if k != "file"}
            write_private(os.path.join(self.standards_dir(), subject_filename(doc["subject"], self.standards_dir())), doc)
            got["subjects"] += 1
        if b["logo"] and (replace or not self.logo()):
            self.set_logo(img=b["logo"])
        self.save()
        self.reload_standards(rescan=True)
        return got

    # -- lessons <-> assessments
    def assessment(self, aid):
        return next((a for a in self.data["assessments"] if a["id"] == aid), None)

    def attached(self, lesson):
        """The assessments and worksheets linked to a lesson, in the lesson's order."""
        return [a for a in map(self.assessment, lesson.get("assessments") or []) if a]

    def vocab_quiz(self, lesson):
        """A new quiz with one Matching question built from the lesson's vocabulary, linked to the lesson.
        None when there are fewer than two words with definitions."""
        pairs = [[w, d] for w, d in vocab_pairs(lesson.get("vocab")) if w and d]
        if len(pairs) < 2:
            return None
        title = (lesson.get("title") or "").strip()
        q = new_question("match")
        q.update(prompt="Match each word with its definition.", pairs=pairs)
        a = new_assessment(self.settings, "Quiz")
        a.update(title=f"{title} Vocabulary Quiz" if title else "Vocabulary Quiz", unit=lesson.get("unit", ""),
                 course=lesson.get("course", ""), grades=lesson.get("grades") or a["grades"], questions=[q])
        self.data["assessments"].append(a)
        lesson.setdefault("assessments", []).append(a["id"])
        lesson["updated"] = now()
        return a

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
                index = json.loads(pkgutil.get_data(PACKAGE, STANDARDS_DIR + "/index.json").decode("utf-8"))
                for s in index["subjects"]:
                    doc = json.loads(pkgutil.get_data(PACKAGE, f"{STANDARDS_DIR}/{s['file']}").decode("utf-8"))
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

    def shareable_subjects(self):
        """What a teacher can export to share: subjects they imported, then their own standards.
        Standards bundled with the app are never exported."""
        return self.imported_subjects() + ([MY_STANDARDS] if self.data["custom_standards"] else [])

    def export_standards(self, folder, subjects):
        """Save subjects (from shareable_subjects) as a JSON file another teacher can import with
        Standards Library > Import. Returns its path; raises OSError or ValueError."""
        docs = []
        for subj in subjects:
            if subj == MY_STANDARDS:
                docs.append({"subject": MY_STANDARDS, "source": f"Written by a teacher, shared {time.strftime('%Y-%m-%d')}.",
                             "standards": [{"code": c["code"], "grades": "" if c.get("grades") == "Custom" else c.get("grades", ""),
                                            "strand": "", "cluster": "", "text": c["text"], "subs": []}
                                           for c in self.data["custom_standards"]]})
                continue
            doc = next((d for d in self.load_kas() if d["subject"] == subj and d.get("file")), None)
            if doc is None:
                raise ValueError(f"{subj} ISN'T A SUBJECT YOU IMPORTED")
            docs.append({k: v for k, v in doc.items() if k != "file"})
        if not docs:
            raise ValueError("NOTHING TO EXPORT")
        folder = clean_path(folder)
        if os.path.exists(folder) and not os.path.isdir(folder):
            raise OSError(f"{folder} IS A FILE, NOT A FOLDER")
        os.makedirs(folder, exist_ok=True)
        if len(docs) == 1:
            name = subject_filename(docs[0]["subject"] + " standards", folder)
        else:
            name = subject_filename(f"standards {time.strftime('%Y-%m-%d')}", folder)
        path = os.path.join(folder, name)
        tmp = path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(docs[0] if len(docs) == 1 else docs, f, indent=1, ensure_ascii=False)
        os.replace(tmp, path)
        return path

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
            raw = json.loads(pkgutil.get_data(PACKAGE, GOOD_THINGS_FILE).decode("utf-8"))
            self._good_things = [GOOD_THINGS_PREFIX + p for p in raw["prompts"]]
        used = {(l.get("bell_ringer") or "").strip() for l in self.data["lessons"]} | {(current or "").strip()}
        fresh = [g for g in self._good_things if g not in used]
        return random.choice(fresh or [g for g in self._good_things if g != (current or "").strip()])
