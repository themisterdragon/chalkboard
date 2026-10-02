"""Assessments & assignments: the list, the quiz/test editor, its question dialog, and the fixed sheets."""

import copy
import tkinter as tk

from ..doc import LETTERS, assessment_doc
from ..store import (ANNOTATION, ASSESSMENT_KINDS, BELL_SHEET, GOOD_THINGS_PREFIX, GRADE_CHOICES, QUESTION_TYPES,
                     SHEET_KINDS, TYPE_TAG, WEEKDAYS, fmt_points, new_assessment, new_question, points_of)
from . import widgets as W
from .common import AutoText, ItemList, LineField, StandardsField, gap, tool, toolbar, touch
from .desktop import first_line, grade_label
from .lessons import size_text

TYPE_TEXT = {"mc": "Multiple choice", "tf": "True / false", "short": "Short answer", "essay": "Extended response",
             "fill": "Fill in the blank", "match": "Matching", "passage": "Reading passage", "section": "Section header"}
TYPE_HELP = {"mc": "A prompt and lettered choices.", "tf": "A statement students mark True or False.",
             "short": "A prompt and a few writing lines.", "essay": "A prompt and lined, blank, or boxed space.",
             "fill": "Type ___ (three underscores) wherever a blank goes.",
             "match": "Terms and their matches. Matches are shuffled on the student copy.",
             "passage": "A text block with line numbers. Not scored.",
             "section": "A titled part with its own directions. Not scored."}
assert set(TYPE_TEXT) == {t for t, _, _ in QUESTION_TYPES}
KIND_HELP = {ANNOTATION: "Name/date, a heading, and a Line / Symbol / Reason chart on one page.",
             BELL_SHEET: "Monday–Friday boxes, one week per side. Print double-sided."}
SPACE_TEXT = {"lines": "Lined", "blank": "Blank space", "box": "Bordered box"}


def open_factory(a):
    aid = a["id"]

    def make(gui, parent):
        x = gui.store.assessment(aid)
        if x is None:
            return AssessmentList(gui, parent)
        return (SheetEditor if x.get("kind") in SHEET_KINDS else AssessmentEditor)(gui, parent, x)
    return make


def new_assessment_dialog(gui, lesson=None, unit=""):
    """Ask what to make and its title. With a lesson, it gets the lesson's unit, course, and standards
    and is linked to it. Returns the new assessment, or None."""
    sk, S = gui.skin, gui.skin.S
    d = W.Dialog(gui, "New Assessment or Worksheet")
    W.label(d.body, sk, "What are you making?", bold=True).pack(anchor="w")
    kind = tk.StringVar(value="Quiz")
    grid = tk.Frame(d.body, bg=sk["window"])
    grid.pack(fill="x", pady=(4 * S, 8 * S))
    for i, k in enumerate(ASSESSMENT_KINDS):
        W.Radio(grid, sk, k, kind, k).grid(row=i % 4, column=i // 4, sticky="w", padx=(0, 24 * S))
    for i, k in enumerate(SHEET_KINDS):
        W.Radio(grid, sk, f"{k} — {KIND_HELP[k]}", kind, k, wrap=520 * S).grid(
            row=4 + i, column=0, columnspan=3, sticky="w")
    W.label(d.body, sk, "Title:").pack(anchor="w")
    title = tk.StringVar()
    e = W.entry(d.body, sk, title, width=46)
    e.pack(fill="x", pady=(2 * S, 0))

    def defaults(*a):
        k = kind.get()
        cur = title.get()
        guesses = {"", "Bell Ringers"} | ({lesson.get("title") + " Annotation"} if lesson and lesson.get("title") else set())
        if cur in guesses:
            title.set({ANNOTATION: (lesson.get("title") + " Annotation") if lesson and lesson.get("title") else "",
                       BELL_SHEET: "Bell Ringers"}.get(k, ""))
    kind.trace_add("write", defaults)
    d.buttons([("Create", True), ("Cancel", None)])
    if not d.run(focus=e):
        return None
    a = new_assessment(gui.settings, kind.get())
    a["title"] = title.get().strip()
    if lesson:
        a.update(unit=lesson.get("unit") or "", course=lesson.get("course") or a["course"],
                 grades=lesson.get("grades") or a["grades"], standards=list(lesson.get("standards") or []))
        lesson.setdefault("assessments", []).append(a["id"])
    elif unit:
        a["unit"] = unit
    gui.store.data["assessments"].append(a)
    gui.save()
    return a


class AssessmentList(ItemList):
    title = "Assessments & Assignments"
    kind, pool_key, sort_key, noun = "assessment", "assessments", "assess_sort", "item"
    search_fields = ("title", "kind", "course", "unit", "standards")
    columns = [("kind", "Type", 140, False), ("title", "Title", 340, True), ("unit", "Unit", 110, False),
               ("size", "Size", 170, False), ("updated", "Last Changed", 150, False)]

    def build_tools(self, bar):
        sk = self.gui.skin
        tool(bar, sk, "New…", self.cmd_new)
        self.needs(tool(bar, sk, "Open", self.cmd_open))
        self.needs(tool(bar, sk, "Copy", self.cmd_duplicate))
        self.needs(tool(bar, sk, "Rename…", self.cmd_rename))
        self.needs(tool(bar, sk, "Delete", self.cmd_delete))
        gap(bar, sk)
        self.needs(tool(bar, sk, "Preview", self.cmd_preview))
        self.key_btn = self.needs(tool(bar, sk, "Answer Key", self.cmd_key))
        self.needs(tool(bar, sk, "Export…", self.cmd_export))

    def build_filters(self, filt):
        sk, S = self.gui.skin, self.gui.skin.S
        W.label(filt, sk, "Type:").pack(side="left")
        self.type = tk.StringVar(value="")
        W.Dropdown(filt, sk, [("", "All types")] + [(k, k) for k in ASSESSMENT_KINDS + SHEET_KINDS], self.type,
                   lambda v: self.refresh(), width=14).pack(side="left", padx=(6 * S, 14 * S))

    def match(self, a):
        return not self.type.get() or a.get("kind") == self.type.get()

    def row(self, a):
        return [a.get("kind", ""), a.get("title") or "(untitled)", a.get("unit") or "", size_text(a),
                a.get("updated") or ""]

    def update_tools(self):
        super().update_tools()
        a = self.selected()
        if a is not None:
            self.key_btn.set_enabled(a.get("kind") not in SHEET_KINDS)

    def menu_items(self):
        return [("Open", self.cmd_open), ("Preview", self.cmd_preview), ("Answer Key", self.cmd_key),
                ("Export…", self.cmd_export), (None, None), ("Copy", self.cmd_duplicate),
                ("Rename…", self.cmd_rename), ("Delete", self.cmd_delete)]

    def cmd_new(self):
        a = new_assessment_dialog(self.gui, unit=self.unit.get())
        if a:
            self.gui.show(open_factory(a))

    def cmd_open(self):
        a = self.selected()
        if a:
            self.gui.show(open_factory(a))

    def cmd_delete(self):
        a = self.selected()
        if not a:
            return
        used = [l for l in self.gui.store.data["lessons"] if a["id"] in (l.get("assessments") or [])]
        also = f"\n\nIt's linked to {len(used)} lesson{'s' if len(used) != 1 else ''}; the link{'s' if len(used) != 1 else ''} will be removed." if used else ""
        if W.confirm(self.gui, "Delete", f"Delete “{a.get('title') or 'untitled'}”?{also}", "Delete", icon="warn"):
            self.pool.remove(a)
            for l in used:
                l["assessments"].remove(a["id"])
            self.gui.save()
            self.refresh()
            self.gui.status("Deleted.")

    def cmd_preview(self):
        a = self.selected()
        if a:
            self.gui.preview(assessment_doc(a, self.gui.store))

    def cmd_key(self):
        a = self.selected()
        if a and a.get("kind") not in SHEET_KINDS:
            self.gui.preview(assessment_doc(a, self.gui.store, key=True), (a.get("title") or "Untitled") + " (Key)")

    def cmd_export(self):
        a = self.selected()
        if a:
            from .export import export_dialog
            export_dialog(self.gui, "assessment", a)


# ================================================================ quiz / test editor
class AssessmentEditor:
    def __init__(self, gui, parent, a):
        self.gui, self.a = gui, a
        self.title = f"{a.get('kind') or 'Assessment'}: {a.get('title') or 'untitled'}"
        sk, S = gui.skin, gui.skin.S
        f = tk.Frame(parent, bg=sk["window"])
        f.pack(fill="both", expand=True)
        bar = toolbar(f, sk)
        tool(bar, sk, "Preview", self.cmd_preview)
        tool(bar, sk, "Answer Key", self.cmd_key)
        tool(bar, sk, "Export…", self.cmd_export)
        W.label(bar, sk, "Changes save by themselves.", dim=True).pack(side="right")
        tk.Frame(f, bg=sk["dark"], height=S).pack(fill="x")

        panes = tk.PanedWindow(f, orient="horizontal", bg=sk["window"], sashwidth=6 * S, bd=0, sashrelief="flat")
        panes.pack(fill="both", expand=True)
        left = W.ScrollArea(panes, sk, gui)
        right = tk.Frame(panes, bg=sk["window"])
        panes.add(left, minsize=320 * S, width=440 * S)
        panes.add(right, minsize=400 * S)
        self.build_settings(left.inner)
        self.build_questions(right)
        self.refresh()

    # -- left: title, type, standards, directions, options
    def build_settings(self, p):
        sk, S, a = self.gui.skin, self.gui.skin.S, self.a
        g = W.group(p, sk, "About This " + (a.get("kind") or "Assessment"))
        g.pack(fill="x", padx=10 * S, pady=(10 * S, 0))
        g.columnconfigure(1, weight=1)
        for r, (key, label) in enumerate((("title", "Title"), ("unit", "Unit"), ("course", "Course"))):
            W.label(g, sk, label + ":").grid(row=r, column=0, sticky="w", pady=3 * S, padx=(0, 8 * S))
            fld = LineField(g, self.gui, a, key, width=22, on_change=self.retitle if key == "title" else None)
            fld.entry.grid(row=r, column=1, sticky="ew", pady=3 * S)
        W.label(g, sk, "Type:").grid(row=3, column=0, sticky="w", pady=3 * S)
        kind = tk.StringVar(value=a.get("kind") if a.get("kind") in ASSESSMENT_KINDS else "Quiz")
        W.Dropdown(g, sk, [(k, k) for k in ASSESSMENT_KINDS], kind, lambda v: self.set(kind=v), width=14).grid(
            row=3, column=1, sticky="w", pady=3 * S)
        W.label(g, sk, "Grades:").grid(row=4, column=0, sticky="w", pady=3 * S)
        gr = tk.StringVar(value=a.get("grades") or "")
        opts = [(x, grade_label(x)) for x in GRADE_CHOICES]
        if gr.get() not in GRADE_CHOICES:
            opts.insert(0, (gr.get(), grade_label(gr.get())))
        W.Dropdown(g, sk, opts, gr, lambda v: self.set(grades=v), width=14).grid(row=4, column=1, sticky="w", pady=3 * S)

        g = W.group(p, sk, "Standards")
        g.pack(fill="x", padx=10 * S, pady=(10 * S, 0))
        StandardsField(g, self.gui, a).pack(fill="x")

        g = W.group(p, sk, "Directions (Printed at the Top)")
        g.pack(fill="x", padx=10 * S, pady=(10 * S, 0))
        AutoText(g, self.gui, a, "instructions", min_lines=3, width=30).text.pack(fill="x")

        g = W.group(p, sk, "On the Student Copy")
        g.pack(fill="x", padx=10 * S, pady=(10 * S, 12 * S))
        for key, text in (("show_name", "Name / date / period line"), ("show_points", "Point values"),
                          ("show_standards", "The standards")):
            v = tk.BooleanVar(value=bool(a.get(key)))
            v.trace_add("write", lambda *x, k=key, v=v: self.set(**{k: v.get()}))
            W.Check(g, sk, text, v).pack(anchor="w")

    def set(self, **kw):
        self.a.update(kw)
        touch(self.gui, self.a)
        if "kind" in kw:
            self.retitle(self.a.get("title"))

    def retitle(self, t):
        self.title = f"{self.a.get('kind') or 'Assessment'}: {t or 'untitled'}"
        self.gui.set_title(self.title)

    # -- right: the questions
    def build_questions(self, p):
        sk, S = self.gui.skin, self.gui.skin.S
        g = W.group(p, sk, "Questions")
        g.pack(fill="both", expand=True, padx=(0, 10 * S), pady=10 * S)
        bar = tk.Frame(g, bg=g["bg"])
        bar.pack(fill="x", pady=(0, 6 * S))
        self.add_btn = W.Button(bar, sk, "Add Question ▾", self.add_menu, default=False)
        self.add_btn.pack(side="left", padx=(0, 6 * S))
        self.sel_btns = []
        for text, fn in (("Edit…", self.edit), ("Copy", self.duplicate), ("Delete", self.delete),
                         ("Move Up", lambda: self.move(-1)), ("Move Down", lambda: self.move(1))):
            b = W.Button(bar, sk, text, fn)
            b.pack(side="left", padx=(0, 6 * S))
            self.sel_btns.append(b)
        self.list = W.ListView(g, sk, [("n", "#", 44, False), ("type", "Type", 70, False), ("pts", "Pts", 50, False),
                                       ("q", "Question", 380, True), ("std", "Standard", 120, False)], height=12)
        self.list.pack(fill="both", expand=True)
        self.list.on_open(self.edit)
        self.list.on_delete(self.delete)
        self.list.on_select(self.update_buttons)
        self.list.on_menu(self.popup)
        self.total = W.label(g, sk, "", bold=True)
        self.total.pack(anchor="e", pady=(6 * S, 0))

    def refresh(self, select=None):
        qs = self.a["questions"]
        rows, n = [], 0
        for q in qs:
            t = q["type"]
            if t == "section":
                rows.append((q, ["", "SECT", "", "── " + (q.get("title") or "Section") + " ──", ""]))
                continue
            if t == "passage":
                words = len((q.get("text") or "").split())
                rows.append((q, ["", "TEXT", "", f"{q.get('title') or 'Passage'}  ({words} words)", ""]))
                continue
            n += 1
            warn = ""
            if (t == "mc" and (q.get("answer") is None or not q.get("choices"))) or (t == "tf" and q.get("answer") is None):
                warn = "   (no answer key yet)"
            rows.append((q, [f"{n}.", TYPE_TAG[t], fmt_points(points_of(q)),
                             (first_line(q.get("prompt")) or "(no prompt)") + warn, q.get("standard") or ""]))
        self.list.set_rows(rows, keep=select,
                           empty_text="No questions yet. Click Add Question to write the first one.")
        pts = sum(points_of(q) for q in qs)
        self.total.configure(text=f"{n} question{'s' if n != 1 else ''}, {fmt_points(pts)} point{'s' if pts != 1 else ''}")
        self.gui.status(self.total.cget("text"),
                        "Standards: " + (", ".join(self.a.get("standards") or []) or "none"))
        self.update_buttons()

    def update_buttons(self):
        on = self.list.selected() is not None
        for b in self.sel_btns:
            b.set_enabled(on)

    def changed(self, select=None):
        touch(self.gui, self.a)
        self.refresh(select)

    def add_menu(self):
        m = tk.Menu(self.add_btn, tearoff=False)
        W.skin_menu(m, self.gui.skin)
        for t, _, _ in QUESTION_TYPES:
            m.add_command(label=f"{TYPE_TEXT[t]}", command=lambda t=t: self.add(t))
        try:
            m.tk_popup(self.add_btn.winfo_rootx(), self.add_btn.winfo_rooty() + self.add_btn.winfo_height())
        finally:
            m.grab_release()

    def default_std(self):
        s = self.a.get("standards") or []
        return s[0] if len(s) == 1 else ""

    def add(self, t):
        q = new_question(t, self.default_std())
        if t == "match":
            q["prompt"] = "Match each term with its definition."
        q = question_dialog(self.gui, self.a, q, new=True)
        if q is None:
            return
        qs = self.a["questions"]
        i = self.list.index()
        at = len(qs) if i is None else i + 1
        qs.insert(at, q)
        self.changed(q)

    def edit(self):
        q = self.list.selected()
        if q is None:
            return
        new = question_dialog(self.gui, self.a, q)
        if new is not None:
            q.clear()
            q.update(new)
            self.changed(q)

    def duplicate(self):
        q = self.list.selected()
        if q is None:
            return
        qs = self.a["questions"]
        dup = copy.deepcopy(q)
        qs.insert(qs.index(q) + 1, dup)
        self.changed(dup)

    def delete(self):
        q = self.list.selected()
        if q is None:
            return
        if W.confirm(self.gui, "Delete Question", f"Delete this {TYPE_TEXT[q['type']].lower()} question?\n\n"
                     f"“{first_line(q.get('prompt') or q.get('title')) or '(blank)'}”", "Delete", icon="warn"):
            qs = self.a["questions"]
            i = qs.index(q)
            qs.remove(q)
            self.changed(qs[min(i, len(qs) - 1)] if qs else None)

    def move(self, d):
        q = self.list.selected()
        if q is None:
            return
        qs = self.a["questions"]
        i = qs.index(q)
        j = i + d
        if 0 <= j < len(qs):
            qs[i], qs[j] = qs[j], qs[i]
            self.changed(q)

    def popup(self, e):
        m = tk.Menu(self.list, tearoff=False)
        W.skin_menu(m, self.gui.skin)
        for label, fn in (("Edit…", self.edit), ("Copy", self.duplicate), ("Delete", self.delete),
                          ("Move Up", lambda: self.move(-1)), ("Move Down", lambda: self.move(1))):
            m.add_command(label=label, command=fn)
        try:
            m.tk_popup(e.x_root, e.y_root)
        finally:
            m.grab_release()

    def cmd_preview(self):
        self.gui.save()
        self.gui.preview(assessment_doc(self.a, self.gui.store))

    def cmd_key(self):
        self.gui.save()
        self.gui.preview(assessment_doc(self.a, self.gui.store, key=True), (self.a.get("title") or "Untitled") + " (Key)")

    def cmd_export(self):
        from .export import export_dialog
        export_dialog(self.gui, "assessment", self.a)

    def cmd_open(self):
        self.edit()

    def reloaded(self):
        self.gui.show(open_factory(self.a), push=False)


def parse_points(s, old):
    try:
        v = float(s)
        return int(v) if v.is_integer() else v
    except (TypeError, ValueError):
        return old


def question_dialog(gui, a, q, new=False):
    """Edit a copy of q. Returns the edited question, or None if cancelled."""
    q = copy.deepcopy(q)
    t = q["type"]
    sk, S = gui.skin, gui.skin.S
    d = W.Dialog(gui, ("New " if new else "Edit ") + TYPE_TEXT[t] + (" Question" if t not in ("passage", "section") else ""))
    body = d.body
    W.label(body, sk, TYPE_HELP[t], dim=True).pack(anchor="w", pady=(0, 6 * S))
    texts = {}

    def line(label, key, width=50):
        W.label(body, sk, label, bold=True).pack(anchor="w", pady=(6 * S, 2 * S))
        v = tk.StringVar(value=str(q.get(key) or ""))
        e = W.entry(body, sk, v, width=width)
        e.pack(fill="x")
        texts[key] = lambda v=v: v.get()
        return e

    def text(label, key, h):
        W.label(body, sk, label, bold=True).pack(anchor="w", pady=(6 * S, 2 * S))
        box = W.textbox(body, sk, height=h, width=64)
        box.insert("1.0", q.get(key) or "")
        box.pack(fill="both", expand=True)
        box.bind("<Tab>", lambda e: (box.tk_focusNext().focus_set(), "break")[1])
        texts[key] = lambda b=box: b.get("1.0", "end-1c").strip()
        return box

    first = None
    if t == "section":
        first = line("Section title (like Part II: Vocabulary)", "title")
        text("Directions for this section (optional)", "prompt", 3)
    elif t == "passage":
        first = line("Passage title (optional)", "title")
        text("Passage text (paste or type; each new line starts a paragraph)", "text", 12)
        num = tk.BooleanVar(value=q.get("numbered", True))
        W.Check(body, sk, "Number the lines on the PDF", num).pack(anchor="w", pady=(6 * S, 0))
        texts["numbered"] = num.get
    else:
        first = text("Question" if t != "match" else "Directions for the matching set", "prompt", 4 if t != "fill" else 3)

    choice_rows = []
    if t == "mc":
        W.label(body, sk, "Choices (click the circle next to the correct answer)", bold=True).pack(
            anchor="w", pady=(8 * S, 2 * S))
        holder = tk.Frame(body, bg=sk["window"])
        holder.pack(fill="x")
        ans = tk.StringVar(value=str(q.get("answer")) if q.get("answer") is not None else "")

        def draw_choices():
            for w in holder.winfo_children():
                w.destroy()
            for i, (var, _) in enumerate(choice_rows):
                W.Radio(holder, sk, LETTERS[i] + ")", ans, str(i)).grid(row=i, column=0, sticky="w")
                W.entry(holder, sk, var, width=48).grid(row=i, column=1, sticky="ew", pady=2 * S, padx=(4 * S, 6 * S))
                W.Button(holder, sk, "Remove", lambda i=i: drop(i), small=True).grid(row=i, column=2, sticky="w")
            holder.columnconfigure(1, weight=1)
            add_btn.set_enabled(len(choice_rows) < 6)

        def drop(i):
            choice_rows.pop(i)
            a_ = ans.get()
            if a_.isdigit():
                k = int(a_)
                ans.set("" if k == i else str(k - 1) if k > i else a_)
            draw_choices()

        def add_choice():
            if len(choice_rows) < 6:
                choice_rows.append((tk.StringVar(), None))
                draw_choices()
                holder.winfo_children()[-2].focus_set()
        add_btn = W.Button(body, sk, "Add Choice", add_choice, small=True)
        add_btn.pack(anchor="w", pady=(4 * S, 0))
        for c in q.get("choices") or []:
            choice_rows.append((tk.StringVar(value=c), None))
        while len(choice_rows) < 4 and not q.get("choices"):
            choice_rows.append((tk.StringVar(), None))
        draw_choices()
    elif t == "tf":
        W.label(body, sk, "Answer", bold=True).pack(anchor="w", pady=(8 * S, 2 * S))
        tf = tk.StringVar(value={True: "T", False: "F"}.get(q.get("answer"), ""))
        row = tk.Frame(body, bg=sk["window"])
        row.pack(anchor="w")
        for v, label_ in (("T", "True"), ("F", "False"), ("", "Not set")):
            W.Radio(row, sk, label_, tf, v).pack(side="left", padx=(0, 16 * S))
        texts["answer"] = lambda: {"T": True, "F": False}.get(tf.get())
    elif t == "short":
        row = tk.Frame(body, bg=sk["window"])
        row.pack(anchor="w", pady=(8 * S, 0))
        W.label(row, sk, "Writing lines:").pack(side="left")
        lines = tk.StringVar(value=str(q.get("lines") or 3))
        W.Dropdown(row, sk, [(str(n), str(n)) for n in range(1, 13)], lines, width=4).pack(side="left", padx=6 * S)
        texts["lines"] = lambda: int(lines.get())
        text("Answer key / exemplar (only on the key)", "answer", 2)
    elif t == "essay":
        row = tk.Frame(body, bg=sk["window"])
        row.pack(anchor="w", pady=(8 * S, 0))
        W.label(row, sk, "Writing space:").pack(side="left", padx=(0, 6 * S))
        space = tk.StringVar(value=q.get("space") or "lines")
        for v, label_ in SPACE_TEXT.items():
            W.Radio(row, sk, label_, space, v).pack(side="left", padx=(0, 12 * S))
        texts["space"] = space.get
        row = tk.Frame(body, bg=sk["window"])
        row.pack(anchor="w", pady=(4 * S, 0))
        W.label(row, sk, "Size (lines, about 1/3 inch each):").pack(side="left")
        lines = tk.StringVar(value=str(q.get("lines") or 12))
        W.Dropdown(row, sk, [(str(n), str(n)) for n in (4, 6, 8, 10, 12, 15, 18, 20, 24, 30)]
                   + ([(lines.get(), lines.get())] if lines.get() not in {"4", "6", "8", "10", "12", "15", "18", "20", "24", "30"} else []),
                   lines, width=4).pack(side="left", padx=6 * S)
        texts["lines"] = lambda: int(lines.get())
        text("Rubric / key notes (only on the key)", "answer", 3)
    elif t == "fill":
        line("Answer(s) for the key", "answer")
    pair_rows = []
    if t == "match":
        W.label(body, sk, "Terms and matches (up to 10)", bold=True).pack(anchor="w", pady=(8 * S, 2 * S))
        holder = tk.Frame(body, bg=sk["window"])
        holder.pack(fill="x")

        def draw_pairs():
            for w in holder.winfo_children():
                w.destroy()
            W.label(holder, sk, "Term", dim=True).grid(row=0, column=0, sticky="w")
            W.label(holder, sk, "Matches", dim=True).grid(row=0, column=1, sticky="w", padx=(6 * S, 0))
            for i, (tv, mv) in enumerate(pair_rows, 1):
                W.entry(holder, sk, tv, width=22).grid(row=i, column=0, sticky="ew", pady=2 * S)
                W.entry(holder, sk, mv, width=36).grid(row=i, column=1, sticky="ew", pady=2 * S, padx=6 * S)
                W.Button(holder, sk, "Remove", lambda i=i - 1: (pair_rows.pop(i), draw_pairs()), small=True).grid(
                    row=i, column=2)
            holder.columnconfigure(1, weight=1)
            padd.set_enabled(len(pair_rows) < 10)

        def add_pair():
            if len(pair_rows) < 10:
                pair_rows.append((tk.StringVar(), tk.StringVar()))
                draw_pairs()
                holder.grid_slaves(row=len(pair_rows), column=0)[0].focus_set()
        padd = W.Button(body, sk, "Add Pair", add_pair, small=True)
        padd.pack(anchor="w", pady=(4 * S, 0))
        for term, m in q.get("pairs") or []:
            pair_rows.append((tk.StringVar(value=term), tk.StringVar(value=m)))
        if not pair_rows:
            pair_rows.extend((tk.StringVar(), tk.StringVar()) for _ in range(3))
        draw_pairs()

    if t not in ("passage", "section"):
        row = tk.Frame(body, bg=sk["window"])
        row.pack(fill="x", pady=(10 * S, 0))
        W.label(row, sk, "Points:").pack(side="left")
        pts = tk.StringVar(value=fmt_points(q.get("points") or 0))
        W.entry(row, sk, pts, width=5).pack(side="left", padx=(6 * S, 4 * S))
        if t == "match":
            W.label(row, sk, "(0 = one per pair)", dim=True).pack(side="left")
        W.label(row, sk, "     Standard:").pack(side="left")
        pool = list(a.get("standards") or [])
        if q.get("standard") and q["standard"] not in pool:
            pool.append(q["standard"])
        std = tk.StringVar(value=q.get("standard") or "")
        opts = [("", "(none)")] + [(c, f"{c}  {gui.store.std_text(c)[:60]}") for c in pool]
        W.Dropdown(row, sk, opts, std, width=26).pack(side="left", padx=(6 * S, 0))
        if not a.get("standards"):
            W.label(body, sk, "Add standards to this assessment (on the left) to tag questions with them.",
                    dim=True, small=True).pack(anchor="w", pady=(2 * S, 0))
        texts["points"] = lambda: parse_points(pts.get(), q.get("points"))
        texts["standard"] = std.get

    out = {}

    def harvest():
        for key, get in texts.items():
            out[key] = get()
    d.harvest = harvest
    d.buttons([("OK", True), ("Cancel", None)])
    if not d.run(focus=first):
        return None
    q.update(out)
    if t == "mc":
        q["choices"] = [v.get().strip() for v, _ in choice_rows if v.get().strip()]
        kept = [i for i, (v, _) in enumerate(choice_rows) if v.get().strip()]
        a_ = ans.get()
        q["answer"] = kept.index(int(a_)) if a_.isdigit() and int(a_) in kept else None
    if t == "match":
        q["pairs"] = [[tv.get().strip(), mv.get().strip()] for tv, mv in pair_rows if tv.get().strip()]
    return q


# ================================================================ annotation & bell ringer sheets
class SheetEditor:
    def __init__(self, gui, parent, a):
        self.gui, self.a = gui, a
        bell = a.get("kind") == BELL_SHEET
        self.title = f"{a.get('kind')}: {a.get('title') or 'untitled'}"
        sk, S = gui.skin, gui.skin.S
        if bell:
            a.setdefault("weeks", ["", ""])
            a.setdefault("days", [""] * 10)
        f = tk.Frame(parent, bg=sk["window"])
        f.pack(fill="both", expand=True)
        bar = toolbar(f, sk)
        tool(bar, sk, "Preview", self.cmd_preview)
        tool(bar, sk, "Export…", self.cmd_export)
        W.label(bar, sk, KIND_HELP[a.get("kind")], dim=True).pack(side="right")
        tk.Frame(f, bg=sk["dark"], height=S).pack(fill="x")
        area = W.ScrollArea(f, sk, gui, maxwidth=1000 * S)
        area.pack(fill="both", expand=True)
        p = area.inner

        g = W.group(p, sk, "Sheet")
        g.pack(fill="x", padx=12 * S, pady=(10 * S, 0))
        g.columnconfigure(1, weight=1)
        fields = (("title", "Heading" if not bell else "Title"), ("unit", "Unit"), ("course", "Course"))
        for r, (key, label) in enumerate(fields):
            W.label(g, sk, label + ":").grid(row=r, column=0, sticky="w", pady=3 * S, padx=(0, 8 * S))
            LineField(g, gui, a, key, width=40, on_change=self.retitle if key == "title" else None).entry.grid(
                row=r, column=1, sticky="ew", pady=3 * S)
        if not bell:
            W.label(g, sk, "Rows in the chart:").grid(row=3, column=0, sticky="w", pady=3 * S)
            rows = tk.StringVar(value=str(a.get("rows") or 10))
            W.Dropdown(g, sk, [(str(n), str(n) + (" (fills one page)" if n == 10 else "")) for n in range(1, 21)],
                       rows, lambda v: self.set(rows=int(v)), width=16).grid(row=3, column=1, sticky="w", pady=3 * S)
        g = W.group(p, sk, "Directions (Optional)")
        g.pack(fill="x", padx=12 * S, pady=(10 * S, 0))
        AutoText(g, gui, a, "instructions", min_lines=2).text.pack(fill="x")

        if bell:
            self.days = {}
            for w in range(2):
                g = W.group(p, sk, f"Week {w + 1} ({'front' if w == 0 else 'back'} of the page)")
                g.pack(fill="x", padx=12 * S, pady=(10 * S, 0))
                row = tk.Frame(g, bg=g["bg"])
                row.pack(fill="x")
                W.label(row, sk, "Week of:").pack(side="left")
                wv = tk.StringVar(value=a["weeks"][w])
                wv.trace_add("write", lambda *x, w=w, wv=wv: self.set_week(w, wv.get()))
                W.entry(row, sk, wv, width=18).pack(side="left", padx=6 * S)
                W.label(row, sk, "(like Oct 5–9; blank prints a line to fill in)", dim=True, small=True).pack(side="left")
                for dd in range(5):
                    i = w * 5 + dd
                    head = tk.Frame(g, bg=g["bg"])
                    head.pack(fill="x", pady=(6 * S, 2 * S))
                    W.label(head, sk, WEEKDAYS[dd], bold=True).pack(side="left")
                    W.label(head, sk, "  blank: students write the prompt", dim=True, small=True).pack(side="left")
                    W.Button(head, sk, "Random SEL Prompt", lambda i=i: self.good_thing(i), small=True).pack(side="right")
                    box = AutoText(g, gui, a, f"day{i}", min_lines=1, max_lines=6,
                                   get=lambda i=i: a["days"][i], set=lambda v, i=i: a["days"].__setitem__(i, v))
                    box.text.pack(fill="x")
                    self.days[i] = box
        tk.Frame(p, bg=sk["window"], height=16 * S).pack()
        gui.status("Last saved " + (a.get("updated") or "–"))

    def set(self, **kw):
        self.a.update(kw)
        touch(self.gui, self.a)

    def set_week(self, w, v):
        self.a["weeks"][w] = v
        touch(self.gui, self.a)

    def retitle(self, t):
        self.title = f"{self.a.get('kind')}: {t or 'untitled'}"
        self.gui.set_title(self.title)

    def good_thing(self, i):
        cur = self.a["days"][i].strip()
        if cur and not cur.startswith(GOOD_THINGS_PREFIX) and not W.confirm(
                self.gui, "Bell Ringer", f"Replace {WEEKDAYS[i % 5]}'s prompt with a random SEL prompt?", "Replace"):
            return
        self.days[i].replace(self.gui.store.good_thing(cur))

    def cmd_preview(self):
        self.gui.save()
        self.gui.preview(assessment_doc(self.a, self.gui.store))

    def cmd_export(self):
        from .export import export_dialog
        export_dialog(self.gui, "assessment", self.a)

    def reloaded(self):
        self.gui.show(open_factory(self.a), push=False)
