"""Turn lessons and assessments into a format-neutral list of printable blocks.

Every exporter (PDF, DOCX, TXT) renders the same block list:

  title, subtitle, fields, h1, p, bullet, kv, q, choice, choice_inline,
  lines, blank, box, answer, match, organizer, passage, space, rule

(makeup_doc also uses "check", a checkbox step heading, and "pad" to indent p/bullet blocks.
The fixed-layout sheets use "grid", a ruled chart, and "days", a stack of day boxes; both
fill the rest of the page. "pagebreak" starts a new page.)
"""

import copy
import random
import re

from .organizers import auto_lines
from .store import (ANNOTATION, ANNOTATION_COLS, BELL_SHEET, board_sections, periods_for, CHART_DIRECTIONS, LESSON_FIELDS,
                    WEEKDAYS, fmt_points, points_of, vocab_pairs)

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


# ------------------------------------------------------------------ lessons --

def lesson_doc(lesson, store):
    st = store.settings
    blocks = [{"t": "title", "text": lesson.get("title") or "Untitled Lesson"}]
    meta = [("Course", lesson.get("course")), ("Unit", lesson.get("unit")),
            ("Date", lesson.get("date")), ("Duration", lesson.get("duration"))]
    sub = "   |   ".join(f"{k}: {v}" for k, v in meta if v)
    if sub:
        blocks.append({"t": "subtitle", "text": sub})
    if lesson.get("standards"):
        blocks.append({"t": "h1", "text": "Standards"})
        for code in lesson["standards"]:
            blocks.append({"t": "kv", "label": code, "text": store.std_text(code) or "(not in library)"})
    for key, label, kind in LESSON_FIELDS:
        if kind == "attached" and store.attached(lesson):
            blocks.append({"t": "h1", "text": label})
            blocks += [{"t": "bullet", "text": handout_name(a)} for a in store.attached(lesson)]
        if kind not in ("text", "vocab") or not (lesson.get(key) or "").strip():
            continue
        blocks.append({"t": "h1", "text": label})
        blocks += vocab_blocks(lesson[key]) if kind == "vocab" else text_blocks(lesson[key])
    footer = " - ".join(x for x in (st.get("teacher"), lesson.get("course"), st.get("school")) if x)
    return {"title": lesson.get("title") or "Untitled Lesson", "footer": footer, "blocks": blocks}


# Student-facing lesson fields for the make-up sheet, in class order:
# (key, step heading, writing lines for a response). Differentiation, checks,
# and notes are teacher-only and never appear.
MAKEUP_STEPS = [
    ("bell_ringer", "Bell Ringer", 4),
    ("instruction", "What We Learned", 0),
    ("guided", "Class Practice", 0),
    ("independent", "On Your Own", 0),
    ("closure", "Exit Ticket", 4),
    ("homework", "Homework", 0),
]


def makeup_doc(lesson, store):
    """A one-stop sheet for a student who missed class: goals, materials, then checklist steps."""
    st = store.settings
    title = lesson.get("title") or "Untitled Lesson"
    blocks = [{"t": "title", "text": "Make-Up Work: " + title}]
    sub = "   |   ".join(x for x in (lesson.get("course"), lesson.get("unit"), lesson.get("date")) if x)
    if sub:
        blocks.append({"t": "subtitle", "text": sub})
    blocks.append({"t": "fields", "items": ["Name", "Date Missed", "Due"], "shares": (0.46, 0.30, 0.24)})
    when = f" on {lesson['date']}" if lesson.get("date") else ""
    ask = f" Questions? Ask {st['teacher']}." if st.get("teacher") else " Questions? Ask your teacher."
    blocks.append({"t": "p", "style": "italic",
                   "text": f"We missed you! This is what we did in class{when}. Work through each step "
                           f"below, check it off when you finish, and turn everything in by the due date.{ask}"})
    handouts = [{"t": "bullet", "text": handout_name(a)} for a in store.attached(lesson)]
    for key, label in (("question", "Today's Big Question"), ("targets", "Today's Goals"),
                       ("success", "How You'll Know You've Got It"), ("vocab", "Words to Know"),
                       ("materials", "What You'll Need")):
        extra = handouts if key == "materials" else []
        if (lesson.get(key) or "").strip() or extra:
            blocks.append({"t": "h1", "text": label})
            blocks += (vocab_blocks if key == "vocab" else text_blocks)(lesson.get(key)) + extra
            if key == "targets" and lesson.get("standards"):
                blocks.append({"t": "p", "style": "small", "text": "Standards: " + ", ".join(lesson["standards"])})
    steps = [(k, label, n) for k, label, n in MAKEUP_STEPS if (lesson.get(k) or "").strip()]
    if steps:
        blocks.append({"t": "h1", "text": "Your Make-Up Steps"})
    for i, (key, label, n) in enumerate(steps, 1):
        blocks.append({"t": "check", "text": f"{i}. {label}"})
        blocks += [dict(b, pad=18) for b in text_blocks(lesson[key])]
        if n:
            blocks.append({"t": "lines", "n": n})
        blocks.append({"t": "space", "h": 6})
    blocks += [{"t": "space", "h": 14}, {"t": "rule"},
               {"t": "fields", "items": ["Turned In", "Teacher Initials"], "shares": (0.5, 0.5)}]
    footer = " - ".join(x for x in (st.get("teacher"), lesson.get("course"), st.get("school")) if x)
    return {"title": "Make-Up Work - " + title, "footer": footer, "blocks": blocks}


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


# -------------------------------------------------------------- assessments --

def handout_name(a):
    title = a.get("title") or "Untitled"
    kind = a.get("kind") or ""
    named = kind.split()[0].lower() in title.lower() if kind else True  # "Hamlet Quiz", "Hamlet 4.1 Annotation"
    return title if named else f"{title} ({kind})"


def annotation_doc(a, store):
    """One page: Name/Date, the assignment as a heading, then a Line / Symbol / Reason chart."""
    st = store.settings
    title = a.get("title") or "Annotation"
    blocks = [{"t": "fields", "items": ["Name", "Date"], "shares": (0.62, 0.38)},
              {"t": "space", "h": 4}, {"t": "title", "text": title}, {"t": "space", "h": 8}]
    blocks += text_blocks(a.get("instructions"), style="italic")
    blocks.append({"t": "grid", "cols": ANNOTATION_COLS, "rows": int(a.get("rows") or 10)})
    footer = " - ".join(x for x in (st.get("teacher"), a.get("course")) if x)
    return {"title": title, "footer": footer, "blocks": blocks}


def bell_sheet_doc(a, store):
    """Two pages (print double-sided): Monday-Friday boxes for one week per side."""
    st = store.settings
    title = a.get("title") or "Bell Ringers"
    weeks = (a.get("weeks") or []) + ["", ""]
    days = (a.get("days") or []) + [""] * 10
    blocks = []
    for w in range(2):
        if w:
            blocks.append({"t": "pagebreak"})
        else:
            blocks += [{"t": "fields", "items": ["Name", "Period"], "shares": (0.7, 0.3)}, {"t": "space", "h": 4}]
        blocks.append({"t": "title", "text": title})
        if weeks[w].strip():
            blocks.append({"t": "subtitle", "text": "Week of " + weeks[w].strip()})
        else:
            blocks.append({"t": "fields", "items": ["Week of"], "shares": (0.45,), "center": True})
        if w == 0:
            blocks += text_blocks(a.get("instructions"), style="italic")
        blocks.append({"t": "days", "days": [(WEEKDAYS[i], days[w * 5 + i]) for i in range(5)]})
    footer = " - ".join(x for x in (st.get("teacher"), a.get("course")) if x)
    return {"title": title, "footer": footer, "blocks": blocks}


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

    title = a.get("title") or "Untitled " + a.get("kind", "Assessment")
    total = sum(points_of(q) for q in qs)
    blocks = [{"t": "title", "text": title + (" - Answer Key" if key else "")}]
    sub = [a.get("kind"), a.get("course")]
    if versions > 1:
        sub.append(f"Version {LETTERS[version]}")
    if a.get("show_points", True) and total:
        sub.append(f"{fmt_points(total)} point" + ("" if total == 1 else "s"))
    blocks.append({"t": "subtitle", "text": "   |   ".join(x for x in sub if x)})
    if a.get("show_name", True) and not key:
        blocks.append({"t": "fields", "items": ["Name", "Date", "Period"]})
    if a.get("standards") and (a.get("show_standards") or key):
        blocks.append({"t": "p", "text": "Standards: " + ", ".join(a["standards"]), "style": "small"})
    instr = text_blocks(a.get("instructions"))
    if instr:
        first = instr[0]
        if first["t"] == "p":
            instr[0] = {"t": "kv", "label": "Directions:", "text": first["text"]}
        else:
            instr.insert(0, {"t": "kv", "label": "Directions:", "text": ""})
        blocks += instr

    num = 0
    quick = []
    body = []
    for q in qs:
        t = q["type"]
        if t == "section":
            body.append({"t": "h1", "text": q.get("title") or "Section"})
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
        body.append({"t": "q", "num": f"{num}.", "text": prompt, "points": label})

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
    return {"title": title, "footer": footer, "blocks": blocks}
