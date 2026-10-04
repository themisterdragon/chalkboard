"""Chalkboard -- a retro, offline lesson planner and assessment builder."""

import argparse
import copy
import datetime
import os
import re
import textwrap

from . import __version__
from .ui import APP, BACK, UI, ExportBar, big, ch, curses, truncate
from .store import (LOGO_PLACES, ALL, BOARD_CHOICES, board_sections, move_board_section, ANNOTATION, ASSESSMENT_KINDS, BELL_SHEET, BOARD_SIDES, BOARD_STYLES, DEFAULT_SUBJECT,
                    FIXED_FIELDS, VOCAB_HINT, shown_fields, GOOD_THINGS_PREFIX, GRADE_CHOICES, LESSON_FIELDS, QUESTION_TYPES, SHEET_KINDS, SORTS, TYPE_LABEL,
                    TYPE_TAG, WEEKDAYS, Store, fmt_points, grades_match, new_assessment, new_lesson, new_question, now,
                    parse_hex, parse_points, points_of, sort_items)
from .doc import LETTERS, assessment_doc, curriculum_doc, layout_sample, lesson_doc, preview_doc, school_years
from .markup import plain
from .mascots import MASCOTS, hop, sprite
from .mascots import name as mascot_name
from .store import CHART_DIRECTIONS, CHART_PRESETS, CHART_SIZES, chart_preset, chart_size_label
from .store import LAYOUT_CHOICES, PAGE_LAYOUTS, advanced_changes, page_layout, reset_advanced, set_page_layout
from .store import (classes, day_plan, fmt_date, fmt_range, in_class, lesson_dates, school_year,
                    school_year_label)
from . import offline, plugins
from .exporting import (FORMAT_ORDER, PLUGIN_FORMATS, ExportError, export, export_day, export_everything,
                        export_map, export_subdirs, files_folder, open_path)
from . import fontlib
from .export_png import low_contrast, render_preview
from .export_txt import render_lines
FORMAT_LABEL = {"PDF": "PDF", "DOCX": "WORD (.DOCX - ALSO GOOGLE DOCS)", "TXT": "PLAIN TEXT",
                "PNG": "BOARD SLIDE (1920x1080 PNG + EDITABLE .PPTX SLIDESHOW)",
                "MAKEUP": "MAKE-UP SHEET + CLASSWORK FOR ABSENT STUDENTS (PDF + DOCX)", "ALL": "ALL FORMATS"}
INCLUDE_LABEL = {"STUDENT": "STUDENT COPY ONLY", "KEY": "ANSWER KEY ONLY", "BOTH": "STUDENT COPY + ANSWER KEY"}
SPACE_LABEL = {"lines": "LINED", "blank": "BLANK SPACE", "box": "BORDERED BOX"}
PRESET_LABEL = {k: label for k, label, _ in CHART_PRESETS}


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
            return plain(line.strip())
    return ""


class App:
    def __init__(self, stdscr, args, store=None):
        self.store = store or Store()  # the window app passes its own, so both views share one copy
        if not getattr(args, "no_plugins", False):
            plugins.load(self.store.dir)
        self.st = self.store.settings
        self.ui = UI(stdscr, self.st)
        self.args = args
        if self.store.warning:
            self.ui.msg = self.store.warning
        elif plugins.problems:
            self.ui.msg = "?" + "  ".join(plugins.take_problems())

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
            ui.draw(2 + i, x0 + 1, line, ui.HI)
            pause(50)
        key = self.st.get("mascot")
        art = sprite(key) if key in MASCOTS else None
        if art and (w < 77 or rows < 19):
            art = None  # no room beside the stats; skip it
        left = x0 + 1
        mx = left + 58
        if art:  # scan the mascot in, two pixel rows at a time
            runs = ui.sprite_runs(art)
            for dy in range(8):
                for ry, rx, text, attr in runs:
                    if ry == dy:
                        ui.draw(9 + ry, mx + rx, text, attr)
                pause(35)
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
        if art:
            cheer = f"GO {mascot_name(key)}!"
            ui.draw(18, mx + (16 - len(cheer)) // 2, cheer, ui.HI)
            for frame in (hop(art), art, hop(art), art):
                ui.draw_sprite(9, mx, frame)
                pause(110)
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
        leave = getattr(self.args, "leave", None)
        if leave:  # running inside the window app: switch back to its windows
            items.insert(4, ("W", "WINDOW VIEW", self.window_view))
        sel, first = 0, True
        while True:
            rows, w, x0 = ui.dims()
            ui.begin()
            ui.header("MAIN MENU")
            y = 2
            if rows >= 24:
                banner = big(APP, "██" if w >= 106 else "█")
                for i, line in enumerate(banner):
                    ui.put(y + i, x0 + 1, line, ui.HI)
                y += 6
            ui.line(y, "* LESSON PLANS, ASSESSMENTS & ASSIGNMENTS *", ui.DIM)
            y += 2
            mx = x0 + 1
            for i, (k, label, _) in enumerate(items):
                ui.badge(y, mx, k)
                ui.put(y, mx + 5, label, ui.INV if i == sel else ui.N)
                y += 2
            d = self.store.data
            if y < rows - 3:
                who = f"   //   {self.st['teacher']}" if self.st.get("teacher") else ""
                ui.line(y + 1, f"{len(d['lessons'])} LESSONS   //   {len(d['assessments'])} ASSESSMENTS{who.upper()}", ui.DIM)
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

    def window_view(self):
        """Back to the window app's windows (only when running inside it)."""
        self.save()
        self.args.leave("switch")

    # --------------------------------------------------------------- shared
    def search_filter(self, items, query, fields):
        if not query:
            return items
        q = query.lower()
        return [it for it in items if any(q in str(it.get(f, "")).lower() for f in fields)]

    def filtered(self, pool, state, fields):
        """Sort, then narrow a list by the unit/type filters and search in state."""
        xs = sort_items(pool, self.st.get(state["sort"], "updated"))
        if self.st.get(state["sort"]) == "class" and state.get("cls") is not None:
            xs = [x for x in xs if in_class(x, state["cls"])]
        if state["unit"]:
            xs = [x for x in xs if (x.get("unit") or "").strip() == state["unit"]]
        if state.get("kind"):
            xs = [x for x in xs if x.get("kind") == state["kind"]]
        return self.search_filter(xs, state["q"], fields)

    def filter_info(self, state, count, noun):
        bits = [f"{count} {noun}"]
        if self.st.get(state["sort"]) == "class" and state.get("cls") is not None:
            bits.insert(0, "CLASS: " + (state["cls"] or "NO CLASS YET").upper())
        if state["unit"]:
            bits.append(f"UNIT: {state['unit']}")
        if state.get("kind"):
            bits.append(f"TYPE: {state['kind']}")
        if state["q"]:
            bits.append(f"SEARCH: '{state['q']}'")
        bits.append("SORT: " + SORTS[self.st.get(state["sort"], "updated")])
        return "   ".join(bits)

    def class_list(self, title, state, pool, items, row, on_open, on_key, noun, **kw):
        """A lesson or assessment list that, sorted By Class, first shows the classes: pick one to see
        (and add to) just its items. Esc inside a class goes back to the classes."""
        ui = self.ui
        state.setdefault("cls", None)
        is_cls = lambda x: isinstance(x, tuple) and x[:1] == ("class",)

        def picking():
            return self.st.get(state["sort"]) == "class" and state["cls"] is None and not state["q"]

        def items2():
            return [("class", n, c) for n, c in classes(self.st, pool)] if picking() else items()

        def row2(x, w):
            if is_cls(x):
                return f"{(x[1] or 'NO CLASS YET').upper()}  ({x[2]} {noun}{'' if x[2] == 1 else 'S'})", ui.HI
            return row(x, w)

        def on_open2(x, i):
            if is_cls(x):
                state["cls"] = x[1]
                return 0
            return on_open(x, i)

        def on_key2(k, x, i):
            return on_key(k, None if is_cls(x) else x, i)

        info = kw.pop("info_fn")
        hints = kw.pop("hints")
        while True:
            ui.list_screen(title, items2, row2, on_open=on_open2, on_key=on_key2,
                           info_fn=lambda: "PICK A CLASS. (O CHANGES THE SORT.)" if picking() else info(),
                           hints=hints.replace("ESC BACK", "ESC ALL CLASSES") if (
                               self.st.get(state["sort"]) == "class" and state["cls"] is not None) else hints, **kw)
            if self.st.get(state["sort"]) == "class" and state["cls"] is not None and not state["q"]:
                state["cls"] = None
                continue
            return

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
        state = {"q": "", "unit": "", "sort": "lesson_sort", "cls": None}

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
                if self.st.get("lesson_sort") == "class" and state["cls"]:
                    l["course"] = state["cls"]
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

        def on_key_any(k, l, i):
            if ch(k).lower() == "w":
                self.day_slideshow()
                return None
            if ch(k).lower() == "y":
                self.curriculum_map(state.get("cls") if self.st.get("lesson_sort") == "class" else None)
                return None
            return on_key(k, l, i)

        self.class_list("LESSON PLANS", state, self.store.data["lessons"], items, row,
                        lambda l, i: self.lesson_editor(l), on_key_any, "LESSON",
                        info_fn=lambda: self.filter_info(state, len(items()), "LESSONS"),
                        hints="N NEW  R RENAME  C COPY  D DEL  X EXPORT  B BOARD  M MAKE-UP  W WHOLE DAY  Y YEAR MAP  P PREVIEW  "
                              "/ SEARCH  U UNIT  O SORT  ESC BACK",
                        empty="NO LESSONS MATCH. PRESS N TO PLAN ONE." if self.store.data["lessons"]
                        else "NO LESSONS YET. PRESS N TO PLAN ONE.")

    def lesson_editor(self, lesson):
        ui = self.ui

        def fields():
            return shown_fields(self.st, lesson)

        def at(key):
            return next((i for i, f in enumerate(fields()) if f[0] == key), None)

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
            if kind == "line" and key == "date":  # a calendar (it writes the year), or / to type it
                days = lesson_dates(lesson)
                got = ui.calendar("DATE(S): " + (lesson.get("title") or "UNTITLED").upper(),
                                  days[0] if days else None, days[-1] if days else None, self.lesson_marks(),
                                  allow_range=True, allow_type=True)
                v = ui.prompt(label, lesson.get(key, "")) if got == "type" else fmt_range(*got) if got else None
            elif kind == "line":
                v = ui.prompt(label, lesson.get(key, ""))
            elif kind == "text" and key == "bell_ringer":
                v = ui.edit_text(label, lesson.get(key, ""), fill=self.store.good_thing,
                                 fill_hint="CTRL-G RANDOM SEL PROMPT", fill_prefix=GOOD_THINGS_PREFIX)
            elif kind == "text":
                v = ui.edit_text(label, lesson.get(key, ""))
            elif kind == "vocab":
                v = ui.edit_text(label, lesson.get(key, ""), ui.tx(VOCAB_HINT) + ". ESC SAVES. THEN V ON THE LESSON MAKES A QUIZ.")
            elif kind == "attached":
                self.lesson_attachments(lesson)
                return None
            else:
                v = self.standards_picker(lesson.get("standards", []), lesson.get("grades") or self.st["grades"])
            if v is not None:
                lesson[key] = v
                lesson["updated"] = now()
                self.save()
            return min(i + 1, len(fields()) - 1) if v is not None and kind != "standards" else None

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
                return at("bell_ringer")
            elif c == "v":
                a = self.store.vocab_quiz(lesson)
                if a is None:
                    ui.msg = "?TYPE AT LEAST TWO WORDS WITH DEFINITIONS IN VOCABULARY FIRST (WORD: DEFINITION)"
                    return at("vocab")
                self.save()
                ui.msg = f"MADE '{a['title']}' ({len(a['questions'][0]['pairs'])} WORDS) AND LINKED IT TO THIS LESSON"
                return at("assessments")
            elif c == "s":
                self.lesson_sections()
                return 0
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
                return at("homework")
            return None

        ui.list_screen("LESSON: " + (lesson.get("title") or "untitled"), fields, row,
                       on_open=on_open, on_key=on_key,
                       info_fn=lambda: f"EDITING '{lesson.get('title') or 'untitled'}'   LAST SAVED {lesson.get('updated', '')}",
                       hints="1-0/RETURN EDIT  G SEL PROMPT  H ANNOTATION HW  V VOCAB QUIZ  S SECTIONS  P PREVIEW  "
                             "X EXPORT  B BOARD  M MAKE-UP  ESC BACK")

    def lesson_sections(self):
        ui, st = self.ui, self.st
        keys = [(k, label) for k, label, _ in LESSON_FIELDS if k not in FIXED_FIELDS]

        def row(f, w):
            on = f[0] not in (st.get("lesson_hide") or [])
            return f"[{'X' if on else ' '}] {ui.tx(f[1])}", (ui.HI if on else ui.DIM)

        def on_open(f, i):
            hide = st.get("lesson_hide") or []
            st["lesson_hide"] = [k for k in hide if k != f[0]] if f[0] in hide else hide + [f[0]]
            self.save()
            return i

        ui.list_screen("LESSON SECTIONS", lambda: keys, row, on_open=on_open,
                       info_fn=lambda: "UNCHECKED SECTIONS ARE HIDDEN WHILE EMPTY. NOTHING IS DELETED.",
                       hints="1-0/RETURN SHOW OR HIDE  ESC BACK")

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
            elif c == "e":
                self.export_standards()
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
                       hints="1-0 VIEW  F SUBJECT  G GRADE  / SEARCH  I IMPORT  E EXPORT  X REMOVE  A ADD CUSTOM  D DELETE  ESC BACK",
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

    def export_standards(self):
        """Save imported or hand-typed standards to a file a colleague can import (bundled ones stay put)."""
        ui = self.ui
        opts = self.store.shareable_subjects()
        if not opts:
            ui.msg = "?NOTHING TO SHARE YET: ONLY STANDARDS YOU IMPORTED OR ADDED CAN BE EXPORTED."
            return
        labels = [ui.tx(o) for o in opts]
        if len(opts) > 1:
            labels.insert(0, "ALL OF THEM")
        j = ui.choose("EXPORT WHICH STANDARDS TO SHARE", labels)
        if j is None:
            return
        pick = opts if len(opts) > 1 and j == 0 else [opts[j - (len(opts) > 1)]]
        folder = ui.prompt("SAVE IN WHICH FOLDER (DRAG ONE HERE)", self.store.export_dir(), raw=True)
        if not folder:
            return
        try:
            path = self.store.export_standards(folder, pick)
        except (ValueError, OSError) as e:
            ui.msg = "?" + str(e).upper()
            return
        ui.msg = f"SAVED {path}  (ANOTHER TEACHER IMPORTS IT WITH I)"

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
        state = {"q": "", "unit": "", "kind": "", "sort": "assess_sort", "cls": None}

        def items():
            return self.filtered(self.store.data["assessments"], state, ("title", "kind", "course", "unit", "standards"))

        def on_key(k, a, i):
            c = ch(k).lower()
            if c == "n":
                a = self.new_assessment_flow(unit=state["unit"])
                if a and self.st.get("assess_sort") == "class" and state["cls"]:
                    a["course"] = state["cls"]
                    self.save()
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

        self.class_list("ASSESSMENTS & ASSIGNMENTS", state, self.store.data["assessments"], items, self.assessment_row,
                        lambda a, i: self.open_assessment(a), on_key, "ITEM",
                        info_fn=lambda: self.filter_info(state, len(items()), "ITEMS"),
                        hints="1-0 OPEN  N NEW  R RENAME  C COPY  D DEL  X EXPORT  P PREVIEW  / SEARCH  U UNIT  T TYPE  "
                              "O SORT  ESC BACK",
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
            prompt = first_line(q.get("prompt"))
            if t == "chart":
                prompt = f"[{PRESET_LABEL.get(q.get('preset'), 'Chart').upper()}] " + (
                    prompt or CHART_DIRECTIONS.get(q.get("layout"), ""))
            return f"{nums[id(q)]:>3}. {TYPE_TAG[t]:<5}{pts:>3}p  {prompt or '(no prompt)'}{warn}", None

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
        if t == "chart":
            k = ui.choose("WHAT KIND OF CHART?", [label.upper() for _, label, _ in CHART_PRESETS], 0)
            if k is None:
                return None
            chart_preset(q, CHART_PRESETS[k][0])
            d = ui.edit_text("CHART - DIRECTIONS", "", "DIRECTIONS FOR STUDENTS (OPTIONAL; BLANK PRINTS A SHORT DEFAULT). "
                                                     "ESC SAVES. NEXT YOU CAN SET LABELS AND SIZE.")
            if d is None:
                return None
            q["prompt"] = d.strip()
            self.question_editor(a, q)
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
        q["points"] = parse_points(p, q["points"])
        return q

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
        elif t == "chart":
            lay = q.get("layout")
            f = [("prompt", "DIRECTIONS", "text"), ("preset", "KIND (RESETS LABELS)", "preset")]
            if lay == "table":
                f += [("cols", "COLUMNS (1-8)", "int"), ("rows", "ROWS (1-20)", "int"),
                      ("heads", "COLUMN HEADINGS", "list"), ("sidecol", "LABEL EACH ROW (MATRIX)", "bool"),
                      ("side", "ROW LABELS", "list")]
            elif lay == "venn":
                f += [("circles", "CIRCLES (2 OR 3)", "int"), ("heads", "CIRCLE LABELS", "list")]
            elif lay == "web":
                f += [("center", "CENTER TOPIC", "line"), ("rows", "BUBBLES (3-8)", "int"),
                      ("heads", "BUBBLE LABELS", "list")]
            elif lay == "sequence":
                f += [("rows", "BOXES (2-8)", "int"), ("heads", "BOX LABELS", "list")]
            elif lay == "frayer":
                f += [("center", "WORD IN THE MIDDLE", "line"), ("heads", "CORNER LABELS", "list")]
            else:
                f += [("heads", "STAGE LABELS", "list")]
            f += [("lines", "SIZE", "size"), ("answer", "KEY NOTES", "text")]
        f += [("points", "POINTS" + (" (0 = 1 PER PAIR)" if t == "match" else " (0 = NOT SCORED)" if t == "chart" else ""), "num"),
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
            if kind == "list":
                return " / ".join(x or "(blank)" for x in v or []) or "-"
            if kind == "preset":
                return PRESET_LABEL.get(v, "-").upper()
            if kind == "size":
                return chart_size_label(v or 0).upper()
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
                    v = parse_points(s, q.get(key)) if kind == "num" else (int(s) if s.isdigit() else q.get(key))
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
            elif kind == "list":
                t = ui.edit_text(label, "\n".join(q.get(key) or []), "ONE PER LINE. ESC SAVES.")
                if t is not None:
                    v = [x.strip() for x in t.strip().split("\n")] if t.strip() else []
            elif kind == "preset":
                keys = [k for k, _, _ in CHART_PRESETS]
                j = ui.choose("WHAT KIND OF CHART?", [label.upper() for _, label, _ in CHART_PRESETS],
                              keys.index(v) if (v := q.get(key)) in keys else None)
                v = None
                if j is not None:
                    chart_preset(q, keys[j])
            elif kind == "size":
                j = ui.choose("SIZE", [chart_size_label(n).upper() for n in CHART_SIZES],
                              CHART_SIZES.index(q.get(key)) if q.get(key) in CHART_SIZES else None)
                if j is not None:
                    v = CHART_SIZES[j]
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
            f = [("format", f"FORMAT ........ {FORMAT_LABEL.get(st[fkey]) or PLUGIN_FORMATS.get(st[fkey], st[fkey]).upper()}")]
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
                f += [("board", f"BOARD SLIDE ... {BOARD_STYLES[st['board_style']]}  (BOARD DESIGNER)")]
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

    def lesson_marks(self):
        """For ui.calendar: days that already have a lesson."""
        days = {d for l in self.store.data["lessons"] for d in lesson_dates(l)}
        return lambda d: d in days

    def day_slideshow(self):
        """W in Lesson Plans: every class period's board slide, in order, in one slideshow (and a PDF)."""
        ui, st = self.ui, self.st
        if not st.get("class_periods"):
            ui.msg = "?ADD YOUR CLASS PERIODS FIRST: SETTINGS > CLASS PERIODS & CODES."
            return
        got = ui.calendar("DAY SLIDESHOW: PICK THE DAY", datetime.date.today(), marks=self.lesson_marks())
        if not got:
            return
        md = got[0]
        lessons = sorted(self.store.data["lessons"], key=lambda l: l.get("updated", ""), reverse=True)
        plan = [list(x) for x in day_plan(st, lessons, md)]

        def row(x, w):
            p, l = x
            name = (p.get("name") or "CLASS") + (f" ({p['course']})" if (p.get("course") or "").strip() else "")
            return f"{name.upper()[:30]:<31} {(l.get('title') or 'UNTITLED') if l else '(SKIPPED)'}", ui.HI

        def on_open(x, i):
            course = (x[0].get("course") or "").strip().lower()
            mine = [l for l in lessons if not course or (l.get("course") or "").strip().lower() == course]
            labels = ["(SKIP THIS PERIOD)"] + [(l.get("title") or "untitled") + (
                f"  {first_line(l.get('date'))}" if l.get("date") else "") for l in mine]
            j = ui.choose("LESSON FOR " + (x[0].get("name") or "THIS CLASS").upper(), labels,
                          mine.index(x[1]) + 1 if x[1] in mine else 0)
            if j is not None:
                x[1] = mine[j - 1] if j else None
            return i

        def on_key(k, x, i):
            if ch(k).upper() == "M":
                try:
                    files = export_day(self.store, plan, md)
                except ExportError as e:
                    ui.msg = f"?{e}"
                    return None
                self.export_done(files)
                return "back"
            return None

        n = sum(1 for _, l in plan if l)
        ui.list_screen("DAY SLIDESHOW: " + fmt_date(md, weekday=True).upper(), lambda: plan, row, on_open=on_open,
                       on_key=on_key,
                       info_fn=lambda: f"FOUND {n} OF {len(plan)} PERIODS WITH A LESSON THAT DAY. "
                                       "EACH PERIOD'S SLIDE, IN ORDER, IN ONE SLIDESHOW + PDF.",
                       hints="1-0 PICK A LESSON  M MAKE THE SLIDESHOW  ESC CANCEL")

    def curriculum_map(self, course=None):
        """Y in Lesson Plans: a class's units in order with dates, standards, lessons, and assessments."""
        ui, st = self.ui, self.st
        found = [(n, c) for n, c in classes(st, self.store.data["lessons"]) if c]
        if not found:
            ui.msg = "?PLAN A FEW LESSONS FIRST. THE MAP IS BUILT FROM THEIR UNITS, DATES, AND STANDARDS."
            return
        names = [n for n, _ in found]
        j = ui.choose("CURRICULUM MAP FOR WHICH CLASS?",
                      [f"{(n or 'NO CLASS YET').upper()}  ({c} LESSON{'S' if c != 1 else ''})" for n, c in found] +
                      ["ALL MY CLASSES"], names.index(course) if course in names else 0)
        if j is None:
            return
        course = names[j] if j < len(names) else None
        subject = st.get("subject") if st.get("subject") in self.store.subjects else ""
        grades = st.get("grades", "")
        years = school_years(self.store.data["lessons"])
        this = school_year(datetime.date.today())
        year = this if this in years else (years[0] if years else None)
        while True:
            opts = ["PREVIEW", "SAVE: PDF + SPREADSHEET (CSV)", "SAVE: WORD + SPREADSHEET (CSV)",
                    "SAVE: PDF + WORD + SPREADSHEET",
                    "NOT-TAUGHT-YET LIST: " + (f"{subject} {grade_label(grades)}".upper() if subject else "OFF"),
                    "SCHOOL YEAR: " + (school_year_label(year) if year else "EVERY YEAR")]
            k = ui.choose("CURRICULUM MAP: " + ("ALL MY CLASSES" if course is None else (course or "NO CLASS YET")
                                                ).upper(), opts, 0)
            if k is None:
                return
            if k == 0:
                self.preview(curriculum_doc(self.store, names[0] if course is None else course, subject, grades, year))
            elif k == 5:
                m = ui.choose("WHICH SCHOOL YEAR?", [school_year_label(y) for y in years] + ["EVERY YEAR"],
                              years.index(year) if year in years else len(years))
                if m is not None:
                    year = years[m] if m < len(years) else None
            elif k == 4:
                subs = self.store.subjects
                if not subs:
                    ui.msg = "?IMPORT STANDARDS FIRST (MAIN MENU > STANDARDS LIBRARY > I)."
                    continue
                m = ui.choose("LIST WHAT ISN'T TAUGHT YET FROM", ["(LEAVE THE LIST OFF)"] + [x.upper() for x in subs],
                              subs.index(subject) + 1 if subject in subs else 0)
                if m is not None:
                    subject = subs[m - 1] if m else ""
                if subject:
                    g = self.pick_grade(grades)
                    grades = g if g is not None else grades
            else:
                fmts = [["PDF", "CSV"], ["DOCX", "CSV"], ["PDF", "DOCX", "CSV"]][k - 1]
                try:
                    files = export_map(self.store, course, fmts, subject, grades, year=year)
                except ExportError as e:
                    ui.msg = f"?{e}"
                    return
                self.export_done(files)
                return

    def export_now(self, lesson, fmt="PNG"):
        files = self.do_export("lesson", lesson, fmt)
        if files:
            self.export_done(files)

    def do_export(self, kind, obj, fmt):
        try:
            files = export(self.store, kind, obj, fmt, progress=ExportBar(self.ui, self.st.get("mascot")))
            if plugins.problems:
                self.ui.msg = "?" + "  ".join(plugins.take_problems())
            return files
        except ExportError as e:
            self.ui.msg = f"?{e}"
            return None

    def export_done(self, files):
        ui = self.ui
        folder = files_folder(files)

        def on_open(path, i):
            ui.msg = "OPENING..." if open_path(path) else "?COULD NOT OPEN FILE"

        def on_key(k, path, i):
            if ch(k).lower() == "f":
                ui.msg = "OPENING FOLDER..." if open_path(folder) else "?COULD NOT OPEN FOLDER"
            return None

        ui.list_screen("EXPORT COMPLETE", lambda: files, lambda p, w: (os.path.relpath(p, folder), ui.HI),
                       on_open=on_open, on_key=on_key,
                       info_fn=lambda: f"SAVED {len(files)} FILE{'S' if len(files) != 1 else ''} TO {folder}",
                       hints="1-0 OPEN FILE  F OPEN FOLDER  ESC DONE")

    def board_settings(self):
        """The board designer: fonts, where things go, and which sections show."""
        ui, st = self.ui, self.st
        cycle = {k: list(v) for k, v in BOARD_CHOICES.items()}

        def choice(key):
            return BOARD_CHOICES[key].get(st.get(key), next(iter(BOARD_CHOICES[key].values()))).upper()

        def fields():
            head = st.get("board_head_font", "")
            if not st.get("advanced"):
                return [("style", f"COLORS .............. {BOARD_STYLES[st['board_style']]}"),
                        ("sections", "SECTIONS ..."),
                        ("preview", "PREVIEW A SLIDE")]
            return [("style", f"COLORS .............. {BOARD_STYLES[st['board_style']]}"),
                    ("font", f"TEXT FONT ........... {fontlib.label(st.get('board_font', '')).upper()}"),
                    ("head", f"HEADING FONT ........ {(fontlib.label(head) if head else 'SAME AS TEXT').upper()}"),
                    ("board_layout", f"COLUMNS ............. {choice('board_layout')}"),
                    ("board_panels", f"SECTION BOXES ....... {choice('board_panels')}"),
                    ("board_title_align", f"TITLE ............... {choice('board_title_align')}"),
                    ("board_codes_place", f"CLASS CODES ......... {choice('board_codes_place')}"),
                    ("big", "TEXT SIZE ........... " + ("EXTRA BIG (MAY USE MORE SLIDES)" if st.get("board_big_text")
                                                         else "BIGGEST THAT FITS")),
                    ("sections", "SECTIONS, SIDES & ORDER..."),
                    ("preview", "PREVIEW A SLIDE"),
                    ("reset", "RESET THE BOARD DESIGN TO DEFAULT")]

        def on_open(f, i):
            key = f[0]
            if key == "style":
                order = list(BOARD_STYLES)
                st["board_style"] = order[(order.index(st["board_style"]) + 1) % len(order)]
            elif key in ("font", "head"):
                self.font_picker("board_font" if key == "font" else "board_head_font")
            elif key in cycle:
                order = cycle[key]
                cur = st.get(key) if st.get(key) in order else order[0]
                st[key] = order[(order.index(cur) + 1) % len(order)]
            elif key == "big":
                st["board_big_text"] = not st.get("board_big_text")
            elif key == "reset":
                if not ui.confirm("PUT FONTS, COLUMNS, BOXES, AND SECTION PLACES BACK TO THE USUAL"):
                    return i
                reset_advanced(st, "board")
                ui.msg = "THE BOARD DESIGN IS BACK TO THE USUAL ONE."
            elif key == "sections":
                self.board_sections()
            elif key == "preview":
                self.board_preview()
                return i
            self.save()
            return i

        def on_key(k, item, i):
            if ch(k).upper() == "P":
                self.board_preview()

        ui.list_screen("BOARD DESIGNER" if st.get("advanced") else "BOARD SLIDES", fields, lambda f, w: (f[1], ui.HI),
                       on_open=on_open, on_key=on_key,
                       info_fn=lambda: "1920x1080 PNG + SLIDESHOW. EMPTY SECTIONS ARE LEFT OFF; TEXT SIZES ITSELF." + (
                           "" if st.get("advanced") else " FONTS + LAYOUT: SETTINGS > ADVANCED MODE."),
                       hints="1-0 CHANGE  P PREVIEW  ESC BACK")

    def board_sections(self):
        ui, st = self.ui, self.st

        def fields():
            on = st.get("board_sections") or []
            f = [("std_text", "STANDARDS SHOW ...... " + ("CODE + FULL TEXT" if st.get("board_std_text", True)
                                                          else "CODES ONLY"))]
            for key, label, col in board_sections(st):
                f.append((key, f"[{'X' if key in on else ' '}] {label.upper():<19} ({BOARD_SIDES[col].upper()})"))
            return f

        def on_open(f, i):
            key = f[0]
            if key == "std_text":
                st["board_std_text"] = not st.get("board_std_text", True)
            else:
                on = [k for k in st.get("board_sections") or [] if k != key]
                st["board_sections"] = on if len(on) < len(st.get("board_sections") or []) else on + [key]
            self.save()
            return i

        def on_key(k, item, i):
            c = ch(k).upper()
            if not item or item[0] == "std_text":
                return None
            key = item[0]
            side = dict((x[0], x[2]) for x in board_sections(st)).get(key)
            if c == "S" and side in ("left", "right"):
                st.setdefault("board_sides", {})[key] = "right" if side == "left" else "left"
                self.save()
                return i
            if c in ("+", "-", "=", "_") and side != "top":
                if move_board_section(st, key, 1 if c in ("+", "=") else -1):
                    self.save()
                    keys = [x[0] for x in fields()]
                    return keys.index(key)
            if c == "R" and ui.confirm("PUT EVERY SECTION BACK IN ITS USUAL PLACE"):
                st["board_sides"], st["board_order"] = {}, []
                self.save()
            return None

        ui.list_screen("BOARD SECTIONS", fields, lambda f, w: (f[1], ui.HI), on_open=on_open, on_key=on_key,
                       info_fn=lambda: "COLUMNS: " + BOARD_CHOICES["board_layout"].get(
                           st.get("board_layout", "auto"), "").upper() + " (BOARD DESIGNER > COLUMNS)",
                       hints="RETURN ON/OFF  S SIDE  +/- MOVE  R RESET  ESC BACK")

    def font_picker(self, key):
        """Pick a board font: the standard ones or any font on this computer."""
        ui, st = self.ui, self.st
        rows, _, x0 = ui.dims()
        ui.draw(rows - 1, x0, ui.tx("LOOKING FOR FONTS..."), ui.HI)
        ui.s.refresh()
        top = [("", "SAME AS TEXT FONT" if key == "board_head_font" else fontlib.label("").upper(), "")]
        tags = {"standard": "", "system": ""}
        everything = top + [(n, n, tags[k]) for n, k in fontlib.families()
                            if not (n == "Helvetica" and key == "board_font")]  # (the standard one, above)
        query = [""]

        def items():
            q = query[0].lower()
            return [x for x in everything if not q or q in x[1].lower()]

        def row(x, w):
            mark = "* " if x[0] == st.get(key, "") else "  "
            return mark + x[1] + x[2], None

        def on_open(x, i):
            st[key] = x[0]
            self.save()
            ui.msg = "FONT: " + (x[1] if x[1] else "STANDARD").upper()
            return "back"

        def on_key(k, x, i):
            c = ch(k)
            if k in (curses.KEY_BACKSPACE, 127, 8) or c in ("\x7f", "\b"):
                query[0] = query[0][:-1]
                return 0
            if len(c) == 1 and c.isprintable() and not c.isdigit():
                query[0] += c
                return 0
            return None

        cur = st.get(key, "")
        start = next((i for i, x in enumerate(everything) if x[0] == cur), 0)
        ui.list_screen("BOARD FONT", items, row, on_open=on_open, on_key=on_key, start=start,
                       info_fn=lambda: (f"FIND: {query[0]}_" if query[0] else
                                        f"{len(everything)} FONTS. TYPE TO FIND ONE. * = THE ONE IN USE"),
                       hints="1-0 PICK  TYPE TO FIND  ESC CANCEL", empty="NO FONT MATCHES. BACKSPACE TO ERASE.")

    def board_preview(self):
        """Render the first board slide of the newest lesson (or a sample) and open it."""
        ui = self.ui
        rows, _, x0 = ui.dims()
        ui.draw(rows - 1, x0, ui.tx("DRAWING A PREVIEW..."), ui.HI)
        ui.s.refresh()
        path = os.path.join(self.store.dir, "board-preview.png")
        try:
            n = render_preview(preview_doc(self.store), path)
        except Exception as e:  # noqa: BLE001 - say what went wrong, never crash the planner
            ui.msg = "?" + str(e).upper()
            return
        open_path(path)
        ui.msg = "PREVIEW OPENED" + (f" (SLIDE 1 OF {n})" if n > 1 else "") + "."

    # ------------------------------------------------------------- settings
    def logo_settings(self):
        ui, st = self.ui, self.st
        has = self.store.logo() is not None
        opts = ["CHOOSE A PNG OR JPEG PICTURE"] + (
            [f"PUT IT {LOGO_PLACES['right' if st.get('logo_place', 'left') == 'left' else 'left']}", "REMOVE THE LOGO"]
            if has else [])
        j = ui.choose("SCHOOL LOGO (CORNER OF EVERY BOARD SLIDE)", opts)
        if j == 0:
            path = ui.prompt("PATH TO THE LOGO, A .PNG OR .JPG (DRAG IT HERE)", raw=True)
            if not path:
                return
            rows, _, x0 = ui.dims()
            ui.draw(rows - 1, x0, ui.tx("READING THE PICTURE..."), ui.HI)
            ui.s.refresh()
            try:
                img = self.store.set_logo(path)
            except (ValueError, OSError) as e:
                ui.msg = "?" + str(e).upper()
                return
            ui.msg = f"LOGO SET ({img['w']}x{img['h']}). IT SHOWS {LOGO_PLACES[st.get('logo_place', 'left')]}."
        elif j == 1:
            st["logo_place"] = "right" if st.get("logo_place", "left") == "left" else "left"
        elif j == 2 and ui.confirm("TAKE THE LOGO OFF YOUR BOARD SLIDES"):
            self.store.remove_logo()

    def class_periods(self):
        """Each period's class codes go on its own copy of a lesson's board slide."""
        ui, st = self.ui, self.st
        periods = st.setdefault("class_periods", [])

        def row(p, w):
            codes = " / ".join(x.strip() for x in (p.get("codes") or "").split("\n") if x.strip())
            return f"{p.get('name', ''):<14} {(p.get('course') or 'ANY COURSE'):<14} {codes or '(NO CODES)'}", ui.HI

        def on_open(p, i):
            self.period_editor(p)
            return i

        def on_key(k, p, i):
            c = ch(k).lower()
            if c == "n":
                p = {"name": f"Period {len(periods) + 1}", "course": "", "codes": ""}
                periods.append(p)
                if "class_codes" not in (st.get("board_sections") or []):
                    st.setdefault("board_sections", []).append("class_codes")
                self.period_editor(p)
                self.save()
                return len(periods) - 1
            if c == "d" and p is not None and ui.confirm(f"REMOVE {p.get('name') or 'THIS PERIOD'}"):
                periods.remove(p)
                self.save()
                return max(0, i - 1)
            if c in "+-" and p is not None:
                j = i + (1 if c == "+" else -1)
                if 0 <= j < len(periods):
                    periods[i], periods[j] = periods[j], periods[i]
                    self.save()
                    return j
            return None

        ui.list_screen("CLASS PERIODS & CODES", lambda: periods, row, on_open=on_open, on_key=on_key,
                       info_fn=lambda: "ONE BOARD SLIDE PER PERIOD, EACH WITH ITS OWN CODES",
                       hints="1-0 EDIT  N NEW  D REMOVE  +/- MOVE  ESC BACK",
                       empty="NO PERIODS YET. N ADDS ONE (E.G. PERIOD 1, WITH ITS GOOGLE CLASSROOM CODE).")

    def period_editor(self, p):
        ui = self.ui
        fields = [("name", "NAME", "line"), ("course", "COURSE (BLANK = EVERY LESSON)", "line"),
                  ("codes", "CODES", "text")]

        def row(f, w):
            key, label, kind = f
            v = p.get(key) or ""
            if kind == "text":
                v = " / ".join(x.strip() for x in v.split("\n") if x.strip())
            return f"{label:<30} {v or '-'}", ui.HI

        def on_open(f, i):
            key, label, kind = f
            if kind == "text":
                v = ui.edit_text(f"CODES FOR {p.get('name') or 'THIS PERIOD'}", p.get(key, ""),
                                 "ONE PER LINE, NAME: CODE, E.G. GOOGLE CLASSROOM: ABC123. ESC SAVES.")
            else:
                v = ui.prompt(label, p.get(key, ""))
            if v is not None:
                p[key] = v.strip()
                self.save()
            return i

        ui.list_screen(f"CLASS PERIOD: {p.get('name') or ''}", lambda: fields, row, on_open=on_open,
                       info_fn=lambda: "A COURSE MAKES THIS SLIDE ONLY FOR THAT COURSE'S LESSONS",
                       hints="1-0 CHANGE  ESC BACK")

    def backup_now(self):
        ui = self.ui
        folder = ui.prompt("SAVE A BACKUP IN WHICH FOLDER (DRAG ONE HERE)", self.store.backup_dir(), raw=True)
        if not folder:
            return
        self.save()
        try:
            path = self.store.backup(folder)
        except OSError as e:
            ui.msg = f"?COULD NOT BACK UP: {e}"
            return
        ui.msg = f"BACKED UP TO {path}"

    def export_everything(self):
        ui = self.ui
        parent = ui.prompt("PUT THE NEW FOLDER WHERE (DRAG A FOLDER HERE)", self.store.export_dir(), raw=True)
        if not parent:
            return
        self.save()
        try:
            folder, files, problems = export_everything(self.store, parent,
                                                        progress=ExportBar(self.ui, self.st.get("mascot")))
        except ExportError as e:
            ui.msg = f"?{e}"
            return
        ui.msg = f"EXPORTED {len(files)} FILES TO {folder}" + (f"  ({len(problems)} DIDN'T: {problems[0]})" if problems else "")
        if ui.confirm("OPEN THE FOLDER NOW"):
            open_path(folder)

    def import_backup(self):
        ui = self.ui
        path = ui.prompt("PATH TO A CHALKBOARD BACKUP .JSON FILE (DRAG IT HERE)", raw=True)
        if not path:
            return
        j = ui.choose("IMPORT HOW", ["ADD WHAT I DON'T HAVE (KEEPS MY WORK)",
                                     "REPLACE EVERYTHING WITH THE BACKUP"])
        if j is None or (j == 1 and not ui.confirm("REPLACE ALL YOUR LESSONS, ASSESSMENTS, AND SETTINGS")):
            return
        self.save()
        try:
            got = self.store.import_backup(path, replace=j == 1)
        except (ValueError, OSError) as e:
            ui.msg = "?" + str(e).upper()
            return
        self._std_subject = None
        ui.apply_theme()
        if j == 1:
            ui.msg = f"REPLACED WITH THE BACKUP: {got['lessons']} LESSONS, {got['assessments']} ASSESSMENTS."
        else:
            ui.msg = (f"ADDED {got['lessons']} LESSONS, {got['assessments']} ASSESSMENTS, "
                      f"{got['subjects']} SUBJECTS, {got['standards']} CUSTOM STANDARDS; {got['updated']} UPDATED.")
        ui.msg += f" YOUR OLD WORK IS SAVED IN {got['safety']}"

    def pick_mascot(self):
        """Browse the pixel-art mascots with the arrow keys; ENTER picks one."""
        ui, st = self.ui, self.st
        keys = [None] + list(MASCOTS)
        i = keys.index(st.get("mascot")) if st.get("mascot") in MASCOTS else 0
        while True:
            rows, w, x0 = ui.dims()
            key = keys[i]
            ui.begin()
            ui.header("SCHOOL MASCOT")
            ui.line(2, "SHOWS UP IN THE BOOT SEQUENCE AND RUNS ALONG THE EXPORT PROGRESS BAR", ui.DIM)
            y = 4
            mx = x0 + 5
            ui.put_sprite(y, mx, sprite(key))
            ui.put(y + 3, mx - 4, "<", ui.HI)
            ui.put(y + 3, mx + 19, ">", ui.HI)
            label = f"GO {mascot_name(key)}!" if key else "NONE (THE CHALKBOARD LOGO RUNS THE EXPORT BAR)"
            ui.line(y + 10, label, ui.HI)
            ui.line(y + 12, f"{i + 1} OF {len(keys)}", ui.DIM)
            ui.footer("LEFT/RIGHT BROWSE   ENTER PICK   ESC CANCEL")
            ui.show()
            k = ui.key()
            if k == curses.KEY_LEFT:
                i = (i - 1) % len(keys)
            elif k in (curses.KEY_RIGHT, " "):
                i = (i + 1) % len(keys)
            elif k in ("\n", "\r", curses.KEY_ENTER):
                st["mascot"] = key or ""
                ui.msg = f"MASCOT SET: {mascot_name(key)}" if key else "NO MASCOT"
                return
            elif k in BACK:
                return

    def page_layouts(self):
        """Advanced Mode > Page Layouts: pick a kind of page."""
        ui, st = self.ui, self.st
        kinds = list(PAGE_LAYOUTS)

        def row(k, w):
            changed = " (CHANGED)" if (st.get("page_layouts") or {}).get(k) else ""
            return PAGE_LAYOUTS[k][0].upper() + changed + " ...", ui.HI

        def on_open(k, i):
            self.page_layout(k)
            return i

        def on_key(k, item, i):
            if ch(k).upper() == "R" and advanced_changes(st) and ui.confirm(
                    "PUT EVERY PAGE LAYOUT AND THE BOARD DESIGN BACK TO THE USUAL"):
                reset_advanced(st)
                self.save()
                ui.msg = "EVERYTHING IS BACK TO THE USUAL LAYOUT."
                return i
            return None

        ui.list_screen("PAGE LAYOUTS", lambda: kinds, row, on_open=on_open, on_key=on_key,
                       info_fn=lambda: "NAME LINES, TITLES, HEADINGS, AND FOOTERS FOR EACH KIND OF PAGE.",
                       hints="1-0 CHANGE  R RESET EVERYTHING  ESC BACK")

    def page_layout(self, kind):
        """One kind of page's layout options."""
        ui, st = self.ui, self.st
        name, options = PAGE_LAYOUTS[kind]

        def lay():
            return page_layout(st, kind)

        def row(opt, w):
            key, label, typ, _ = opt
            v = lay()[key]
            if typ == "bool":
                v = "YES" if v else "NO"
            elif typ in LAYOUT_CHOICES:
                v = LAYOUT_CHOICES[typ][v].upper()
            elif typ == "num":
                v = str(v) if v else "NONE"
            elif typ == "text":
                v = first_line(v) or "(THE USUAL)"
            else:
                v = v or ("(NONE)" if typ == "fields" else "(THE USUAL)" if key == "footer_text" else "(NONE)")
            return f"{label.upper()[:44]:<45} {v}", ui.HI

        def change(key, value):
            values = lay()
            values[key] = value
            set_page_layout(st, kind, values)
            self.save()

        def on_open(opt, i):
            key, label, typ, _ = opt
            cur = lay()[key]
            if typ == "bool":
                change(key, not cur)
            elif typ in LAYOUT_CHOICES:
                order = list(LAYOUT_CHOICES[typ])
                change(key, order[(order.index(cur) + 1) % len(order)])
            elif typ == "num":
                v = ui.prompt(label.upper() + " (0-12)", str(cur))
                if v is not None and v.strip().isdigit():
                    change(key, min(12, int(v)))
            elif typ == "text":
                v = ui.edit_text(label.upper(), cur, "BLANK KEEPS THE USUAL NOTE. ESC SAVES.")
                if v is not None:
                    change(key, v.strip())
            else:
                hint = " (COMMAS BETWEEN, BLANK = NONE)" if typ == "fields" else ""
                v = ui.prompt(label.upper() + hint, cur)
                if v is not None:
                    change(key, v.strip())
            return i

        def on_key(k, opt, i):
            c = ch(k).upper()
            if c == "P":
                self.preview(layout_sample(self.store, kind, st))
            elif c == "R" and ui.confirm(f"PUT THE {name.upper()} LAYOUT BACK TO THE USUAL"):
                reset_advanced(st, kind)
                self.save()
                ui.msg = f"{name.upper()}: BACK TO THE USUAL LAYOUT."
                return i
            return None

        ui.list_screen("PAGE LAYOUT: " + name.upper(), lambda: options, row, on_open=on_open, on_key=on_key,
                       info_fn=lambda: "EVERY EXPORT (PDF, WORD, TEXT) FOLLOWS THIS LAYOUT.",
                       hints="1-0 CHANGE  P PREVIEW  R RESET TO DEFAULT  ESC BACK")

    def settings_screen(self):
        ui, st = self.ui, self.st
        everything = [
            ("teacher", "TEACHER NAME (FOR FOOTERS)", "line"),
            ("school", "SCHOOL", "line"),
            ("course", "DEFAULT COURSE", "line"),
            ("subject", "DEFAULT SUBJECT (STANDARDS)", "subject"),
            ("grades", "DEFAULT GRADE BAND", "grades"),
            ("default_materials", "DEFAULT MATERIALS NEEDED", "text"),
            ("lesson_hide", "LESSON SECTIONS", "sections"),
            ("font", "DOCUMENT FONT", "font"),
            ("page", "PAPER SIZE", "page"),
            ("export_dir", "EXPORT FOLDER", "folder"),
            ("board_style", "BOARD SLIDE DESIGNER", "board"),
            ("logo", "SCHOOL LOGO (BOARD SLIDES)", "logo"),
            ("class_periods", "CLASS PERIODS & CODES", "periods"),
            ("primary_color", "SCHOOL COLOR 1 (BACKGROUND)", "color"),
            ("secondary_color", "SCHOOL COLOR 2 (HEADINGS)", "color"),
            ("text_color", "SCHOOL COLOR 3 (TEXT)", "color"),
            ("theme", "SCREEN COLOR", "theme"),
            ("uppercase", "ALL-CAPS MENUS", "bool"),
            ("boot", "BOOT SEQUENCE", "bool"),
            ("mascot", "SCHOOL MASCOT (BOOT & EXPORT)", "mascot"),
            ("backup_dir", "BACK UP EVERYTHING NOW", "backup"),
            ("", "EXPORT EVERYTHING (PDF + WORD)", "everything"),
            ("", "IMPORT A BACKUP", "restore"),
            ("advanced", "ADVANCED MODE", "advanced"),
            ("page_layouts", "  PAGE LAYOUTS", "layouts"),
        ]

        def fields():
            return [f for f in everything if f[2] != "layouts" or st.get("advanced")]

        def row(f, w):
            key, label, kind = f
            v = st.get(key)
            if kind == "bool":
                v = "ON" if v else "OFF"
            elif kind == "folder":
                v = self.store.export_dir()
            elif kind == "logo":
                img = self.store.logo()
                v = (f"{img.get('name') or 'LOGO'} ({img['w']}x{img['h']}), {LOGO_PLACES[st.get('logo_place', 'left')]}"
                     if img else "NONE")
            elif kind == "mascot":
                v = mascot_name(v) + " ..." if v in MASCOTS else "NONE ..."
            elif kind == "periods":
                n = len([p for p in st.get("class_periods") or [] if (p.get("codes") or "").strip()])
                v = f"{n} WITH CODES: ONE BOARD SLIDE EACH ..." if n else "NONE ..."
            elif kind == "backup":
                v = self.store.backup_dir() + (f"  (LAST: {st['last_backup']})" if st.get("last_backup") else "")
            elif kind == "restore":
                v = "FROM A BACKUP FILE OR ANOTHER COMPUTER'S DATA.JSON"
            elif kind == "everything":
                v = "A FOLDER TO KEEP OR DRAG INTO GOOGLE DRIVE ..."
            elif kind == "board":
                v = BOARD_STYLES.get(v, "") + " ..."
            elif kind == "advanced":
                changed = advanced_changes(st)
                v = ("ON" if v else "OFF: NAME LINES, HEADINGS, FONTS ...") + (
                    f"  ({len(changed)} CHANGED, STILL USED)" if changed and not v else "")
            elif kind == "layouts":
                n = len(st.get("page_layouts") or {})
                v = f"{n} CHANGED ..." if n else "ALL THE USUAL ..."
            elif kind == "sections":
                n = len([k for k in v or [] if k not in FIXED_FIELDS])
                v = f"{n} HIDDEN ..." if n else "ALL SHOWN ..."
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
            elif kind == "advanced":
                st[key] = not st.get(key)
                ui.msg = ("ADVANCED MODE ON: PAGE LAYOUTS + MORE IN THE BOARD SLIDE DESIGNER" if st[key] else
                          "ADVANCED MODE OFF. ANYTHING YOU CHANGED STILL APPLIES.")
            elif kind == "layouts":
                self.page_layouts()
            elif kind == "board":
                self.board_settings()
            elif kind == "sections":
                self.lesson_sections()
            elif kind == "logo":
                self.logo_settings()
            elif kind == "periods":
                self.class_periods()
            elif kind == "mascot":
                self.pick_mascot()
            elif kind == "backup":
                self.backup_now()
                return i
            elif kind == "restore":
                self.import_backup()
                return i
            elif kind == "everything":
                self.export_everything()
                return i
            self.save()
            return i

        ui.list_screen("SETTINGS", fields, row, on_open=on_open,
                       info_fn=lambda: f"DATA FILE: {self.store.path}",
                       hints="1-0 CHANGE  ESC BACK")


def main():
    ap = argparse.ArgumentParser(prog="chalkboard", description="Chalkboard -- retro lesson planner & assessment builder")
    ap.add_argument("--no-boot", action="store_true", help="skip the boot sequence")
    ap.add_argument("--no-plugins", action="store_true", help="start without plugins")
    ap.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    args = ap.parse_args()
    offline.enforce()  # before anything else runs, plugins included
    os.environ.setdefault("ESCDELAY", "25")
    try:
        curses.wrapper(lambda s: App(s, args).run())
    except KeyboardInterrupt:
        pass
    print("]BYE.")
