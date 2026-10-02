"""Lesson Plans: the list and the lesson editor."""

import tkinter as tk

from ..doc import lesson_doc
from ..store import GOOD_THINGS_PREFIX, LESSON_FIELDS, new_lesson, now, points_of, fmt_points, SHEET_KINDS
from . import widgets as W
from .common import AutoText, ItemList, LineField, StandardsField, gap, tool, toolbar, touch
from .desktop import first_line

LABEL = {k: label for k, label, _ in LESSON_FIELDS}
GROUPS = [
    ("Lesson", ["title", "unit", "course", "date", "duration"]),
    ("Standards", ["standards"]),
    ("Goals", ["targets", "success"]),
    ("Getting Started", ["materials", "bell_ringer"]),
    ("Instruction", ["instruction", "guided", "independent"]),
    ("Wrapping Up", ["closure", "checks", "differentiation"]),
    ("Assessments & Worksheets", ["assessments"]),
    ("Homework & Notes", ["homework", "notes"]),
]
assert sorted(k for _, ks in GROUPS for k in ks) == sorted(k for k, _, _ in LESSON_FIELDS)
HINTS = {
    "targets": "One per line. “- ” starts a bullet.",
    "bell_ringer": "No warm-up planned? Click Random SEL Prompt.",
    "materials": "Settings can pre-fill this on every new lesson.",
}


def editor_factory(lesson_id):
    def make(gui, parent):
        lesson = next((l for l in gui.store.data["lessons"] if l["id"] == lesson_id), None)
        if lesson is None:
            return LessonList(gui, parent)
        return LessonEditor(gui, parent, lesson)
    return make


class LessonList(ItemList):
    title = "Lesson Plans"
    kind, pool_key, sort_key, noun = "lesson", "lessons", "lesson_sort", "lesson"
    columns = [("title", "Title", 380, True), ("unit", "Unit", 110, False), ("course", "Course", 130, False),
               ("date", "Date(s)", 120, False), ("updated", "Last Changed", 150, False)]

    def build_tools(self, bar):
        sk = self.gui.skin
        tool(bar, sk, "New Lesson", self.cmd_new)
        self.needs(tool(bar, sk, "Open", self.cmd_open))
        self.needs(tool(bar, sk, "Copy", self.cmd_duplicate))
        self.needs(tool(bar, sk, "Rename…", self.cmd_rename))
        self.needs(tool(bar, sk, "Delete", self.cmd_delete))
        gap(bar, sk)
        self.needs(tool(bar, sk, "Preview", self.cmd_preview))
        self.needs(tool(bar, sk, "Export…", self.cmd_export))
        self.needs(tool(bar, sk, "Board Slide", lambda: self.quick("PNG")))
        self.needs(tool(bar, sk, "Make-Up Sheet", lambda: self.quick("MAKEUP")))

    def row(self, l):
        return [l.get("title") or "(untitled lesson)", l.get("unit") or "", l.get("course") or "",
                first_line(l.get("date")), l.get("updated") or ""]

    def menu_items(self):
        return [("Open", self.cmd_open), ("Preview", self.cmd_preview), ("Export…", self.cmd_export),
                ("Board Slide", lambda: self.quick("PNG")), ("Make-Up Sheet", lambda: self.quick("MAKEUP")),
                (None, None), ("Copy", self.cmd_duplicate), ("Rename…", self.cmd_rename), ("Delete", self.cmd_delete)]

    def cmd_new(self):
        t = W.ask(self.gui, "New Lesson", "What's the lesson called?")
        if t is None:
            return
        l = new_lesson(self.gui.settings)
        l["title"] = t.strip()
        if self.unit.get():
            l["unit"] = self.unit.get()
        self.gui.store.data["lessons"].append(l)
        self.gui.save()
        self.gui.show(editor_factory(l["id"]))

    def cmd_open(self):
        l = self.selected()
        if l:
            self.gui.show(editor_factory(l["id"]))

    def cmd_delete(self):
        l = self.selected()
        if l and W.confirm(self.gui, "Delete Lesson", f"Delete the lesson “{l.get('title') or 'untitled'}”?\n\n"
                           "Its linked quizzes and worksheets are kept.", "Delete", icon="warn"):
            self.pool.remove(l)
            self.gui.save()
            self.refresh()
            self.gui.status("Deleted.")

    def cmd_preview(self):
        l = self.selected()
        if l:
            self.gui.preview(lesson_doc(l, self.gui.store))

    def cmd_export(self):
        l = self.selected()
        if l:
            from .export import export_dialog
            export_dialog(self.gui, "lesson", l)

    def quick(self, fmt):
        l = self.selected()
        if l:
            from .export import run_export
            run_export(self.gui, "lesson", l, fmt)


class LessonEditor:
    def __init__(self, gui, parent, lesson):
        self.gui, self.lesson = gui, lesson
        self.title = "Lesson: " + (lesson.get("title") or "untitled")
        sk, S = gui.skin, gui.skin.S
        f = tk.Frame(parent, bg=sk["window"])
        f.pack(fill="both", expand=True)
        bar = toolbar(f, sk)
        tool(bar, sk, "Preview", self.cmd_preview)
        tool(bar, sk, "Export…", self.cmd_export)
        tool(bar, sk, "Board Slide", lambda: self.quick("PNG"))
        tool(bar, sk, "Make-Up Sheet", lambda: self.quick("MAKEUP"))
        W.label(bar, sk, "Changes save by themselves.", dim=True).pack(side="right")
        tk.Frame(f, bg=sk["dark"], height=S).pack(fill="x")

        self.area = W.ScrollArea(f, sk, gui, maxwidth=1100 * S)
        self.area.pack(fill="both", expand=True)
        form = self.area.inner
        self.fields = {}
        for gtitle, keys in GROUPS:
            g = W.group(form, sk, gtitle)
            g.pack(fill="x", padx=12 * S, pady=(10 * S, 0))
            if gtitle == "Lesson":
                self.build_basics(g)
                continue
            for key in keys:
                self.build_field(g, key)
        tk.Frame(form, bg=sk["window"], height=16 * S).pack()
        gui.status("Last saved " + (lesson.get("updated") or "–"))
        self.title_entry.after_idle(lambda: (self.title_entry.focus_set(), self.title_entry.icursor("end")))

    def build_basics(self, g):
        sk, S, l = self.gui.skin, self.gui.skin.S, self.lesson
        g.columnconfigure(1, weight=1)
        g.columnconfigure(3, weight=1)
        spec = [("title", 0, 0, 3), ("unit", 1, 0, 1), ("course", 1, 2, 1), ("date", 2, 0, 1), ("duration", 2, 2, 1)]
        for key, r, c, span in spec:
            W.label(g, sk, LABEL[key] + ":").grid(row=r, column=c, sticky="w", padx=(0 if c == 0 else 16 * S, 8 * S),
                                                  pady=3 * S)
            fld = LineField(g, self.gui, l, key, width=24, on_change=self.retitle if key == "title" else None)
            fld.entry.grid(row=r, column=c + 1, columnspan=span, sticky="ew", pady=3 * S)
            if key == "title":
                self.title_entry = fld.entry

    def build_field(self, g, key):
        sk, S, l = self.gui.skin, self.gui.skin.S, self.lesson
        head = tk.Frame(g, bg=g["bg"])
        head.pack(fill="x", pady=(6 * S, 2 * S))
        if key not in ("standards", "assessments"):
            W.label(head, sk, LABEL[key], bold=True).pack(side="left")
        if key in HINTS:
            W.label(head, sk, "  " + HINTS[key], dim=True, small=True).pack(side="left")
        if key == "standards":
            StandardsField(g, self.gui, l).pack(fill="x")
        elif key == "assessments":
            Attachments(g, self.gui, l).pack(fill="x")
        else:
            box = AutoText(g, self.gui, l, key)
            box.text.pack(fill="x")
            self.fields[key] = box
            if key == "bell_ringer":
                W.Button(head, sk, "Random SEL Prompt", self.good_thing, small=True).pack(side="right")

    def retitle(self, t):
        self.title = "Lesson: " + (t or "untitled")
        self.gui.set_title(self.title)

    def good_thing(self):
        box = self.fields["bell_ringer"]
        cur = box.text.get("1.0", "end-1c").strip()
        if cur and not cur.startswith(GOOD_THINGS_PREFIX) and not W.confirm(
                self.gui, "Bell Ringer", "Replace this bell ringer with a random SEL prompt?", "Replace"):
            return
        box.replace(self.gui.store.good_thing(cur))
        self.gui.status("SEL bell ringer added. Click again for another.")

    def cmd_preview(self):
        self.gui.save()
        self.gui.preview(lesson_doc(self.lesson, self.gui.store))

    def cmd_export(self):
        from .export import export_dialog
        export_dialog(self.gui, "lesson", self.lesson)

    def quick(self, fmt):
        from .export import run_export
        run_export(self.gui, "lesson", self.lesson, fmt)

    def reloaded(self):
        self.gui.show(editor_factory(self.lesson["id"]), push=False)


class Attachments(tk.Frame):
    """The quizzes and worksheets linked to a lesson. Export All puts them in the lesson's folder."""

    def __init__(self, parent, gui, lesson):
        sk, S = gui.skin, gui.skin.S
        super().__init__(parent, bg=W.bg_of(parent))
        self.gui, self.lesson = gui, lesson
        self.list = W.ListView(self, sk, [("kind", "Type", 140, False), ("title", "Title", 360, True),
                                          ("size", "Size", 140, False)], height=4)
        self.list.pack(fill="x")
        self.list.on_open(self.open)
        self.list.on_delete(self.remove)
        bar = tk.Frame(self, bg=self["bg"])
        bar.pack(fill="x", pady=(6 * S, 0))
        for text, fn in (("New…", self.new), ("Link Existing…", self.link), ("Open", self.open),
                         ("Remove", self.remove), ("Move Up", lambda: self.move(-1)), ("Move Down", lambda: self.move(1))):
            W.Button(bar, sk, text, fn, small=True).pack(side="left", padx=(0, 6 * S))
        W.label(bar, sk, "Export → All formats puts these in the lesson's folder.", dim=True, small=True).pack(
            side="right")
        self.draw()

    def ids(self):
        # drop links to deleted ones
        self.lesson["assessments"] = [a["id"] for a in self.gui.store.attached(self.lesson)]
        return self.lesson["assessments"]

    def draw(self, select=None):
        rows = []
        for a in self.gui.store.attached(self.lesson):
            rows.append((a, [a.get("kind", ""), a.get("title") or "(untitled)", size_text(a)]))
        self.list.set_rows(rows, keep=select, empty_text="Nothing linked yet. Click New… to build a quiz or worksheet.")
        self.list.tv.configure(height=max(3, min(8, len(rows))))

    def changed(self, select=None):
        touch(self.gui, self.lesson)
        self.draw(select)

    def new(self):
        from .assessments import new_assessment_dialog, open_factory
        a = new_assessment_dialog(self.gui, lesson=self.lesson)
        if a:
            self.gui.save()
            self.gui.show(open_factory(a))

    def link(self):
        from ..store import sort_items
        ids = self.ids()
        unit = (self.lesson.get("unit") or "").strip()
        pool = [x for x in sort_items(self.gui.store.data["assessments"], "updated") if x["id"] not in ids]
        pool.sort(key=lambda x: not unit or (x.get("unit") or "").strip() != unit)  # this unit first
        if not pool:
            W.alert(self.gui, "Link Existing", "There's nothing else to link. Click New… to build one.")
            return
        a = W.choose(self.gui, "Link to This Lesson", "Pick a quiz, worksheet, or sheet to go with this lesson:",
                     [(x, f"{x.get('kind', '')}:  {x.get('title') or '(untitled)'}"
                          + (f"   [{x['unit']}]" if (x.get("unit") or "").strip() else "")) for x in pool])
        if a:
            ids.append(a["id"])
            self.changed(a)

    def open(self):
        a = self.list.selected()
        if a:
            from .assessments import open_factory
            self.gui.show(open_factory(a))

    def remove(self):
        a = self.list.selected()
        if a and W.confirm(self.gui, "Remove from Lesson",
                           f"Take “{a.get('title') or 'untitled'}” off this lesson? It isn't deleted.", "Remove"):
            self.ids().remove(a["id"])
            self.changed()

    def move(self, d):
        a = self.list.selected()
        if not a:
            return
        ids = self.ids()
        i = ids.index(a["id"])
        j = i + d
        if 0 <= j < len(ids):
            ids[i], ids[j] = ids[j], ids[i]
            self.changed(a)


def size_text(a):
    kind = a.get("kind", "")
    if kind in SHEET_KINDS:
        return f"{a.get('rows') or 10} rows" if kind == "Annotation Sheet" else "2 weeks"
    qs = a.get("questions") or []
    n = sum(1 for q in qs if q["type"] not in ("passage", "section"))
    return f"{n} question{'s' if n != 1 else ''}, {fmt_points(sum(points_of(q) for q in qs))} pts"
