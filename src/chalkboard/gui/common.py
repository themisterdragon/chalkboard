"""Pieces the screens share: toolbars, filterable item lists, autosaving fields, the standards field."""

import copy
import tkinter as tk

from ..store import SORTS, now, sort_items
from . import widgets as W

SORT_LABEL = {"updated": "Date modified", "created": "Date created", "title": "Title (A-Z)", "unit": "Unit"}
assert set(SORT_LABEL) == set(SORTS)


def toolbar(parent, skin):
    S = skin.S
    bar = tk.Frame(parent, bg=skin["window"], padx=8 * S, pady=6 * S)
    bar.pack(side="top", fill="x")
    return bar


def tool(bar, skin, text, command, side="left", default=False):
    b = W.Button(bar, skin, text, command, default=default)
    b.pack(side=side, padx=(0, 6 * skin.S) if side == "left" else (6 * skin.S, 0))
    return b


def gap(bar, skin):
    tk.Frame(bar, bg=bar["bg"], width=10 * skin.S).pack(side="left")


def touch(gui, obj):
    obj["updated"] = now()
    gui.save_soon()


class LineField:
    """A one-line entry bound to obj[key]; saves as you type."""

    def __init__(self, parent, gui, obj, key, width=30, on_change=None):
        self.var = tk.StringVar(value=str(obj.get(key) or ""))
        self.entry = W.entry(parent, gui.skin, self.var, width=width)

        def changed(*a):
            if obj.get(key, "") != self.var.get():
                obj[key] = self.var.get()
                touch(gui, obj)
                if on_change:
                    on_change(self.var.get())
        self.var.trace_add("write", changed)


class AutoText:
    """A text box bound to obj[key] (or get/set functions) that grows with what's in it."""

    def __init__(self, parent, gui, obj, key, min_lines=2, max_lines=16, width=60, get=None, set=None):
        self.gui, self.obj, self.key = gui, obj, key
        self.get_fn = get or (lambda: obj.get(key) or "")
        self.set_fn = set or (lambda v: obj.__setitem__(key, v))
        self.min, self.max = min_lines, max_lines
        self.text = t = W.textbox(parent, gui.skin, height=min_lines, width=width)
        t.insert("1.0", self.get_fn())
        t.edit_reset()
        t.edit_modified(False)
        t.bind("<<Modified>>", self.modified)
        t.bind("<Configure>", lambda e: self.grow())
        t.bind("<Tab>", lambda e: (t.tk_focusNext().focus_set(), "break")[1])
        t.bind("<Shift-Tab>", lambda e: (t.tk_focusPrev().focus_set(), "break")[1])
        t.bind("<ISO_Left_Tab>", lambda e: (t.tk_focusPrev().focus_set(), "break")[1])

    def modified(self, e=None):
        t = self.text
        if not t.edit_modified():
            return
        t.edit_modified(False)
        self.grow()
        v = t.get("1.0", "end-1c")
        if v != self.get_fn():
            self.set_fn(v)
            touch(self.gui, self.obj)

    def grow(self):
        t = self.text
        try:
            n = t.count("1.0", "end", "displaylines")[0]
        except (tk.TclError, TypeError):
            return
        n = max(self.min, min(self.max, n))
        if int(t.cget("height")) != n:
            t.configure(height=n)

    def replace(self, value):
        t = self.text
        t.delete("1.0", "end")
        t.insert("1.0", value)
        self.modified()


class StandardsField(tk.Frame):
    """The standards on a lesson or assessment: code + text rows, a Choose… button, and add-by-code."""

    def __init__(self, parent, gui, obj, key="standards", grades=None):
        sk, S = gui.skin, gui.skin.S
        super().__init__(parent, bg=W.bg_of(parent))
        self.gui, self.obj, self.key = gui, obj, key
        self.grades = grades or (lambda: obj.get("grades") or gui.settings.get("grades"))
        bar = tk.Frame(self, bg=self["bg"])
        bar.pack(fill="x")
        W.Button(bar, sk, "Choose Standards…", self.choose).pack(side="left", padx=(0, 12 * S))
        bar = tk.Frame(bar, bg=self["bg"])
        bar.pack(side="left")
        W.label(bar, sk, "or add a code:").pack(side="left")
        self.code = tk.StringVar()
        e = W.entry(bar, sk, self.code, width=16)
        e.pack(side="left", padx=(6 * S, 6 * S))
        e.bind("<Return>", lambda ev: (self.add_code(), "break")[1])
        W.Button(bar, sk, "Add", self.add_code, small=True).pack(side="left")
        self.rows = tk.Frame(self, bg=self["bg"])
        self.rows.pack(fill="x", pady=(6 * S, 0))
        self.rows.bind("<Configure>", lambda e: self.rewrap())
        self.draw()

    def codes(self):
        return self.obj.setdefault(self.key, [])

    def draw(self):
        sk, S = self.gui.skin, self.gui.skin.S
        for w in self.rows.winfo_children():
            w.destroy()
        self.texts = []
        codes = self.codes()
        if not codes:
            W.label(self.rows, sk, "None yet.", dim=True).grid(row=0, column=0, sticky="w")
        for i, code in enumerate(codes):
            W.label(self.rows, sk, code, bold=True).grid(row=i, column=0, sticky="nw", pady=S, padx=(0, 10 * S))
            t = W.label(self.rows, sk, self.gui.store.std_text(code) or "(not in the standards library)",
                        wrap=500 * S)
            t.grid(row=i, column=1, sticky="nw", pady=S)
            self.texts.append(t)
            W.Button(self.rows, sk, "Remove", lambda c=code: self.remove(c), small=True).grid(
                row=i, column=2, sticky="ne", pady=S, padx=(10 * S, 0))
        self.rows.columnconfigure(1, weight=1)
        self.after_idle(self.rewrap)

    def rewrap(self):
        if not self.winfo_exists():
            return
        w = self.rows.winfo_width() - 260 * self.gui.skin.S
        for t in getattr(self, "texts", []):
            t.configure(wraplength=max(200, w))

    def changed(self):
        touch(self.gui, self.obj)
        self.draw()

    def remove(self, code):
        self.obj[self.key] = [c for c in self.codes() if c != code]
        self.changed()

    def add_code(self):
        raw = self.code.get().strip()
        if not raw:
            return
        idx = self.gui.store.std_index
        code = raw if raw in idx else next((c for c in idx if c.lower() == raw.lower()), None)
        if not code:
            self.gui.status(f"No standard “{raw}” in the library. Check the code (like RL.9-10.1).")
            return
        if code not in self.codes():
            self.codes().append(code)
            self.changed()
        self.code.set("")

    def choose(self):
        from .standards import pick_standards
        sel = pick_standards(self.gui, self.codes(), self.grades())
        if sel is not None and sel != self.codes():
            self.obj[self.key] = sel
            self.changed()


class ItemList:
    """A list window for lessons or assessments: toolbar, search/unit/sort filters, and the list."""

    kind = "lesson"           # set by subclasses
    pool_key = "lessons"
    sort_key = "lesson_sort"
    noun = "lesson"
    columns = []
    search_fields = ("title", "unit", "course", "date", "standards")

    def __init__(self, gui, parent):
        self.gui = gui
        sk, S = gui.skin, gui.skin.S
        self.frame = f = tk.Frame(parent, bg=sk["window"])
        f.pack(fill="both", expand=True)
        bar = toolbar(f, sk)
        self.need_sel = []
        self.build_tools(bar)

        filt = tk.Frame(f, bg=sk["window"], padx=8 * S)
        filt.pack(fill="x", pady=(0, 6 * S))
        W.label(filt, sk, "Search:").pack(side="left")
        self.q = tk.StringVar()
        self.q.trace_add("write", lambda *a: self.refresh())
        self.search = W.entry(filt, sk, self.q, width=22)
        self.search.pack(side="left", padx=(6 * S, 14 * S))
        W.label(filt, sk, "Unit:").pack(side="left")
        self.unit = tk.StringVar(value="")
        self.unit_dd = W.Dropdown(filt, sk, [("", "All units")], self.unit, lambda v: self.refresh(), width=14)
        self.unit_dd.pack(side="left", padx=(6 * S, 14 * S))
        self.build_filters(filt)
        W.label(filt, sk, "Sort by:").pack(side="left")
        self.sort = tk.StringVar(value=gui.settings.get(self.sort_key, "updated"))
        W.Dropdown(filt, sk, list(SORT_LABEL.items()), self.sort, self.set_sort, width=14).pack(
            side="left", padx=(6 * S, 0))

        self.list = W.ListView(f, sk, self.columns, height=14)
        self.list.pack(fill="both", expand=True, padx=8 * S, pady=(0, 8 * S))
        self.list.on_open(self.cmd_open)
        self.list.on_select(self.update_tools)
        self.list.on_delete(self.cmd_delete)
        self.list.on_menu(self.popup)
        self.search.bind("<Down>", lambda e: self.list.focus())
        self.search.bind("<Return>", lambda e: self.list.focus())
        self.refresh()
        self.list.tv.after_idle(self.list.focus)

    # subclasses fill these in
    def build_tools(self, bar):
        pass

    def build_filters(self, filt):
        pass

    def row(self, x):
        return []

    def match(self, x):
        return True

    def menu_items(self):
        return []

    @property
    def pool(self):
        return self.gui.store.data[self.pool_key]

    def needs(self, b):
        self.need_sel.append(b)
        return b

    def set_sort(self, v):
        self.gui.settings[self.sort_key] = v
        self.gui.save()
        self.refresh()

    def items(self):
        xs = sort_items(self.pool, self.sort.get())
        unit = self.unit.get()
        if unit:
            xs = [x for x in xs if (x.get("unit") or "").strip() == unit]
        q = self.q.get().strip().lower()
        if q:
            xs = [x for x in xs if any(q in str(x.get(k, "")).lower() for k in self.search_fields)]
        return [x for x in xs if self.match(x)]

    def refresh(self, select=None):
        units = self.gui.store.units(self.pool)
        self.unit_dd.set_options([("", "All units")] + [(u, u) for u in units])
        if self.unit.get() and self.unit.get() not in units:
            self.unit.set("")
        xs = self.items()
        empty = (f"Nothing matches. Clear the search or filters to see every {self.noun}." if self.pool else
                 f"No {self.noun}s yet. Click New to make your first one.")
        self.list.set_rows([(x, self.row(x), self.icon()) for x in xs], keep=select, empty_text=empty)
        self.gui.status(f"{len(xs)} of {len(self.pool)} {self.noun}s" if len(xs) != len(self.pool)
                        else f"{len(self.pool)} {self.noun}{'s' if len(self.pool) != 1 else ''}")
        self.update_tools()

    def icon(self):
        return None

    def update_tools(self):
        on = self.selected() is not None
        for b in self.need_sel:
            b.set_enabled(on)

    def selected(self):
        return self.list.selected()

    def reloaded(self):
        self.refresh()

    def escape(self):
        if self.q.get():
            self.q.set("")
            return True
        return False

    def cmd_find(self):
        self.search.focus_set()
        self.search.select_range(0, "end")

    def popup(self, e):
        m = tk.Menu(self.list, tearoff=False)
        W.skin_menu(m, self.gui.skin)
        for label, fn in self.menu_items():
            if label is None:
                m.add_separator()
            else:
                m.add_command(label=label, command=fn)
        try:
            m.tk_popup(e.x_root, e.y_root)
        finally:
            m.grab_release()

    # shared actions
    def cmd_rename(self):
        x = self.selected()
        if not x:
            return
        t = W.ask(self.gui, "Rename", f"New name for “{x.get('title') or 'untitled'}”:", x.get("title") or "")
        if t and t.strip() and t.strip() != x.get("title"):
            x.update(title=t.strip(), updated=now())
            self.gui.save()
            self.refresh(x)

    def cmd_duplicate(self):
        x = self.selected()
        if not x:
            return
        from ..store import new_id
        dup = copy.deepcopy(x)
        dup.update(id=new_id(), title=(x.get("title") or self.noun.title()) + " (copy)", created=now(), updated=now())
        self.pool.append(dup)
        self.gui.save()
        self.refresh(dup)
        self.gui.status("Made a copy. It's selected.")
