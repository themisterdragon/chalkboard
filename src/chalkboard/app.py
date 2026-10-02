"""Chalkboard -- a retro, offline lesson planner and assessment builder."""

import argparse
import copy
import os
import re
import textwrap

from . import __version__
from .ui import APP, BACK, UI, big, ch, curses, truncate
from .store import (ALL, ANNOTATION, ASSESSMENT_KINDS, BELL_SHEET, BOARD_SECTIONS, BOARD_STYLES, DEFAULT_SUBJECT,
                    GOOD_THINGS_PREFIX, GRADE_CHOICES, LESSON_FIELDS, QUESTION_TYPES, SHEET_KINDS, SORTS, TYPE_LABEL,
                    TYPE_TAG, WEEKDAYS, Store, fmt_points, grades_match, new_assessment, new_lesson, new_question, now,
                    parse_hex, points_of, sort_items)
from .doc import LETTERS, assessment_doc, lesson_doc
from .exporting import FORMAT_ORDER, ExportError, export, export_subdirs, open_path
from .export_png import low_contrast
from .export_txt import render_lines
FORMAT_LABEL = {"PDF": "PDF", "DOCX": "WORD (.DOCX - ALSO GOOGLE DOCS)", "TXT": "PLAIN TEXT",
                "PNG": "BOARD SLIDE (1920x1080 PNG FOR CLASSROOM DISPLAY)",
                "MAKEUP": "MAKE-UP SHEET FOR ABSENT STUDENTS (PDF + DOCX)", "ALL": "ALL FORMATS"}
INCLUDE_LABEL = {"STUDENT": "STUDENT COPY ONLY", "KEY": "ANSWER KEY ONLY", "BOTH": "STUDENT COPY + ANSWER KEY"}
SPACE_LABEL = {"lines": "LINED", "blank": "BLANK SPACE", "box": "BORDERED BOX"}


def grade_label(g):
    if g == ALL:
        return "ALL GRADES"
    if not re.fullmatch(r"[K\d]+(-\d+)?", g or ""):
        return (g or "").upper()
    if g == "K":
        return "KINDERGARTEN"
    return f"GRADES {g}" if "-" in g else f"GRADE {g}"


def sub_label(parent, label):
    """'a. ' for lettered parts, '1. ' for indicators, 'GRADE K: ' for grade-by-grade indicators."""
    return f"GRADE {label}: " if parent.get("sub_grades") else f"{label}. "


def first_line(s):
    for line in (s or "").split("\n"):
        if line.strip():
            return line.strip()
    return ""


class App:
    def __init__(self, stdscr, args):
        self.store = Store()
        self.st = self.store.settings
        self.ui = UI(stdscr, self.st)
        self.args = args
        if self.store.warning:
            self.ui.msg = self.store.warning

    def save(self):
        try:
            self.store.save()
        except OSError as e:
            self.ui.msg = f"?COULD NOT SAVE: {e}"

    # ------------------------------------------------------------- boot/menu
    def boot(self):
        ui, s = self.ui, self.ui.s
        rows, w, x0 = ui.dims()
        s.erase()
        s.nodelay(True)
        skip = False

        def pause(ms):
            nonlocal skip
            if skip:
                return
            s.refresh()
            curses.napms(ms)
            if s.getch() != -1:
                skip = True

        banner = big(APP, "██" if w >= 106 else "█")
        for i, line in enumerate(banner):
            ui.draw(2 + i, x0 + (w - len(banner[0])) // 2, line, ui.HI)
            pause(50)
        left = x0 + max(2, (w - 52) // 2)
        lines = [
            (f"{APP} LESSON PLANNING SYSTEM  V{__version__}", ui.N),
            ("", ui.N),
            (f"{f'STANDARDS, {len(self.store.subjects)} SUBJECTS ':.<31} {self.store.kas_count}", ui.N),
            (f"LESSON PLANS .................. {len(self.store.data['lessons'])}", ui.N),
            (f"ASSESSMENTS ................... {len(self.store.data['assessments'])}", ui.N),
            ("NETWORK ....................... NOT REQUIRED", ui.N),
        ]
        for i, (text, attr) in enumerate(lines):
            ui.draw(9 + i, left, ui.tx(text), attr)
            pause(120)
        for j, c in enumerate(ui.tx("]RUN CHALKBOARD")):
            ui.draw(9 + len(lines) + 1, left + j, c, ui.HI)
            pause(55)
        pause(400)
        s.nodelay(False)

    def run(self):
        if self.st.get("boot", True) and not self.args.no_boot:
            self.boot()
        self.main_menu()

    def main_menu(self):
        ui = self.ui
        items = [
            ("1", "LESSON PLANS", self.lessons_screen),
            ("2", "ASSESSMENTS & ASSIGNMENTS", self.assessments_screen),
            ("3", "STANDARDS LIBRARY", self.standards_library),
            ("4", "SETTINGS", self.settings_screen),
            ("Q", "QUIT", None),
        ]
        sel, first = 0, True
        while True:
            rows, w, x0 = ui.dims()
            ui.begin()
            ui.header("MAIN MENU")
            y = 2
            if rows >= 24:
                banner = big(APP, "██" if w >= 106 else "█")
                for i, line in enumerate(banner):
                    ui.put(y + i, x0 + (w - len(banner[0])) // 2, line, ui.HI)
                y += 6
            ui.center(y, "* LESSON PLANS, ASSESSMENTS & ASSIGNMENTS *", ui.DIM)
            y += 2
            mx = x0 + max(2, (w - 40) // 2)
            for i, (k, label, _) in enumerate(items):
                ui.badge(y, mx, k)
                ui.put(y, mx + 5, label, ui.INV if i == sel else ui.N)
                y += 2
            d = self.store.data
            if y < rows - 3:
                who = f"   //   {self.st['teacher']}" if self.st.get("teacher") else ""
                ui.center(y + 1, f"{len(d['lessons'])} LESSONS   //   {len(d['assessments'])} ASSESSMENTS{who.upper()}", ui.DIM)
            ui.footer("PRESS A NUMBER, OR USE ARROWS + RETURN")
            ui.show(animate=first)
            first = False
            k = ui.key()
            c = ch(k).upper()
            choice = None
            if k in ("\n", "\r", curses.KEY_ENTER):
                choice = items[sel]
            elif k == curses.KEY_UP:
                sel = (sel - 1) % len(items)
            elif k == curses.KEY_DOWN:
                sel = (sel + 1) % len(items)
            else:
                choice = next((it for it in items if it[0] == c), None)
            if choice:
                ui.echo(choice[0])
                if choice[2] is None:
                    return
                sel = items.index(choice)
                choice[2]()
                first = True

    # --------------------------------------------------------------- shared
    def search_filter(self, items, query, fields):
        if not query:
            return items
        q = query.lower()
        return [it for it in items if any(q in str(it.get(f, "")).lower() for f in fields)]

    def filtered(self, pool, state, fields):
        """Sort, then narrow a list by the unit/type filters and search in state."""
        xs = sort_items(pool, self.st.get(state["sort"], "updated"))
        if state["unit"]:
            xs = [x for x in xs if (x.get("unit") or "").strip() == state["unit"]]
        if state.get("kind"):
            xs = [x for x in xs if x.get("kind") == state["kind"]]
        return self.search_filter(xs, state["q"], fields)

    def filter_info(self, state, count, noun):
        bits = [f"{count} {noun}"]
        if state["unit"]:
            bits.append(f"UNIT: {state['unit']}")
        if state.get("kind"):
            bits.append(f"TYPE: {state['kind']}")
        if state["q"]:
            bits.append(f"SEARCH: '{state['q']}'")
        bits.append("SORT: " + SORTS[self.st.get(state["sort"], "updated")])
        return "   ".join(bits)

    def filter_key(self, c, state, pool):
        """/ search, U unit, T type (if state has one), O sort order. True if c was one of them."""
        ui = self.ui
        if c == "/":
            q = ui.prompt("SEARCH (BLANK CLEARS)", state["q"])
            if q is not None:
                state["q"] = q
        elif c == "u":
            units = self.store.units(pool)
            if not units:
                ui.msg = "?NOTHING HAS A UNIT YET. FILL IN THE UNIT FIELD TO GROUP THINGS."
                return True
            j = ui.choose("SHOW WHICH UNIT?", ["(ALL UNITS)"] + units,
                          units.index(state["unit"]) + 1 if state["unit"] in units else 0)
            if j is not None:
                state["unit"] = units[j - 1] if j else ""
        elif c == "t" and "kind" in state:
            kinds = [k for k in ASSESSMENT_KINDS + SHEET_KINDS if any(x.get("kind") == k for x in pool)]
            j = ui.choose("SHOW WHICH TYPE?", [ui.tx("(all types)")] + [ui.tx(k) for k in kinds],
                          kinds.index(state["kind"]) + 1 if state["kind"] in kinds else 0)
            if j is not None:
                state["kind"] = kinds[j - 1] if j else ""
        elif c == "o":
            order = list(SORTS)
            cur = self.st.get(state["sort"], "updated")
            self.st[state["sort"]] = order[(order.index(cur) + 1) % len(order) if cur in order else 0]
            self.save()
            ui.msg = "SORTED BY " + SORTS[self.st[state["sort"]]]
        else:
            return False
        return True

    def rename(self, obj, noun, items):
        """Ask for a new title. Returns obj's new row in items() (sorting may move it), or None."""
        t = self.ui.prompt(f"RENAME {noun}", obj.get("title") or "")
        if t is None or not t.strip() or t.strip() == (obj.get("title") or ""):
            return None
        obj.update(title=t.strip(), updated=now())
        self.save()
        self.ui.msg = "RENAMED."
        xs = items()
        return next((j for j, x in enumerate(xs) if x is obj), None)

    def preview(self, doc):
        rows, w, x0 = self.ui.dims()
        lines = render_lines(doc, width=min(78, w - 4))
        self.ui.view_lines("PREVIEW: " + doc["title"], [(l, None) for l in lines])

    # -------------------------------------------------------------- lessons
    def lessons_screen(self):
        ui = self.ui
        state = {"q": "", "unit": "", "sort": "lesson_sort"}

        def items():
            return self.filtered(self.store.data["lessons"], state, ("title", "unit", "course", "date", "standards"))

        def row(l, w):
            right = "  ".join(x for x in (l.get("unit"), l.get("date")) if x)
            right = truncate(right, w // 2)
            left = truncate(l.get("title") or "(untitled lesson)", w - len(right) - 2)
            return left.ljust(w - len(right)) + right, ui.HI

        def on_key(k, l, i):
            c = ch(k).lower()
            if c == "n":
                l = new_lesson(self.st)
                self.store.data["lessons"].append(l)
                t = ui.prompt("LESSON TITLE")
                if t is None:
                    self.store.data["lessons"].remove(l)
                    return None
                l["title"] = t
                if state["unit"]:
                    l["unit"] = state["unit"]
                self.save()
                self.lesson_editor(l)
                return 0
            if self.filter_key(c, state, self.store.data["lessons"]):
                return 0
            if not l:
                return None
            if c == "d" and ui.confirm(f"DELETE LESSON '{l.get('title') or 'untitled'}'"):
                self.store.data["lessons"].remove(l)
                self.save()
                ui.msg = "DELETED."
            elif c == "r":
                return self.rename(l, "LESSON", items)
            elif c == "c":
                dup = copy.deepcopy(l)
                dup.update(id=new_lesson(self.st)["id"], title=(l.get("title") or "Lesson") + " (copy)",
                           created=now(), updated=now())
                self.store.data["lessons"].append(dup)
                self.save()
                ui.msg = "DUPLICATED. THE COPY IS AT THE TOP."
                return 0
            elif c == "x":
                self.export_screen("lesson", l)
            elif c == "b":
                self.export_now(l)
            elif c == "m":
                self.export_now(l, "MAKEUP")
            elif c == "p":
                self.preview(lesson_doc(l, self.store))
            return None

        ui.list_screen("LESSON PLANS", items, row, on_open=lambda l, i: self.lesson_editor(l),
                       on_key=on_key, info_fn=lambda: self.filter_info(state, len(items()), "LESSONS"),
                       hints="N NEW  R RENAME  C COPY  D DEL  X EXPORT  B BOARD  M MAKE-UP  P PREVIEW  / SEARCH  U UNIT  O SORT  ESC BACK",
                       empty="NO LESSONS MATCH. PRESS N TO PLAN ONE." if self.store.data["lessons"] else "NO LESSONS YET. PRESS N TO PLAN ONE.")

    def lesson_editor(self, lesson):
        ui = self.ui

        def row(field, w):
            key, label, kind = field
            if kind == "standards":
                val = ", ".join(lesson.get("standards", [])) or "-"
            elif kind == "attached":
                val = "; ".join(a.get("title") or "untitled" for a in self.store.attached(lesson)) or "-"
            else:
                val = first_line(lesson.get(key)) or "-"
                extra = len([l for l in (lesson.get(key) or "").split("\n") if l.strip()]) - 1
                if extra > 0:
                    val += f"  (+{extra} more)"
            lab = ui.tx(label)
            return f"{lab:<38} {val}", (ui.HI if val != "-" else ui.DIM)

        def on_open(field, i):
            key, label, kind = field
            if kind == "line":
                v = ui.prompt(label, lesson.get(key, ""))
            elif kind == "text" and key == "bell_ringer":
                v = ui.edit_text(label, lesson.get(key, ""), fill=self.store.good_thing,
                                 fill_hint="CTRL-G RANDOM SEL PROMPT", fill_prefix=GOOD_THINGS_PREFIX)
            elif kind == "text":
                v = ui.edit_text(label, lesson.get(key, ""))
            elif kind == "attached":
                self.lesson_attachments(lesson)
                return None
            else:
                v = self.standards_picker(lesson.get("standards", []), lesson.get("grades") or self.st["grades"])
            if v is not None:
                lesson[key] = v
                lesson["updated"] = now()
                self.save()
            return min(i + 1, len(LESSON_FIELDS) - 1) if v is not None and kind != "standards" else None

        def on_key(k, field, i):
            c = ch(k).lower()
            if c == "x":
                self.export_screen("lesson", lesson)
            elif c == "b":
                self.export_now(lesson)
            elif c == "m":
                self.export_now(lesson, "MAKEUP")
            elif c == "p":
                self.preview(lesson_doc(lesson, self.store))
            elif c == "g":
                cur = (lesson.get("bell_ringer") or "").strip()
                if cur and not cur.startswith(GOOD_THINGS_PREFIX) and not ui.confirm("REPLACE THE BELL RINGER WITH AN SEL PROMPT"):
                    return None
                lesson["bell_ringer"] = self.store.good_thing(cur)
                lesson["updated"] = now()
                self.save()
                ui.msg = "SEL BELL RINGER ADDED. PRESS G AGAIN FOR ANOTHER."
                return [f[0] for f in LESSON_FIELDS].index("bell_ringer")
            elif c == "h":
                default = (lesson.get("title") + " Annotation") if lesson.get("title") else ""
                name = ui.prompt("ANNOTATION SHEET HEADING (E.G. HAMLET 4.1 ANNOTATION)", default)
                if not name:
                    return None
                a = self.add_assessment(ANNOTATION, name, lesson)
                hw = (lesson.get("homework") or "").rstrip()
                lesson["homework"] = (hw + "\n" if hw else "") + f"- Complete the annotation sheet: {name}"
                lesson["updated"] = now()
                self.save()
                ui.msg = f"ADDED '{a['title']}' TO HOMEWORK. IT EXPORTS WITH THE LESSON (X, ALL FORMATS)."
                return [f[0] for f in LESSON_FIELDS].index("homework")
            return None

        ui.list_screen("LESSON: " + (lesson.get("title") or "untitled"), lambda: LESSON_FIELDS, row,
                       on_open=on_open, on_key=on_key,
                       info_fn=lambda: f"EDITING '{lesson.get('title') or 'untitled'}'   LAST SAVED {lesson.get('updated', '')}",
                       hints="1-0/RETURN EDIT  G SEL PROMPT  H ANNOTATION HW  P PREVIEW  X EXPORT  B BOARD  M MAKE-UP  ESC BACK")

    def lesson_attachments(self, lesson):
        """The quizzes, worksheets, and sheets that go with a lesson (and export with it)."""
        ui = self.ui

        def items():
            return self.store.attached(lesson)

        def touch():
            lesson["updated"] = now()
            self.save()

        def on_key(k, a, i):
            c = ch(k).lower()
            ids = lesson["assessments"] = [x["id"] for x in items()]  # drop links to deleted ones
            if c == "n":
                a = self.new_assessment_flow(lesson)
                if a:
                    touch()
                    self.open_assessment(a)
                    return len(ids) - 1
                return None
            if c == "l":
                unit = (lesson.get("unit") or "").strip()
                pool = [x for x in sort_items(self.store.data["assessments"], "updated") if x["id"] not in ids]
                pool.sort(key=lambda x: not unit or (x.get("unit") or "").strip() != unit)  # this unit first
                if not pool:
                    ui.msg = "?NOTHING ELSE TO LINK. PRESS N TO BUILD ONE."
                    return None
                j = ui.choose("LINK WHICH ONE TO THIS LESSON?",
                              [f"{x.get('kind', ''):<18}{x.get('title') or '(untitled)'}"
                               + (f"   [{x['unit']}]" if (x.get("unit") or "").strip() else "") for x in pool])
                if j is not None:
                    ids.append(pool[j]["id"])
                    touch()
                    ui.msg = "LINKED."
                    return len(ids) - 1
                return None
            if a is None:
                return None
            if c == "r" and ui.confirm(f"REMOVE '{a.get('title') or 'untitled'}' FROM THIS LESSON (IT IS NOT DELETED)"):
                ids.remove(a["id"])
                touch()
                return max(0, i - 1)
            if c in ("+", "=", "-", "_"):
                j = i + 1 if c in ("+", "=") else i - 1
                if 0 <= j < len(ids):
                    ids[i], ids[j] = ids[j], ids[i]
                    touch()
                    return j
            elif c == "e":
                return self.rename(a, (a.get("kind") or "item").upper(), items)
            elif c == "x":
                self.export_screen("assessment", a)
            elif c == "p":
                self.preview(assessment_doc(a, self.store))
            return None

        ui.list_screen("WORKSHEETS: " + (lesson.get("title") or "untitled"), items, self.assessment_row,
                       on_open=lambda a, i: self.open_assessment(a), on_key=on_key,
                       info_fn=lambda: f"{len(items())} LINKED. EXPORT ALL ON THE LESSON PUTS THEM IN ITS FOLDER.",
                       hints="1-0 OPEN  N NEW  L LINK EXISTING  E RENAME  R REMOVE  +/- MOVE  P PREVIEW  X EXPORT  ESC BACK",
                       empty="NOTHING YET. N BUILDS A QUIZ, WORKSHEET, ANNOTATION OR BELL RINGER SHEET; L LINKS ONE YOU MADE.")

    # ------------------------------------------------------------ standards
    def std_subject(self):
        """Subject the pickers start on: the last one chosen this session, else the default from Settings."""
        s = getattr(self, "_std_subject", None) or self.st.get("subject") or DEFAULT_SUBJECT
        return s if s in self.store.subjects else ALL

    def std_items(self, grades, query, subject=ALL):
        items = self.store.standards
        if subject != ALL:
            items = [s for s in items if s["subject"] == subject or s.get("custom")]
        if grades != ALL:
            items = [s for s in items if grades_match(s["grades"], grades)]
        if query:
            q = query.lower()
            items = [s for s in items if q in s["code"].lower() or q in s["text"].lower()
                     or q in s["cluster"].lower() or q in s["strand"].lower()]
        self._codew = min(18, max([9] + [len(s["code"]) for s in items[:400]]))
        return items

    def std_row(self, s, w, mark=""):
        indent = "    " if s["parent"] else ""
        attr = self.ui.DIM if s["parent"] else self.ui.N
        return f"{mark}{indent}{s['code']:<{getattr(self, '_codew', 13)}} {s['text']}", attr

    def pick_grade(self, current):
        j = self.ui.choose("GRADE LEVEL", [grade_label(g) for g in GRADE_CHOICES],
                           GRADE_CHOICES.index(current) if current in GRADE_CHOICES else None)
        return None if j is None else GRADE_CHOICES[j]

    def std_filter_keys(self, c, state):
        """F picks the subject, G the grade. True when the key was one of them."""
        ui = self.ui
        if c == "f":
            opts = [ALL] + self.store.subjects
            j = ui.choose("SUBJECT", [ui.tx("All subjects" if o == ALL else o) for o in opts],
                          opts.index(state["s"]) if state["s"] in opts else None)
            if j is not None:
                state["s"] = self._std_subject = opts[j]
            return True
        if c == "g":
            g = self.pick_grade(state["g"])
            if g is not None:
                state["g"] = g
            return True
        return False

    def std_filter_info(self, state):
        subj = "ALL SUBJECTS" if state["s"] == ALL else state["s"].upper()
        return f"{subj}   {grade_label(state['g'])}" + (f"   SEARCH '{state['q']}'" if state["q"] else "")

    def std_detail(self, code):
        s = self.store.std_index.get(code)
        if not s:
            return
        ui = self.ui
        rows, w, x0 = ui.dims()
        width = min(80, w - 6)
        lines = [(s["code"], ui.HI)]
        lines += [(l, ui.DIM) for l in textwrap.wrap(
            f"{s['subject']}  /  {s['strand']}  /  {s['cluster']}  /  {grade_label(s['grades'])}", width)] + [("", None)]
        if s["parent"]:
            p = self.store.std_index[s["parent"]]
            lines += [(f"PART OF {p['code']}:", ui.DIM)] + [(l, ui.DIM) for l in textwrap.wrap(p["text"], width)] + [("", None)]
            lines += [(l, ui.N) for l in textwrap.wrap(sub_label(p, s["label"]) + s["text"], width, subsequent_indent="   ")]
        else:
            lines += [(l, ui.N) for l in textwrap.wrap(s["text"], width)]
            for k, v in s["subs"]:
                lines += [(l, ui.N) for l in textwrap.wrap(sub_label(s, k) + v, width, initial_indent="  ", subsequent_indent="     ")]
        lines += [("", None)] + [(l, ui.DIM) for l in textwrap.wrap("SOURCE: " + s["source"], width)]
        ui.view_lines(s["code"], lines, raw_title=True)

    def standards_picker(self, selected, grades):
        ui = self.ui
        sel = list(selected)
        state = {"g": grades if grades in GRADE_CHOICES else ALL, "s": self.std_subject(), "q": ""}

        def toggle(s, i):
            if s["code"] in sel:
                sel.remove(s["code"])
            else:
                sel.append(s["code"])

        def on_key(k, s, i):
            c = ch(k).lower()
            if self.std_filter_keys(c, state):
                return 0
            if c == "/":
                q = ui.prompt("SEARCH CODE OR TEXT (BLANK CLEARS)", state["q"])
                if q is not None:
                    state["q"] = q
                return 0
            if c == " " and s:
                toggle(s, i)
            elif c == "v" and s:
                self.std_detail(s["code"])
            elif c == "c":
                sel.clear()
            elif c == "s":
                state["q"], state["show"] = "", not state.get("show")
                return 0
            return None

        def items():
            if state.get("show"):
                return [self.store.std_index[c] for c in sel if c in self.store.std_index]
            return self.std_items(state["g"], state["q"], state["s"])

        ui.list_screen("PICK STANDARDS", items,
                       lambda s, w: self.std_row(s, w, "[X] " if s["code"] in sel else "[ ] "),
                       on_open=toggle, on_key=on_key,
                       info_fn=lambda: (f"{len(sel)} SELECTED: {', '.join(sel)}" if sel else "NONE SELECTED") +
                       "   " + self.std_filter_info(state) + ("   (SHOWING SELECTED)" if state.get("show") else ""),
                       hints="1-0/SPACE TOGGLE  V VIEW  F SUBJECT  G GRADE  / SEARCH  S SHOW SELECTED  C CLEAR  ESC DONE")
        return sel

    def standards_library(self):
        ui = self.ui
        g = self.st.get("grades", "9-10")
        state = {"g": g if g in GRADE_CHOICES else ALL, "s": self.std_subject(), "q": ""}

        def on_key(k, s, i):
            c = ch(k).lower()
            if self.std_filter_keys(c, state):
                return 0
            if c == "/":
                q = ui.prompt("SEARCH CODE OR TEXT (BLANK CLEARS)", state["q"])
                if q is not None:
                    state["q"] = q
                return 0
            if c == "a":
                code = ui.prompt("CODE FOR YOUR STANDARD (E.G. SL.9-10.1 OR DISTRICT-1)")
                if not code:
                    return None
                if code in self.store.std_index:
                    ui.msg = "?THAT CODE ALREADY EXISTS."
                    return None
                text = ui.edit_text(f"STANDARD {code}", "", "ENTER THE FULL TEXT OF THE STANDARD. ESC SAVES.")
                if text and text.strip():
                    self.store.data["custom_standards"].append({"code": code, "text": " ".join(text.split()), "grades": "Custom"})
                    self.store.reload_standards()
                    self.save()
                    ui.msg = f"ADDED {code}."
            elif c == "i":
                self.import_standards()
                return 0
            elif c == "x":
                self.remove_standards()
                return 0
            elif c == "d" and s:
                if not s.get("custom"):
                    ui.msg = "?ONLY YOUR OWN CUSTOM STANDARDS CAN BE DELETED."
                elif ui.confirm(f"DELETE CUSTOM STANDARD {s['code']}"):
                    self.store.data["custom_standards"] = [x for x in self.store.data["custom_standards"] if x["code"] != s["code"]]
                    self.store.reload_standards()
                    self.save()
            return None

        ui.list_screen("STANDARDS LIBRARY", lambda: self.std_items(state["g"], state["q"], state["s"]), self.std_row,
                       on_open=lambda s, i: self.std_detail(s["code"]), on_key=on_key,
                       info_fn=lambda: self.std_filter_info(state),
                       hints="1-0 VIEW  F SUBJECT  G GRADE  / SEARCH  I IMPORT  X REMOVE  A ADD CUSTOM  D DELETE  ESC BACK",
                       empty="NO STANDARDS YET. I IMPORTS A CSV OR JSON FILE OF STANDARDS; A ADDS ONE BY HAND.")

    def import_standards(self):
        ui = self.ui
        path = ui.prompt("PATH TO A .CSV OR .JSON STANDARDS FILE (DRAG IT HERE)", raw=True)
        if not path:
            return
        try:
            done = self.store.import_standards(path)
        except (ValueError, OSError) as e:
            ui.msg = "?" + str(e).upper()
            return
        if self.store.warning:
            ui.msg, self.store.warning = self.store.warning, None
            return
        ui.msg = "IMPORTED " + "; ".join(f"{subj.upper()}: {n}" + (f" ({skip} SKIPPED: CODE ALREADY USED)" if skip else "")
                                         for subj, n, skip in done)
        self._std_subject = done[0][0]

    def remove_standards(self):
        ui = self.ui
        opts = self.store.imported_subjects()
        if not opts:
            ui.msg = "?NO IMPORTED SUBJECTS TO REMOVE."
            return
        j = ui.choose("REMOVE WHICH SUBJECT", [ui.tx(o) for o in opts])
        if j is not None and ui.confirm(f"REMOVE {opts[j].upper()} (LESSONS KEEP THEIR CODES)"):
            self.store.remove_subject(opts[j])
            ui.msg = f"REMOVED {opts[j].upper()}."

    # ---------------------------------------------------------- assessments
    def assessments_screen(self):
        ui = self.ui
        state = {"q": "", "unit": "", "kind": "", "sort": "assess_sort"}

        def items():
            return self.filtered(self.store.data["assessments"], state, ("title", "kind", "course", "unit", "standards"))

        def on_key(k, a, i):
            c = ch(k).lower()
            if c == "n":
                a = self.new_assessment_flow(unit=state["unit"])
                if a:
                    self.open_assessment(a)
                    return 0
                return None
            if self.filter_key(c, state, self.store.data["assessments"]):
                return 0
            if not a:
                return None
            used = [l for l in self.store.data["lessons"] if a["id"] in (l.get("assessments") or [])]
            also = f" (LINKED TO {len(used)} LESSON{'S' if len(used) != 1 else ''})" if used else ""
            if c == "d" and ui.confirm(f"DELETE '{a.get('title') or 'untitled'}'{also}"):
                self.store.data["assessments"].remove(a)
                for l in used:
                    l["assessments"].remove(a["id"])
                self.save()
                ui.msg = "DELETED."
            elif c == "r":
                return self.rename(a, (a.get("kind") or "item").upper(), items)
            elif c == "c":
                dup = copy.deepcopy(a)
                dup.update(id=new_assessment(self.st)["id"], title=(a.get("title") or "Assessment") + " (copy)",
                           created=now(), updated=now())
                self.store.data["assessments"].append(dup)
                self.save()
                ui.msg = "DUPLICATED. THE COPY IS AT THE TOP."
                return 0
            elif c == "x":
                self.export_screen("assessment", a)
            elif c == "p":
                self.preview(assessment_doc(a, self.store))
            return None

        ui.list_screen("ASSESSMENTS & ASSIGNMENTS", items, self.assessment_row, on_open=lambda a, i: self.open_assessment(a),
                       on_key=on_key, info_fn=lambda: self.filter_info(state, len(items()), "ITEMS"),
                       hints="1-0 OPEN  N NEW  R RENAME  C COPY  D DEL  X EXPORT  P PREVIEW  / SEARCH  U UNIT  T TYPE  O SORT  ESC BACK",
                       empty="NOTHING MATCHES. PRESS N TO BUILD SOMETHING." if self.store.data["assessments"]
                       else "NOTHING YET. PRESS N TO BUILD A QUIZ, TEST OR ASSIGNMENT.")

    def assessment_row(self, a, w):
        kind = a.get("kind", "")
        if kind == ANNOTATION:
            stat = f"{a.get('rows') or 10} ROWS"
        elif kind == BELL_SHEET:
            stat = "2 WEEKS"
        else:
            pts = sum(points_of(q) for q in a["questions"])
            n = sum(1 for q in a["questions"] if q["type"] not in ("passage", "section"))
            stat = f"{n:>3} Q  {fmt_points(pts):>4} PTS"
        unit = truncate((a.get("unit") or "").strip(), 16)
        right = f"{unit:<16}  {stat:>13}"
        left = f"{self.ui.tx(kind)[:17]:<18}{a.get('title') or '(untitled)'}"
        return truncate(left, w - len(right) - 2).ljust(w - len(right)) + right, self.ui.HI

    def new_assessment_flow(self, lesson=None, unit=""):
        """Ask what to make and its title. With a lesson, it inherits the lesson's unit, course,
        and standards and is linked to it."""
        ui = self.ui
        kinds = ASSESSMENT_KINDS + SHEET_KINDS
        j = ui.choose("WHAT ARE YOU MAKING?", [ui.tx(x) for x in ASSESSMENT_KINDS] +
                      [ui.tx(ANNOTATION + "  (line / symbol / reason chart)"),
                       ui.tx(BELL_SHEET + "  (Mon-Fri, one week per side)")])
        if j is None:
            return None
        kind = kinds[j]
        default = {ANNOTATION: (lesson.get("title") + " Annotation") if lesson and lesson.get("title") else "",
                   BELL_SHEET: "Bell Ringers"}.get(kind, "")
        t = ui.prompt(f"TITLE FOR THIS {kind}" + (" (PRINTS AS THE HEADING)" if kind == ANNOTATION else ""), default)
        if t is None:
            return None
        a = self.add_assessment(kind, t, lesson)
        if unit and not lesson:
            a["unit"] = unit
        return a

    def add_assessment(self, kind, title, lesson=None):
        a = new_assessment(self.st, kind)
        a["title"] = title
        if lesson:
            a.update(unit=lesson.get("unit") or "", course=lesson.get("course") or a["course"],
                     grades=lesson.get("grades") or a["grades"], standards=list(lesson.get("standards") or []))
            lesson.setdefault("assessments", []).append(a["id"])
        self.store.data["assessments"].append(a)
        self.save()
        return a

    def open_assessment(self, a):
        if a.get("kind") in SHEET_KINDS:
            self.sheet_editor(a)
        else:
            self.assessment_editor(a)

    def sheet_editor(self, a):
        """Settings for the fixed-layout sheets (annotation chart, bell ringer sheet)."""
        ui = self.ui
        bell = a.get("kind") == BELL_SHEET
        if bell:
            a.setdefault("weeks", ["", ""])
            a.setdefault("days", [""] * 10)
            fields = [("title", "TITLE", "line"), ("unit", "UNIT", "line"), ("course", "COURSE", "line"),
                      ("instructions", "DIRECTIONS (OPTIONAL)", "text")]
            for w in range(2):
                fields.append(("week", f"WEEK {w + 1} ({'FRONT' if w == 0 else 'BACK'}) WEEK OF", w))
                fields += [("day", f"  {WEEKDAYS[d].upper()} PROMPT", w * 5 + d) for d in range(5)]
        else:
            fields = [("title", "HEADING (UNDER NAME/DATE)", "line"), ("unit", "UNIT", "line"),
                      ("course", "COURSE", "line"), ("rows", "ROWS IN THE CHART", "rows"),
                      ("instructions", "DIRECTIONS (OPTIONAL)", "text")]

        def value(f):
            key, label, extra = f
            if key == "week":
                return a["weeks"][extra] or "(blank line to fill in)"
            if key == "day":
                return first_line(a["days"][extra]) or "(blank: students write the prompt)"
            return first_line(str(a.get(key) or "")) or "-"

        def row(f, w):
            return f"{f[1]:<30} {value(f)}", ui.HI if f[0] not in ("week", "day") or value(f)[0] != "(" else ui.DIM

        def changed():
            a["updated"] = now()
            self.save()

        def on_open(f, i):
            key, label, extra = f
            v = None
            if key == "week":
                v = ui.prompt("WEEK OF (E.G. OCT 5-9; BLANK = LINE TO FILL IN)", a["weeks"][extra])
                if v is not None:
                    a["weeks"][extra] = v
                    changed()
                return None
            if key == "day":
                v = ui.edit_text(label.strip(), a["days"][extra], "THE PROMPT PRINTED IN THIS DAY'S BOX. ESC SAVES.",
                                 fill=self.store.good_thing, fill_hint="CTRL-G RANDOM SEL PROMPT",
                                 fill_prefix=GOOD_THINGS_PREFIX)
                if v is not None:
                    a["days"][extra] = v.strip()
                    changed()
                return min(i + 1, len(fields) - 1) if v is not None else None
            if extra == "line":
                v = ui.prompt(label, a.get(key, ""))
            elif extra == "text":
                v = ui.edit_text(label, a.get(key, ""), "PRINTED UNDER THE HEADING. ESC SAVES.")
            elif extra == "rows":
                n = ui.prompt("ROWS (1-20; 10 FILLS ONE PAGE NICELY)", str(a.get("rows") or 10), replace=True)
                if n and n.isdigit() and 1 <= int(n) <= 20:
                    v = int(n)
            if v is not None:
                a[key] = v
                changed()
            return None

        def on_key(k, f, i):
            c = ch(k).lower()
            if c == "p":
                self.preview(assessment_doc(a, self.store))
            elif c == "x":
                self.export_screen("assessment", a)
            elif c == "g" and f and f[0] == "day":
                a["days"][f[2]] = self.store.good_thing(a["days"][f[2]])
                changed()
            return None

        ui.list_screen(a.get("title") or "UNTITLED", lambda: fields, row, on_open=on_open, on_key=on_key,
                       info_fn=lambda: ("BELL RINGER SHEET: 2 PAGES, PRINT DOUBLE-SIDED" if bell else
                                        "ANNOTATION SHEET: NAME/DATE, HEADING, LINE / SYMBOL / REASON CHART ON ONE PAGE"),
                       hints="1-0 CHANGE  " + ("G SEL PROMPT ON A DAY  " if bell else "") + "P PREVIEW  X EXPORT  ESC BACK")

    def assessment_editor(self, a):
        ui = self.ui
        qs = a["questions"]

        def touch():
            a["updated"] = now()
            self.save()

        def numbers():
            out, n = {}, 0
            for q in qs:
                if q["type"] not in ("passage", "section"):
                    n += 1
                    out[id(q)] = n
            return out

        def row(q, w):
            nums = numbers()
            t = q["type"]
            if t == "section":
                return f"{'':>3}  == {q.get('title') or 'SECTION'} ==", ui.HI
            if t == "passage":
                words = len((q.get("text") or "").split())
                return f"{'':>3}  [TEXT] {q.get('title') or 'Passage'}  ({words} words)", ui.DIM
            warn = ""
            if t == "mc" and (q.get("answer") is None or not q.get("choices")):
                warn = " (!NO KEY)"
            if t == "tf" and q.get("answer") is None:
                warn = " (!NO KEY)"
            pts = fmt_points(points_of(q))
            return f"{nums[id(q)]:>3}. {TYPE_TAG[t]:<5}{pts:>3}p  {first_line(q.get('prompt')) or '(no prompt)'}{warn}", None

        def info():
            pts = sum(points_of(q) for q in qs)
            n = sum(1 for q in qs if q["type"] not in ("passage", "section"))
            return f"{a.get('kind', '').upper()}  |  {n} QUESTIONS  |  {fmt_points(pts)} POINTS  |  " \
                   f"STANDARDS: {', '.join(a.get('standards', [])) or 'NONE'}"

        def on_key(k, q, i):
            c = ch(k).lower()
            if c == "a":
                at = len(qs) if i is None else i + 1
                newq = self.add_question(a)
                if newq:
                    qs.insert(at, newq)
                    touch()
                    return at
                return None
            if c == "s":
                self.assessment_settings(a)
                return None
            if c == "x":
                self.export_screen("assessment", a)
                return None
            if c == "p":
                self.preview(assessment_doc(a, self.store))
                return None
            if c == "k":
                self.preview(assessment_doc(a, self.store, key=True))
                return None
            if q is None:
                return None
            if c == "d" and ui.confirm(f"DELETE THIS {TYPE_LABEL[q['type']]}"):
                qs.remove(q)
                touch()
                return max(0, i - 1)
            if c == "c":
                qs.insert(i + 1, copy.deepcopy(q))
                touch()
                return i + 1
            if c in ("+", "=") and i < len(qs) - 1:
                qs[i], qs[i + 1] = qs[i + 1], qs[i]
                touch()
                return i + 1
            if c in ("-", "_") and i > 0:
                qs[i], qs[i - 1] = qs[i - 1], qs[i]
                touch()
                return i - 1
            if c == "m":
                to = ui.prompt(f"MOVE TO POSITION (1-{len(qs)})")
                if to and to.isdigit():
                    j = max(0, min(len(qs) - 1, int(to) - 1))
                    qs.insert(j, qs.pop(i))
                    touch()
                    return j
            return None

        def on_open(q, i):
            self.question_editor(a, q)
            touch()

        ui.list_screen(a.get("title") or "UNTITLED", lambda: qs, row, on_open=on_open, on_key=on_key,
                       info_fn=info,
                       hints="A ADD  1-0 EDIT  D DEL  C COPY  +/- MOVE  S SETTINGS  P PREVIEW  K KEY  X EXPORT  ESC BACK",
                       empty="NO QUESTIONS YET. PRESS A TO ADD ONE, OR S FOR TITLE/DIRECTIONS/STANDARDS.")

    def assessment_settings(self, a):
        ui = self.ui
        fields = [
            ("title", "TITLE", "line"), ("kind", "TYPE", "kind"), ("unit", "UNIT", "line"), ("course", "COURSE", "line"),
            ("grades", "GRADE BAND", "grades"), ("standards", "STANDARDS", "standards"),
            ("instructions", "DIRECTIONS", "text"), ("show_name", "NAME / DATE / PERIOD LINE", "bool"),
            ("show_points", "SHOW POINT VALUES", "bool"), ("show_standards", "PRINT STANDARDS ON STUDENT COPY", "bool"),
        ]

        def row(f, w):
            key, label, kind = f
            v = a.get(key)
            if kind == "bool":
                v = "YES" if v else "NO"
            elif kind == "standards":
                v = ", ".join(v or []) or "-"
            else:
                v = first_line(v) or "-"
            return f"{label:<34} {v}", ui.HI

        def on_open(f, i):
            key, label, kind = f
            if kind == "line":
                v = ui.prompt(label, a.get(key, ""))
            elif kind == "text":
                v = ui.edit_text(label, a.get(key, ""), "DIRECTIONS PRINTED AT THE TOP. ESC SAVES.")
            elif kind == "kind":
                j = ui.choose("TYPE", [ui.tx(x) for x in ASSESSMENT_KINDS],
                              ASSESSMENT_KINDS.index(a["kind"]) if a.get("kind") in ASSESSMENT_KINDS else None)
                v = ASSESSMENT_KINDS[j] if j is not None else None
            elif kind == "grades":
                v = self.pick_grade(a.get("grades"))
            elif kind == "bool":
                v = not a.get(key)
            else:
                v = self.standards_picker(a.get("standards", []), a.get("grades") or self.st["grades"])
            if v is not None:
                a[key] = v
                a["updated"] = now()
                self.save()

        ui.list_screen("SETTINGS: " + (a.get("title") or "untitled"), lambda: fields, row, on_open=on_open,
                       info_fn=lambda: "THESE PRINT AT THE TOP OF THE STUDENT COPY",
                       hints="1-0 CHANGE  ESC BACK (AUTOSAVED)")

    # ------------------------------------------------------------ questions
    def default_std(self, a):
        return a["standards"][0] if len(a.get("standards", [])) == 1 else ""

    def add_question(self, a):
        ui = self.ui
        j = ui.choose("ADD WHAT KIND OF QUESTION?", [f"{label:<20} {desc}" for _, label, desc in QUESTION_TYPES])
        if j is None:
            return None
        t = QUESTION_TYPES[j][0]
        q = new_question(t, self.default_std(a))
        if t == "section":
            title = ui.prompt("SECTION TITLE (E.G. PART II: VOCABULARY)")
            if title is None:
                return None
            q["title"] = title
            d = ui.prompt("DIRECTIONS FOR THIS SECTION (OPTIONAL)")
            q["prompt"] = d or ""
            return q
        if t == "passage":
            q["title"] = ui.prompt("PASSAGE TITLE (OPTIONAL)") or ""
            text = ui.edit_text("PASSAGE TEXT", "", "PASTE OR TYPE THE TEXT. BLANK LINE OR NEW LINE = NEW PARAGRAPH. ESC SAVES.")
            if not text:
                return None
            q["text"] = text
            return q
        help_text = {"fill": "TYPE THE SENTENCE. USE ___ (3+ UNDERSCORES) FOR EACH BLANK. ESC SAVES.",
                     "match": "DIRECTIONS FOR THE MATCHING SET (E.G. MATCH EACH TERM TO ITS DEFINITION). ESC SAVES."}
        prompt = ui.edit_text(TYPE_LABEL[t] + " - PROMPT",
                              "Match each term with its definition." if t == "match" else "",
                              help_text.get(t, "TYPE THE QUESTION. ESC SAVES, CTRL-X CANCELS."))
        if prompt is None or not prompt.strip():
            return None
        q["prompt"] = prompt.strip()
        if t == "mc":
            while len(q["choices"]) < 6:
                c = ui.prompt(f"CHOICE {LETTERS[len(q['choices'])]} (BLANK WHEN DONE)")
                if not c:
                    break
                q["choices"].append(c)
            if q["choices"]:
                ans = ui.prompt(f"CORRECT ANSWER (A-{LETTERS[len(q['choices']) - 1]})")
                if ans and ans.strip().upper()[:1] in LETTERS[:len(q["choices"])]:
                    q["answer"] = LETTERS.index(ans.strip().upper()[:1])
        elif t == "tf":
            ans = ui.prompt("ANSWER (T/F)")
            if ans and ans.strip().lower()[:1] in ("t", "f"):
                q["answer"] = ans.strip().lower().startswith("t")
        elif t == "fill":
            q["answer"] = ui.prompt("ANSWER(S) FOR THE KEY") or ""
        elif t == "match":
            while len(q["pairs"]) < 10:
                term = ui.prompt(f"TERM {len(q['pairs']) + 1} (BLANK WHEN DONE)")
                if not term:
                    break
                m = ui.prompt(f"WHAT MATCHES '{term}'")
                if m is None:
                    break
                q["pairs"].append([term, m])
        elif t == "short":
            n = ui.prompt("HOW MANY WRITING LINES", "3", replace=True)
            q["lines"] = int(n) if n and n.isdigit() else 3
        elif t == "essay":
            k = ui.choose("WRITING SPACE", [SPACE_LABEL[x] for x in ("lines", "blank", "box")], 0)
            q["space"] = ("lines", "blank", "box")[k or 0]
            n = ui.prompt("HOW BIG (IN LINES, ~1/3 INCH EACH)", "12", replace=True)
            q["lines"] = int(n) if n and n.isdigit() else 12
        default_pts = {"essay": "10", "match": str(len(q.get("pairs", [])))}.get(t, "1")
        p = ui.prompt("POINTS", default_pts, replace=True)
        q["points"] = self.parse_points(p, q["points"])
        return q

    def parse_points(self, s, old):
        try:
            v = float(s)
            return int(v) if v.is_integer() else v
        except (TypeError, ValueError):
            return old

    def question_fields(self, q):
        t = q["type"]
        if t == "section":
            return [("title", "TITLE", "line"), ("prompt", "DIRECTIONS", "text")]
        if t == "passage":
            return [("title", "TITLE", "line"), ("text", "PASSAGE TEXT", "text"), ("numbered", "LINE NUMBERS (PDF)", "bool")]
        f = [("prompt", "PROMPT", "text")]
        if t == "mc":
            f += [("choices", "CHOICES", "choices"), ("answer", "CORRECT ANSWER", "mcans")]
        elif t == "tf":
            f += [("answer", "ANSWER", "tf")]
        elif t == "short":
            f += [("lines", "WRITING LINES", "int"), ("answer", "ANSWER KEY / EXEMPLAR", "text")]
        elif t == "essay":
            f += [("space", "WRITING SPACE", "space"), ("lines", "SIZE (LINES)", "int"),
                  ("answer", "RUBRIC / KEY NOTES", "text")]
        elif t == "fill":
            f += [("answer", "ANSWER(S) FOR KEY", "line")]
        elif t == "match":
            f += [("pairs", "TERMS & MATCHES", "pairs")]
        f += [("points", "POINTS" + (" (0 = 1 PER PAIR)" if t == "match" else ""), "num"),
              ("standard", "STANDARD", "std1")]
        return f

    def question_editor(self, a, q):
        ui = self.ui

        def value(key, kind):
            v = q.get(key)
            if kind == "choices":
                return "  ".join(f"{LETTERS[i]}) {c}" for i, c in enumerate(v or [])) or "-"
            if kind == "mcans":
                return LETTERS[v] if v is not None and v < len(q.get("choices", [])) else "-  (NOT SET)"
            if kind == "tf":
                return "-  (NOT SET)" if v is None else ("TRUE" if v else "FALSE")
            if kind == "bool":
                return "YES" if v else "NO"
            if kind == "space":
                return SPACE_LABEL.get(v, v)
            if kind == "pairs":
                return f"{len(v or [])} PAIRS: " + ", ".join(p[0] for p in v or []) if v else "-"
            if kind in ("int", "num"):
                return str(v)
            return first_line(v) or "-"

        def row(f, w):
            key, label, kind = f
            return f"{label:<26} {value(key, kind)}", ui.HI

        def on_open(f, i):
            key, label, kind = f
            v = None
            if kind == "line":
                v = ui.prompt(label, q.get(key, ""))
            elif kind == "text":
                v = ui.edit_text(label, q.get(key, ""))
            elif kind in ("int", "num"):
                s = ui.prompt(label, str(q.get(key, "")), replace=True)
                if s is not None:
                    v = self.parse_points(s, q.get(key)) if kind == "num" else (int(s) if s.isdigit() else q.get(key))
            elif kind == "bool":
                v = not q.get(key)
            elif kind == "tf":
                v = {None: True, True: False, False: None}[q.get(key)]
                q[key] = v
                v = None
            elif kind == "space":
                order = ["lines", "blank", "box"]
                v = order[(order.index(q.get(key, "lines")) + 1) % 3]
            elif kind == "choices":
                self.choices_editor(q)
            elif kind == "mcans":
                if q.get("choices"):
                    j = ui.choose("CORRECT ANSWER", [f"{LETTERS[i]}) {c}" for i, c in enumerate(q["choices"])], q.get("answer"))
                    if j is not None:
                        q["answer"] = j
            elif kind == "pairs":
                self.pairs_editor(q)
            elif kind == "std1":
                pool = a.get("standards") or [s["code"] for s in self.std_items(a.get("grades") or self.st["grades"], "",
                                                                                  self.std_subject())]
                opts = ["(none)"] + [f"{c:<{min(18, max(13, len(c)))}} {truncate(self.store.std_text(c), 70)}" for c in pool]
                cur = pool.index(q["standard"]) + 1 if q.get("standard") in pool else 0
                j = ui.choose("STANDARD FOR THIS QUESTION", opts, cur)
                if j is not None:
                    v = "" if j == 0 else pool[j - 1]
            if v is not None:
                q[key] = v

        ui.list_screen(TYPE_LABEL[q["type"]], lambda: self.question_fields(q), row, on_open=on_open,
                       info_fn=lambda: truncate(first_line(q.get("prompt") or q.get("title")) or "", 100),
                       hints="1-0 / ARROWS+RETURN CHANGE   ESC BACK (AUTOSAVED)")

    def choices_editor(self, q):
        ui = self.ui
        cs = q.setdefault("choices", [])

        def row(item, w):
            i, c = item
            mark = "   <== CORRECT" if q.get("answer") == i else ""
            return f"{LETTERS[i]})  {c}{mark}", ui.HI if mark else None

        def on_open(item, i):
            v = ui.prompt(f"CHOICE {LETTERS[i]}", item[1])
            if v:
                cs[i] = v

        def on_key(k, c, i):
            key = ch(k).lower()
            if key == "a" and len(cs) < 6:
                v = ui.prompt(f"CHOICE {LETTERS[len(cs)]}")
                if v:
                    cs.append(v)
                    return len(cs) - 1
            if c is None:
                return None
            if key in ("*", "k", " "):
                q["answer"] = i
            elif key == "d":
                cs.pop(i)
                ans = q.get("answer")
                if ans == i:
                    q["answer"] = None
                elif ans is not None and ans > i:
                    q["answer"] = ans - 1
                return max(0, i - 1)
            elif key in ("+", "=", "-", "_"):
                j = i + 1 if key in ("+", "=") else i - 1
                if 0 <= j < len(cs):
                    cs[i], cs[j] = cs[j], cs[i]
                    ans = q.get("answer")
                    q["answer"] = j if ans == i else (i if ans == j else ans)
                    return j
            return None

        ui.list_screen("ANSWER CHOICES", lambda: list(enumerate(cs)), row, on_open=on_open, on_key=on_key,
                       info_fn=lambda: truncate(first_line(q.get("prompt")), 100),
                       hints="1-0 EDIT  A ADD  K MARK CORRECT  D DELETE  +/- MOVE  ESC DONE",
                       empty="NO CHOICES. PRESS A TO ADD ONE.")

    def pairs_editor(self, q):
        ui = self.ui
        ps = q.setdefault("pairs", [])

        def row(p, w):
            half = (w - 6) // 2
            return f"{truncate(p[0], half):<{half}}  ->  {p[1]}", None

        def on_open(p, i):
            t = ui.prompt("TERM", p[0])
            if t is None:
                return
            m = ui.prompt(f"WHAT MATCHES '{t}'", p[1])
            if m is not None:
                ps[i] = [t, m]

        def on_key(k, p, i):
            key = ch(k).lower()
            if key == "a" and len(ps) < 10:
                t = ui.prompt(f"TERM {len(ps) + 1}")
                if t:
                    m = ui.prompt(f"WHAT MATCHES '{t}'")
                    if m is not None:
                        ps.append([t, m])
                        return len(ps) - 1
            if p is None:
                return None
            if key == "d":
                ps.pop(i)
                return max(0, i - 1)
            if key in ("+", "=", "-", "_"):
                j = i + 1 if key in ("+", "=") else i - 1
                if 0 <= j < len(ps):
                    ps[i], ps[j] = ps[j], ps[i]
                    return j
            return None

        ui.list_screen("MATCHING PAIRS", lambda: list(ps), row, on_open=on_open, on_key=on_key,
                       info_fn=lambda: "MATCHES ARE SHUFFLED AUTOMATICALLY ON THE STUDENT COPY  (MAX 10)",
                       hints="1-0 EDIT  A ADD  D DELETE  +/- MOVE  ESC DONE", empty="NO PAIRS. PRESS A TO ADD ONE.")

    # --------------------------------------------------------------- export
    def export_screen(self, kind, obj):
        ui = self.ui
        st = self.st
        done = []
        fkey = "lesson_export_format" if kind == "lesson" else "export_format"
        formats = FORMAT_ORDER[kind]
        if st.get(fkey) not in formats:
            st[fkey] = "PDF"

        def fields():
            f = [("format", f"FORMAT ........ {FORMAT_LABEL[st[fkey]]}")]
            linked = self.store.attached(obj) if kind == "lesson" else []
            if linked:
                f += [("linked", f"WORKSHEETS .... {len(linked)} LINKED"
                                 + ("  (INCLUDED: PDF + DOCX IN THE LESSON FOLDER)" if st[fkey] == "ALL"
                                    else "  (PICK ALL FORMATS TO INCLUDE THEM)"))]
            if kind == "assessment" and obj.get("kind") not in SHEET_KINDS:
                f += [("include", f"INCLUDE ....... {INCLUDE_LABEL[st['export_include']]}"),
                      ("versions", f"VERSIONS ...... {st['export_versions']}"
                                   + ("  (A = ORIGINAL ORDER; OTHERS SHUFFLED)" if st["export_versions"] > 1 else ""))]
            if st[fkey] in ("PNG", "ALL") and kind == "lesson":
                f += [("board", f"BOARD SLIDE ... {BOARD_STYLES[st['board_style']]}  (CHANGE SECTIONS)")]
            if st[fkey] != "PNG":
                f += [("font", f"FONT .......... {st['font'].upper()}  /  {st['page'].upper()} PAPER")]
            f += [("folder", f"FOLDER ........ {os.path.join(self.store.export_dir(), *export_subdirs(kind, obj))}"),
                  ("go", ">>> EXPORT NOW <<<")]
            return f

        def on_open(f, i):
            key = f[0]
            if key == "format":
                st[fkey] = formats[(formats.index(st[fkey]) + 1) % len(formats)]
            elif key == "include":
                order = ["BOTH", "STUDENT", "KEY"]
                st["export_include"] = order[(order.index(st["export_include"]) + 1) % 3]
            elif key == "versions":
                st["export_versions"] = st["export_versions"] % 4 + 1
            elif key == "board":
                self.board_settings()
            elif key == "linked":
                self.lesson_attachments(obj)
            elif key == "font":
                st["font"] = "Helvetica" if st["font"] == "Times" else "Times"
            elif key == "folder":
                v = ui.prompt("EXPORT FOLDER", self.store.export_dir())
                if v:
                    st["export_dir"] = v
            elif key == "go":
                files = self.do_export(kind, obj, st[fkey])
                if files:
                    done.extend(files)
                    return "back"
            self.save()
            return i

        title = obj.get("title") or "untitled"
        ui.list_screen("EXPORT: " + title, fields, lambda f, w: (f[1], ui.HI if f[0] == "go" else None),
                       on_open=on_open, info_fn=lambda: "PRESS A NUMBER TO CHANGE AN OPTION",
                       hints="1-0 CHANGE / EXPORT  ESC CANCEL", start=len(fields()) - 1)
        if done:
            self.export_done(done)

    def export_now(self, lesson, fmt="PNG"):
        files = self.do_export("lesson", lesson, fmt)
        if files:
            self.export_done(files)

    def do_export(self, kind, obj, fmt):
        try:
            return export(self.store, kind, obj, fmt)
        except ExportError as e:
            self.ui.msg = f"?{e}"
            return None

    def export_done(self, files):
        ui = self.ui
        folder = os.path.dirname(files[0])

        def on_open(path, i):
            ui.msg = "OPENING..." if open_path(path) else "?COULD NOT OPEN FILE"

        def on_key(k, path, i):
            if ch(k).lower() == "f":
                ui.msg = "OPENING FOLDER..." if open_path(folder) else "?COULD NOT OPEN FOLDER"
            return None

        ui.list_screen("EXPORT COMPLETE", lambda: files, lambda p, w: (os.path.basename(p), ui.HI),
                       on_open=on_open, on_key=on_key,
                       info_fn=lambda: f"SAVED {len(files)} FILE{'S' if len(files) != 1 else ''} TO {folder}",
                       hints="1-0 OPEN FILE  F OPEN FOLDER  ESC DONE")

    def board_settings(self):
        ui, st = self.ui, self.st

        def fields():
            on = st.get("board_sections") or []
            f = [("style", f"STYLE ............... {BOARD_STYLES[st['board_style']]}"),
                 ("std_text", "STANDARDS SHOW ...... " + ("CODE + FULL TEXT" if st.get("board_std_text", True) else "CODES ONLY"))]
            for key, label, col in BOARD_SECTIONS:
                f.append((key, f"[{'X' if key in on else ' '}] {label.upper():<17} ({col.upper()} SIDE)"))
            return f

        def on_open(f, i):
            key = f[0]
            if key == "style":
                order = list(BOARD_STYLES)
                st["board_style"] = order[(order.index(st["board_style"]) + 1) % len(order)]
            elif key == "std_text":
                st["board_std_text"] = not st.get("board_std_text", True)
            else:
                on = [k for k in st.get("board_sections") or [] if k != key]
                st["board_sections"] = on if len(on) < len(st.get("board_sections") or []) else on + [key]
            self.save()
            return i

        ui.list_screen("BOARD SLIDE OPTIONS", fields, lambda f, w: (f[1], ui.HI), on_open=on_open,
                       info_fn=lambda: "1920x1080 PNG. EMPTY SECTIONS ARE LEFT OFF; TEXT AUTO-SIZES TO FIT.",
                       hints="1-0 CHANGE  ESC BACK")

    # ------------------------------------------------------------- settings
    def settings_screen(self):
        ui, st = self.ui, self.st
        fields = [
            ("teacher", "TEACHER NAME (FOR FOOTERS)", "line"),
            ("school", "SCHOOL", "line"),
            ("course", "DEFAULT COURSE", "line"),
            ("subject", "DEFAULT SUBJECT (STANDARDS)", "subject"),
            ("grades", "DEFAULT GRADE BAND", "grades"),
            ("default_materials", "DEFAULT MATERIALS NEEDED", "text"),
            ("font", "DOCUMENT FONT", "font"),
            ("page", "PAPER SIZE", "page"),
            ("export_dir", "EXPORT FOLDER", "folder"),
            ("board_style", "BOARD SLIDE (DISPLAY PNG)", "board"),
            ("primary_color", "SCHOOL COLOR 1 (BACKGROUND)", "color"),
            ("secondary_color", "SCHOOL COLOR 2 (HEADINGS)", "color"),
            ("text_color", "SCHOOL COLOR 3 (TEXT)", "color"),
            ("theme", "SCREEN COLOR", "theme"),
            ("uppercase", "ALL-CAPS MENUS", "bool"),
            ("boot", "BOOT SEQUENCE", "bool"),
        ]

        def row(f, w):
            key, label, kind = f
            v = st.get(key)
            if kind == "bool":
                v = "ON" if v else "OFF"
            elif kind == "folder":
                v = self.store.export_dir()
            elif kind == "board":
                v = BOARD_STYLES.get(v, "") + " ..."
            elif kind == "grades":
                v = grade_label(v or ALL)
            elif kind == "text":
                extra = len([l for l in (v or "").split("\n") if l.strip()]) - 1
                v = first_line(v) + (f"  (+{extra} more)" if extra > 0 else "")
            return f"{label:<30} {v or '-'}", ui.HI

        def on_open(f, i):
            key, label, kind = f
            if kind == "color":
                v = ui.prompt(label + " HEX, E.G. #7A0019 (BLANK CLEARS)", st.get(key, ""))
                if v is not None:
                    code = parse_hex(v) if v else ""
                    if code is None:
                        ui.msg = f"?'{v}' IS NOT A HEX COLOR (E.G. #7A0019)"
                    else:
                        st[key] = code
                        if code:
                            st["board_style"] = "school"
                            ui.msg = "BOARD SLIDES NOW USE SCHOOL COLORS"
                            weak = low_contrast(st)
                            if weak:
                                ui.msg = f"NOTE: {' + '.join(weak)} MAY BE HARD TO READ ON THE BACKGROUND"
                        elif not any(st.get(k) for k in ("primary_color", "secondary_color", "text_color")) \
                                and st.get("board_style") == "school":
                            st["board_style"] = "chalk"
            elif kind in ("line", "folder"):
                v = ui.prompt(label, self.store.export_dir() if kind == "folder" else st.get(key, ""))
                if v is not None:
                    st[key] = v
            elif kind == "text":
                v = ui.edit_text(label, st.get(key, ""),
                                 "PRE-FILLS MATERIALS & TEXTS ON EVERY NEW LESSON. '- ' STARTS A BULLET. ESC SAVES.")
                if v is not None:
                    st[key] = v
            elif kind == "grades":
                v = self.pick_grade(st.get(key))
                if v is not None:
                    st[key] = v
            elif kind == "subject":
                opts = self.store.subjects
                if not opts:
                    ui.msg = "?IMPORT STANDARDS FIRST (MAIN MENU > STANDARDS LIBRARY > I)."
                    return i
                j = ui.choose(label, [ui.tx(o) for o in opts], opts.index(st[key]) if st.get(key) in opts else None)
                if j is not None:
                    st[key] = opts[j]
                    self._std_subject = None
            elif kind == "font":
                st[key] = "Helvetica" if st.get(key) == "Times" else "Times"
            elif kind == "page":
                st[key] = "A4" if st.get(key) == "Letter" else "Letter"
            elif kind == "theme":
                order = ["green", "amber", "white"]
                st[key] = order[(order.index(st.get(key, "green")) + 1) % 3]
                ui.apply_theme()
            elif kind == "bool":
                st[key] = not st.get(key)
            elif kind == "board":
                self.board_settings()
            self.save()
            return i

        ui.list_screen("SETTINGS", lambda: fields, row, on_open=on_open,
                       info_fn=lambda: f"DATA FILE: {self.store.path}",
                       hints="1-0 CHANGE  ESC BACK")


def main():
    ap = argparse.ArgumentParser(prog="chalkboard", description="Chalkboard -- retro lesson planner & assessment builder")
    ap.add_argument("--no-boot", action="store_true", help="skip the boot sequence")
    ap.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    args = ap.parse_args()
    os.environ.setdefault("ESCDELAY", "25")
    try:
        curses.wrapper(lambda s: App(s, args).run())
    except KeyboardInterrupt:
        pass
    print("]BYE.")
