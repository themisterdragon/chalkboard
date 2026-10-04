"""Turn lessons and assessments into a format-neutral list of printable blocks.

Every exporter (PDF, DOCX, TXT) renders the same block list:

  title, subtitle, fields, h1, p, bullet, kv, q, choice, choice_inline,
  lines, blank, box, answer, match, organizer, passage, space, rule

Advanced Mode > Page Layouts changes how each kind of page is put together (see store.PAGE_LAYOUTS),
so title and subtitle blocks may carry "align" ("left"/"center") and h1 blocks "rule" (False = no line).

(makeup_doc also uses "check", a checkbox step heading, and "pad" to indent p/bullet blocks.
The fixed-layout sheets use "grid", a ruled chart, and "days", a stack of day boxes; both
fill the rest of the page. "pagebreak" starts a new page.)
"""

import copy
import random
import re

from .organizers import auto_lines
from .store import (ANNOTATION, ANNOTATION_COLS, BELL_SHEET, MAKEUP_HELD, board_sections, periods_for, CHART_DIRECTIONS, LESSON_FIELDS,
                    WEEKDAYS, fmt_points, layout_fields, page_layout, points_of, vocab_pairs)

LETTERS = "ABCDEFGHIJ"
BLANK_RE = re.compile(r"_{3,}")
ANCHORED_CHOICE = re.compile(r"\b(all|none|both|neither)\b.*\babove\b|^(both|neither)\b|^[A-D] and [A-D]\b", re.I)


def text_blocks(value, style="normal"):
    """Split free text into paragraphs and bullets ('- ' or '* ' lines)."""
    out = []
    for line in (value or "").split("\n"):
        s = line.strip()
        if not s:
            continue
        m = re.match(r"^[-*•]\s+(.*)", s)
        if m:
            indent = 1 if line.startswith(("  ", "\t")) else 0
            out.append({"t": "bullet", "text": m.group(1), "indent": indent})
        else:
            out.append({"t": "p", "text": s, "style": style})
    return out


def vocab_blocks(value):
    """Vocabulary as bold 'word:' labels and their definitions."""
    return [{"t": "kv", "label": w + ":", "text": d} if d else {"t": "p", "text": w, "style": "bold"}
            for w, d in vocab_pairs(value)]


def norm_blanks(s):
    return BLANK_RE.sub("_" * 14, s)


# the usual widths of the name lines (anything else is sized from its labels)
FIELD_SHARES = {("Name", "Date Missed", "Due"): (0.46, 0.30, 0.24), ("Turned In", "Teacher Initials"): (0.5, 0.5),
                ("Name", "Date"): (0.62, 0.38), ("Name", "Period"): (0.7, 0.3), ("Name", "Date", "Period"): None}


def fields_block(items):
    """A row of 'Label: ______' blanks, or None when there are no labels."""
    if not items:
        return None
    shares = FIELD_SHARES.get(tuple(items), ())
    if shares == ():
        weights = [2.4 if "name" in x.lower() else 1 + len(x) / 12 for x in items]
        shares = tuple(w / sum(weights) for w in weights)
    return {"t": "fields", "items": items, "shares": shares} if shares else {"t": "fields", "items": items}


class Page:
    """Builds a page the way Advanced Mode > Page Layouts says: the name line, title, headings, footer."""

    def __init__(self, settings, kind):
        self.lay = page_layout(settings, kind)
        self.align = self.lay["title_align"]

    def name_line(self):
        b = fields_block(layout_fields(self.lay["name_fields"]))
        return [b] if b else []

    def top(self, title_blocks, before=(), after=(), name=True):
        """The name line (at the very top or under the title) around the title blocks."""
        for b in title_blocks:
            if b["t"] in ("title", "subtitle"):
                b["align"] = self.align
        name = self.name_line() if name else []
        if self.lay["name_place"] == "top":
            return name + list(before) + title_blocks + list(after)
        return title_blocks + name + list(after)

    def h1(self, text):
        style = self.lay["heading_style"]
        return {"t": "h1", "text": text.upper() if style == "caps" else text, "rule": style != "plain"}

    def head(self, key, usual):
        return (self.lay.get("head_" + key) or "").strip() or usual

    def footer(self, usual):
        if not self.lay["show_footer"]:
            return ""
        return self.lay["footer_text"].strip() or usual


# ------------------------------------------------------------------ lessons --

def lesson_doc(lesson, store):
    st = store.settings
    pg = Page(st, "lesson")
    title = [{"t": "title", "text": lesson.get("title") or "Untitled Lesson"}]
    meta = [("Course", lesson.get("course")), ("Unit", lesson.get("unit")),
            ("Date", lesson.get("date")), ("Duration", lesson.get("duration"))]
    sub = "   |   ".join(f"{k}: {v}" for k, v in meta if v)
    if sub and pg.lay["show_meta"]:
        title.append({"t": "subtitle", "text": sub})
    blocks = pg.top(title)
    if lesson.get("standards"):
        blocks.append(pg.h1(pg.head("standards", "Standards")))
        if pg.lay["std_text"] == "codes":
            blocks.append({"t": "p", "text": ", ".join(lesson["standards"])})
        else:
            for code in lesson["standards"]:
                blocks.append({"t": "kv", "label": code, "text": store.std_text(code) or "(not in library)"})
    for key, label, kind in LESSON_FIELDS:
        if kind == "attached" and store.attached(lesson):
            blocks.append(pg.h1(pg.head(key, label)))
            blocks += [{"t": "bullet", "text": handout_name(a)} for a in store.attached(lesson)]
        if kind not in ("text", "vocab") or not (lesson.get(key) or "").strip():
            continue
        blocks.append(pg.h1(pg.head(key, label)))
        blocks += vocab_blocks(lesson[key]) if kind == "vocab" else text_blocks(lesson[key])
    footer = " - ".join(x for x in (st.get("teacher"), lesson.get("course"), st.get("school")) if x)
    return {"title": lesson.get("title") or "Untitled Lesson", "footer": pg.footer(footer), "blocks": blocks}


# Student-facing lesson fields for the make-up sheet, in class order: (key, gets writing lines).
# Their step headings are in store.PAGE_LAYOUTS["makeup"]. Differentiation, checks,
# and notes are teacher-only and never appear.
MAKEUP_STEPS = [("bell_ringer", True), ("instruction", False), ("guided", False), ("independent", False),
                ("closure", True), ("homework", False)]


def makeup_doc(lesson, store):
    """A one-stop sheet for a student who missed class: goals, materials, then checklist steps,
    followed by the lesson's linked worksheets and assignments. Quizzes and tests stay with the teacher."""
    st = store.settings
    pg = Page(st, "makeup")
    lay = pg.lay
    title = lesson.get("title") or "Untitled Lesson"
    head = [{"t": "title", "text": "Make-Up Work: " + title}]
    sub = "   |   ".join(x for x in (lesson.get("course"), lesson.get("unit"), lesson.get("date")) if x)
    if sub:
        head.append({"t": "subtitle", "text": sub})
    when = f" on {lesson['date']}" if lesson.get("date") else ""
    ask = f" Questions? Ask {st['teacher']}." if st.get("teacher") else " Questions? Ask your teacher."
    note = lay["welcome"].strip() or (f"We missed you! This is what we did in class{when}. Work through each step "
                                      f"below, check it off when you finish, and turn everything in by the due "
                                      f"date.{ask}")
    blocks = pg.top(head, after=text_blocks(note, style="italic"))
    linked = store.attached(lesson)
    held = [a for a in linked if a.get("kind") in MAKEUP_HELD]
    classwork = [a for a in linked if a not in held]
    handouts = [{"t": "bullet", "text": handout_name(a)} for a in classwork]
    for key in ("question", "targets", "success", "vocab", "materials"):
        extra = handouts if key == "materials" else []
        if (lesson.get(key) or "").strip() or extra:
            blocks.append(pg.h1(pg.head(key, key.title())))
            blocks += (vocab_blocks if key == "vocab" else text_blocks)(lesson.get(key)) + extra
            if key == "targets" and lesson.get("standards"):
                blocks.append({"t": "p", "style": "small", "text": "Standards: " + ", ".join(lesson["standards"])})
    steps = [(k, lines) for k, lines in MAKEUP_STEPS if (lesson.get(k) or "").strip()]
    if steps:
        blocks.append(pg.h1(pg.head("steps", "Steps")))
    for i, (key, lines) in enumerate(steps, 1):
        blocks.append({"t": "check", "text": f"{i}. {pg.head(key, key.title())}"})
        blocks += [dict(b, pad=18) for b in text_blocks(lesson[key])]
        if lines and lay["write_lines"]:
            blocks.append({"t": "lines", "n": lay["write_lines"]})
        blocks.append({"t": "space", "h": 6})
    if held:
        who = st.get("teacher") or "your teacher"
        blocks.append({"t": "check", "text": f"{len(steps) + 1}. {pg.head('see_me', 'See Me')}"})
        blocks.append({"t": "p", "pad": 18, "text": f"Set up a time with {who} to take:"})
        blocks += [{"t": "bullet", "pad": 18, "text": handout_name(a)} for a in held]
        blocks.append({"t": "space", "h": 6})
    signoff = fields_block(layout_fields(lay["signoff"]))
    if signoff:
        blocks += [{"t": "space", "h": 14}, {"t": "rule"}, signoff]
    if lay["classwork"]:  # each worksheet on its own pages, student copy only
        for a in classwork:
            blocks += [{"t": "pagebreak"}] + assessment_doc(a, store)["blocks"]
    footer = " - ".join(x for x in (st.get("teacher"), lesson.get("course"), st.get("school")) if x)
    return {"title": "Make-Up Work - " + title, "footer": pg.footer(footer), "blocks": blocks}


def code_blocks(codes):
    """'Google Classroom: abc123' lines -> label/code pairs; other lines stay as they are."""
    out = []
    for line in (codes or "").split("\n"):
        label, sep, code = line.partition(":")
        if sep and label.strip() and code.strip():
            out.append({"t": "kv", "label": label.strip() + ":", "text": code.strip()})
        elif line.strip():
            out.append({"t": "p", "text": line.strip()})
    return out


def board_doc(lesson, store, period=None, settings=None):
    """The at-a-glance pieces of a lesson for a classroom display (see export_png).
    period: one of Settings > Class Periods; its codes fill the Class Codes panel.
    settings: instead of the store's (the board designer previews changes before saving them)."""
    st = settings if settings is not None else store.settings
    wanted = st.get("board_sections") or []
    cols = {"top": [], "left": [], "right": []}
    for key, label, col in board_sections(st):
        if key not in wanted:
            continue
        if key == "class_codes":
            if period and code_blocks(period.get("codes")):
                cols[col].append({"key": key, "label": label, "blocks": code_blocks(period["codes"]), "col": col})
            continue
        if key == "standards":
            codes = lesson.get("standards") or []
            if not codes:
                continue
            short = [{"t": "p", "text": ", ".join(codes)}]
            if st.get("board_std_text", True):
                blocks = [{"t": "kv", "label": c, "text": store.std_text(c)} for c in codes]
            else:
                blocks = short
            # short: what the slide falls back to (codes only) when the full text would be too small to read
            cols[col].append({"key": key, "label": label, "blocks": blocks, "short": short, "col": col})
        elif (lesson.get(key) or "").strip():
            blocks = vocab_blocks(lesson[key]) if key == "vocab" else text_blocks(lesson[key])
            cols[col].append({"key": key, "label": label, "blocks": blocks, "col": col})
    meta = "  |  ".join(x for x in (lesson.get("course"), lesson.get("unit"), (period or {}).get("name")) if x)
    footer = " - ".join(x for x in (st.get("teacher"), st.get("school")) if x)
    return {"title": lesson.get("title") or "Untitled Lesson", "date": lesson.get("date") or "",
            "meta": meta, "footer": footer, "top": cols["top"], "left": cols["left"], "right": cols["right"],
            "style": st.get("board_style", "chalk"),
            "colors": (st.get("primary_color", ""), st.get("secondary_color", ""), st.get("text_color", "")),
            "logo": store.logo(), "logo_place": st.get("logo_place", "left"),
            "font": st.get("board_font", ""), "head_font": st.get("board_head_font", ""),
            "title_align": st.get("board_title_align", "left"), "codes_place": st.get("board_codes_place", "right"),
            "layout": st.get("board_layout", "auto"), "panels": st.get("board_panels", "cards"),
            "big_text": st.get("board_big_text", False), "order": st.get("board_order") or []}


SAMPLE_LESSON = {
    "title": "Making a Strong Claim", "date": "", "course": "", "unit": "Argument Writing",
    "question": "What makes people change their minds?",
    "targets": "I can write a claim that takes a clear side.\nI can back it up with **two** reasons.",
    "success": "My claim fits in one sentence.\nEach reason connects to the claim.",
    "bell_ringer": "Finish the sentence: The best school lunch is ___ because...",
    "materials": "Notebook\nClaim sentence starters",
    "vocab": "claim: what you are arguing\nevidence: facts that back it up",
    "homework": "Write one claim about something you care about.",
}


def preview_doc(store, settings=None):
    """A board slide to try designs on: the newest lesson, or a sample when there are none yet."""
    from datetime import date
    lessons = sorted(store.data.get("lessons") or [], key=lambda x: x.get("updated", ""), reverse=True)
    today = date.today()
    lesson = lessons[0] if lessons else dict(SAMPLE_LESSON, date=f"{today:%b} {today.day}, {today.year}",
                                             course=store.settings.get("course", ""))
    st = settings if settings is not None else store.settings
    periods = periods_for(st, lesson)
    return board_doc(lesson, store, periods[0] if periods else None, st)


class _Trying:
    """A store whose settings are a draft, so a layout can be previewed before it's saved."""

    def __init__(self, store, settings):
        self._store, self.settings = store, settings

    def __getattr__(self, name):
        return getattr(self._store, name)


SAMPLE_QUIZ = [
    {"type": "section", "title": "Part One", "prompt": ""},
    {"type": "mc", "prompt": "Which sentence is a claim?", "points": 1, "answer": 1,
     "choices": ["Lunch is at noon.", "Lunch should be longer.", "Lunch has pizza today."]},
    {"type": "short", "prompt": "Give one reason that backs up your claim.", "points": 2, "lines": 3},
]


def layout_sample(store, kind, settings):
    """What a page layout looks like on the teacher's newest lesson or handout of that kind (or a sample)."""
    from .store import SHEET_KINDS, new_assessment
    trying = _Trying(store, settings)
    newest = lambda xs: max(xs, key=lambda x: x.get("updated", "")) if xs else None
    if kind in ("lesson", "makeup"):
        lesson = newest(store.data.get("lessons") or []) or dict(SAMPLE_LESSON, course=settings.get("course", ""),
                                                                 date="Monday", instruction="Notes on claims",
                                                                 closure="Write one claim.")
        return (lesson_doc if kind == "lesson" else makeup_doc)(lesson, trying)
    want = {"annotation": [ANNOTATION], "bell": [BELL_SHEET]}.get(kind)
    a = newest([x for x in store.data.get("assessments") or []
                if (x.get("kind") in want if want else x.get("kind") not in SHEET_KINDS)])
    if a is None:
        a = new_assessment(settings, (want or ["Quiz"])[0])
        a.update(id="sample", title="Making a Strong Claim", instructions="Answer every question.")
        if not want:
            a["questions"] = copy.deepcopy(SAMPLE_QUIZ)
    return assessment_doc(a, trying)


# -------------------------------------------------------------- assessments --

def handout_name(a):
    title = a.get("title") or "Untitled"
    kind = a.get("kind") or ""
    named = kind.split()[0].lower() in title.lower() if kind else True  # "Hamlet Quiz", "Hamlet 4.1 Annotation"
    return title if named else f"{title} ({kind})"


def annotation_doc(a, store):
    """One page: Name/Date, the assignment as a heading, then a Line / Symbol / Reason chart."""
    st = store.settings
    pg = Page(st, "annotation")
    title = a.get("title") or "Annotation"
    blocks = pg.top([{"t": "title", "text": title}], before=[{"t": "space", "h": 4}], after=[{"t": "space", "h": 8}])
    if blocks and blocks[0]["t"] == "space":  # no name line above it
        blocks.pop(0)
    blocks += text_blocks(a.get("instructions"), style="italic")
    blocks.append({"t": "grid", "cols": grid_columns(layout_fields(pg.lay["columns"])), "rows": int(a.get("rows") or 10)})
    footer = " - ".join(x for x in (st.get("teacher"), a.get("course")) if x)
    return {"title": title, "footer": pg.footer(footer), "blocks": blocks}


def grid_columns(names):
    """Column headings -> [(heading, share of the width)]; the usual three keep their usual widths,
    and the teacher's own columns share the page evenly."""
    if [c for c, _ in ANNOTATION_COLS] == names or not names:
        return ANNOTATION_COLS
    return [(n, 1 / len(names)) for n in names]


def bell_sheet_doc(a, store):
    """Two pages (print double-sided): Monday-Friday boxes for one week per side."""
    st = store.settings
    pg = Page(st, "bell")
    title = a.get("title") or "Bell Ringers"
    weeks = (a.get("weeks") or []) + ["", ""]
    days = (a.get("days") or []) + [""] * 10
    blocks = []
    for w in range(2):
        head = [{"t": "title", "text": title}]
        if weeks[w].strip():
            head.append({"t": "subtitle", "text": "Week of " + weeks[w].strip()})
        else:
            head.append({"t": "fields", "items": ["Week of"], "shares": (0.45,), "center": pg.align == "center"})
        if w:
            blocks.append({"t": "pagebreak"})
            for b in head:
                if b["t"] in ("title", "subtitle"):
                    b["align"] = pg.align
            blocks += head
        else:
            top = pg.top(head, after=[])
            if pg.lay["name_place"] == "top" and top[0]["t"] == "fields":
                top.insert(1, {"t": "space", "h": 4})
            blocks += top + text_blocks(a.get("instructions"), style="italic")
        blocks.append({"t": "days", "days": [(WEEKDAYS[i], days[w * 5 + i]) for i in range(5)]})
    footer = " - ".join(x for x in (st.get("teacher"), a.get("course")) if x)
    return {"title": title, "footer": pg.footer(footer), "blocks": blocks}


def _shuffle_questions(qs, rng):
    """Shuffle questions within runs bounded by passages/sections (those stay put)."""
    out, run = [], []
    for q in qs:
        if q["type"] in ("passage", "section"):
            rng.shuffle(run)
            out += run + [q]
            run = []
        else:
            run.append(q)
    rng.shuffle(run)
    return out + run


def _shuffle_choices(q, rng):
    choices = q.get("choices") or []
    if len(choices) < 3 or any(ANCHORED_CHOICE.search(c) for c in choices):
        return
    order = list(range(len(choices)))
    rng.shuffle(order)
    q["choices"] = [choices[i] for i in order]
    if q.get("answer") is not None and q["answer"] < len(choices):
        q["answer"] = order.index(q["answer"])


def assessment_doc(a, store, version=0, versions=1, key=False):
    if a.get("kind") == ANNOTATION:
        return annotation_doc(a, store)
    if a.get("kind") == BELL_SHEET:
        return bell_sheet_doc(a, store)
    st = store.settings
    qs = copy.deepcopy(a["questions"])
    rng = random.Random(f"{a['id']}:{version}")
    if version > 0:
        qs = _shuffle_questions(qs, rng)
        for q in qs:
            if q["type"] == "mc":
                _shuffle_choices(q, rng)

    pg = Page(st, "assessment")
    title = a.get("title") or "Untitled " + a.get("kind", "Assessment")
    total = sum(points_of(q) for q in qs)
    head = [{"t": "title", "text": title + (" - Answer Key" if key else "")}]
    sub = [a.get("kind"), a.get("course")]
    if versions > 1:
        sub.append(f"Version {LETTERS[version]}")
    if a.get("show_points", True) and total:
        sub.append(f"{fmt_points(total)} point" + ("" if total == 1 else "s"))
    head.append({"t": "subtitle", "text": "   |   ".join(x for x in sub if x)})
    blocks = pg.top(head, name=a.get("show_name", True) and not key)
    if a.get("standards") and (a.get("show_standards") or key):
        blocks.append({"t": "p", "text": "Standards: " + ", ".join(a["standards"]), "style": "small"})
    instr = text_blocks(a.get("instructions"))
    label = pg.lay["directions_label"].strip()
    if instr and label:
        first = instr[0]
        if first["t"] == "p":
            instr[0] = {"t": "kv", "label": label, "text": first["text"]}
        else:
            instr.insert(0, {"t": "kv", "label": label, "text": ""})
    blocks += instr
    number = pg.lay["number_style"]

    num = 0
    quick = []
    body = []
    for q in qs:
        t = q["type"]
        if t == "section":
            body.append(pg.h1(q.get("title") or "Section"))
            body += text_blocks(q.get("prompt"), style="italic")
            continue
        if t == "passage":
            body.append({"t": "passage", "title": q.get("title", ""), "text": q.get("text", ""),
                         "numbered": q.get("numbered", True)})
            continue
        num += 1
        pts = points_of(q)
        prompt = q.get("prompt") or ""
        if t == "chart" and not prompt.strip():
            prompt = CHART_DIRECTIONS.get(q.get("layout"), "Complete the chart.")
        if t == "fill":
            prompt = norm_blanks(prompt) if BLANK_RE.search(prompt) else prompt + " " + "_" * 14
        label = ""
        if a.get("show_points", True) and pts:
            label = f"({fmt_points(pts)} pt{'' if pts == 1 else 's'})"
        if key and q.get("standard"):
            label = (label + f"  [{q['standard']}]").strip()
        body.append({"t": "space", "h": 8})
        body.append({"t": "q", "num": number.replace("1", str(num)), "text": prompt, "points": label})

        if t == "mc":
            choices = q.get("choices") or []
            for i, c in enumerate(choices):
                body.append({"t": "choice", "label": f"{LETTERS[i]}.", "text": c,
                             "correct": key and q.get("answer") == i, "last": i == len(choices) - 1})
            if q.get("answer") is not None and q["answer"] < len(choices):
                quick.append(f"{num}-{LETTERS[q['answer']]}")
        elif t == "tf":
            ans = q.get("answer")
            body.append({"t": "choice_inline", "items": ["True", "False"],
                         "correct": (0 if ans else 1) if (key and ans is not None) else None})
            if ans is not None:
                quick.append(f"{num}-{'T' if ans else 'F'}")
        elif t == "short":
            if key:
                body.append({"t": "answer", "text": q.get("answer") or "Answers will vary."})
            else:
                body.append({"t": "lines", "n": int(q.get("lines") or 3)})
        elif t == "essay":
            if key:
                body.append({"t": "answer", "text": q.get("answer") or "Answers will vary; see rubric."})
            else:
                n = int(q.get("lines") or 12)
                space = q.get("space", "lines")
                body.append({"t": {"lines": "lines", "blank": "blank", "box": "box"}.get(space, "lines"), "n": n})
        elif t == "chart":
            if key:
                body.append({"t": "answer", "text": q.get("answer") or "Answers will vary."})
            else:
                b = {k: copy.deepcopy(q.get(k)) for k in ("layout", "cols", "rows", "circles", "heads", "side",
                                                          "sidecol", "center", "preset")}
                n = int(q.get("lines") or 0)
                b.update(t="organizer", n=auto_lines(q) if n <= 0 else max(n, 4), fill=n < 0)
                body.append(b)
        elif t == "fill":
            if key:
                body.append({"t": "answer", "text": q.get("answer") or "-"})
            if q.get("answer"):
                quick.append(f"{num}-{q['answer']}")
        elif t == "match":
            pairs = [p for p in q.get("pairs", []) if p[0] or p[1]]
            right = list(range(len(pairs)))
            random.Random(f"{a['id']}:{version}:{num}").shuffle(right)
            letters = [LETTERS[right.index(i)] if i < len(LETTERS) else "?" for i in range(len(pairs))]
            body.append({"t": "match", "left": [p[0] for p in pairs],
                         "right": [pairs[i][1] for i in right],
                         "key": letters if key else None})
            if pairs:
                quick.append(f"{num}-" + "".join(letters))

    if key and quick:
        blocks.append({"t": "kv", "label": "Quick key:", "text": ", ".join(quick)})
    blocks += body
    footer = " - ".join(x for x in (st.get("teacher"), a.get("course"), title) if x)
    return {"title": title, "footer": pg.footer(footer), "blocks": blocks}


# ----------------------------------------------------------- curriculum map --

def _span(days):
    """[date] -> 'Aug 12 - Sep 5, 2026', 'Dec 1, 2026 - Jan 8, 2027', or ''."""
    from .store import fmt_date
    if not days:
        return ""
    a, b = min(days), max(days)
    if a == b:
        return fmt_date(a)
    return f"{fmt_date(a, year=a.year != b.year)} - {fmt_date(b)}"


def item_school_year(x):
    """The school year a lesson or assessment belongs to: from its first date, or when it was made."""
    from .store import _stamp_day, lesson_dates, school_year
    days = lesson_dates(x)
    made = days[0] if days else _stamp_day(x.get("created")) or _stamp_day(x.get("updated"))
    return school_year(made) if made else None


def school_years(items):
    """The school years these items belong to, newest first."""
    return sorted({y for y in map(item_school_year, items) if y is not None}, reverse=True)


def curriculum_units(store, course, year=None):
    """A course's plan, unit by unit, in the order it's taught (year: one school year, or None for all):
    [{"unit", "days", "lessons", "assessments", "standards", "questions"}]."""
    import datetime
    from .store import in_class, lesson_dates, unit_sort_key
    mine = lambda x: in_class(x, course) and (year is None or item_school_year(x) == year)
    lessons = [l for l in store.data["lessons"] if mine(l)]
    assessments = [a for a in store.data["assessments"] if mine(a)]
    groups = {}
    for l in lessons:
        groups.setdefault((l.get("unit") or "").strip(), []).append(l)
    out = []
    for unit, ls in groups.items():
        first = lambda l: min(lesson_dates(l) or [datetime.date.max])
        ls.sort(key=lambda l: (first(l), (l.get("title") or "").lower()))
        days = [d for l in ls for d in lesson_dates(l)]
        linked = {aid for l in ls for aid in l.get("assessments") or []}
        tests = [a for a in store.data["assessments"] if a["id"] in linked] + \
                [a for a in assessments if unit and (a.get("unit") or "").strip() == unit and a["id"] not in linked]
        codes = list(dict.fromkeys(c for x in ls + tests for c in x.get("standards") or []))
        questions = list(dict.fromkeys(l["question"].strip() for l in ls if (l.get("question") or "").strip()))
        out.append({"unit": unit, "days": days, "lessons": ls, "assessments": tests, "standards": codes,
                    "questions": questions})
    # dated units in the order they're taught, then the rest by unit number, then lessons with no unit
    out.sort(key=lambda u: (not u["unit"], not u["days"], min(u["days"]) if u["days"] else datetime.date.min,
                            unit_sort_key(u["unit"])))
    return out


def curriculum_doc(store, course, subject="", grades="", year=None):
    """A curriculum map for one class: every unit in order with its dates, essential questions, standards,
    lessons, and assessments, then where each standard is taught (and, with a subject, what isn't yet).
    year: one school year (2026 = 2026-27), or None for every year."""
    from .store import grades_match, lesson_dates, school_year_label
    st = store.settings
    units = curriculum_units(store, course, year)
    name = (course or "Lessons With No Class") + (f" ({school_year_label(year)})" if year else "")
    nl = sum(len(u["lessons"]) for u in units)
    span = _span([d for u in units for d in u["days"]])
    sub = [x for x in (st.get("teacher"), st.get("school"), span,
                       f"{len(units)} unit{'s' if len(units) != 1 else ''}, {nl} lesson{'s' if nl != 1 else ''}") if x]
    blocks = [{"t": "title", "text": "Curriculum Map: " + name}, {"t": "subtitle", "text": "   |   ".join(sub)}]
    title = lambda u: u["unit"] or "Lessons Without a Unit"
    if not units:
        blocks.append({"t": "p", "text": "No lessons are planned for this class yet."})
    else:
        blocks.append({"t": "h1", "text": "At a Glance"})
        for u in units:
            bits = [_span(u["days"]), f"{len(u['lessons'])} lesson{'s' if len(u['lessons']) != 1 else ''}"]
            if u["assessments"]:
                bits.append(f"{len(u['assessments'])} assessment{'s' if len(u['assessments']) != 1 else ''}")
            if u["standards"]:
                bits.append("Standards: " + ", ".join(u["standards"]))
            blocks.append({"t": "kv", "label": title(u) + ":", "text": "   |   ".join(b for b in bits if b)})
    for u in units:
        blocks.append({"t": "h1", "text": title(u) + (f"  ({_span(u['days'])})" if u["days"] else "")})
        if u["questions"]:
            blocks.append({"t": "kv", "label": "Essential Questions:", "text": ""})
            blocks += [{"t": "bullet", "text": q} for q in u["questions"]]
        if u["standards"]:
            blocks.append({"t": "kv", "label": "Standards:", "text": ", ".join(u["standards"])})
        blocks.append({"t": "kv", "label": "Lessons:", "text": ""})
        for l in u["lessons"]:
            when = _span(lesson_dates(l)) or (l.get("date") or "").strip()
            blocks.append({"t": "bullet", "text": (f"{when}: " if when else "") + (l.get("title") or "Untitled")})
        if u["assessments"]:
            blocks.append({"t": "kv", "label": "Assessments:", "text": ""})
            blocks += [{"t": "bullet", "text": handout_name(a)} for a in u["assessments"]]
    # where each standard is taught
    where = {}
    for u in units:
        for l in u["lessons"]:
            for c in l.get("standards") or []:
                where.setdefault(c, {}).setdefault(title(u), 0)
                where[c][title(u)] += 1
        for a in u["assessments"]:
            for c in a.get("standards") or []:
                where.setdefault(c, {}).setdefault(title(u), 0)
    if where:
        blocks.append({"t": "h1", "text": "Standards Coverage"})
        for c in sorted(where, key=_code_key):
            places = ", ".join(f"{t} ({n} lesson{'s' if n != 1 else ''})" if n else f"{t} (assessed)"
                               for t, n in where[c].items())
            blocks.append({"t": "kv", "label": c + ":", "text": places})
    if subject and subject in store.subjects:  # (only a subject in the Standards Library can say what's missing)
        taught = set(where)
        missing = [s for s in store.standards if s["subject"] == subject and not s["parent"] and not s.get("custom")
                   and grades_match(s["grades"], grades or "") and s["code"] not in taught
                   and not any(c.startswith(s["code"]) and store.std_index.get(c, {}).get("parent") == s["code"]
                               for c in taught)]
        blocks.append({"t": "h1", "text": f"Not Taught Yet ({subject}" + (f", grades {grades}" if grades else "") + ")"})
        if missing:
            blocks += [{"t": "kv", "label": s["code"] + ":", "text": store.std_text(s["code"])} for s in missing]
        else:
            blocks.append({"t": "p", "text": "Every standard is taught somewhere in this plan."})
    footer = " - ".join(x for x in (st.get("teacher"), "Curriculum Map", name) if x)
    return {"title": "Curriculum Map - " + name, "footer": footer, "blocks": blocks}


def _code_key(code):
    """RL.9-10.2 before RL.9-10.10."""
    return [(0, int(p), "") if p.isdigit() else (1, 0, p.lower()) for p in re.split(r"(\d+)", code) if p]


def curriculum_rows(store, course, year=None):
    """The same map as spreadsheet rows (one per lesson), for a CSV admins can sort and merge."""
    from .store import lesson_dates, school_year_label
    rows = [["School Year", "Class", "Unit", "Unit Dates", "Lesson", "Lesson Dates", "Essential Question",
             "Standards", "Assessments"]]
    for u in curriculum_units(store, course, year):
        for l in u["lessons"]:
            y = item_school_year(l)
            rows.append([school_year_label(y) if y else "", course, u["unit"], _span(u["days"]),
                         l.get("title") or "Untitled", _span(lesson_dates(l)) or (l.get("date") or "").strip(),
                         (l.get("question") or "").strip(), ", ".join(l.get("standards") or []),
                         "; ".join(handout_name(a) for a in store.attached(l))])
    return rows
