"""The standards library window and the standards picker (both built on StandardsBrowser)."""

import tkinter as tk
from tkinter import filedialog

from ..store import ALL, GRADE_CHOICES, grades_match
from . import widgets as W
from .common import tool, toolbar
from .desktop import grade_label

CHECK_ON, CHECK_OFF = "☒", "☐"


def sub_label(parent, label):
    return f"Grade {label}: " if parent.get("sub_grades") else f"{label}. "


class StandardsBrowser(tk.Frame):
    """Subjects on the left; search, grade, and the standards (parts tucked under each) on the right;
    the full text of the highlighted one underneath. With `selected` (a list), rows get check boxes."""

    def __init__(self, parent, gui, grades=None, selected=None, on_change=None):
        sk, S = gui.skin, gui.skin.S
        super().__init__(parent, bg=W.bg_of(parent))
        self.gui, self.selected, self.on_change = gui, selected, on_change
        st = gui.settings
        g = grades or st.get("grades", "9-10")
        self.grade = tk.StringVar(value=g if g in GRADE_CHOICES else ALL)
        subj = getattr(gui, "std_subject", None) or st.get("subject")
        self.subject = subj if subj in gui.store.subjects else ALL
        self.q = tk.StringVar()
        self.pending = None
        self.show_sel = tk.BooleanVar(value=False)

        left = tk.Frame(self, bg=self["bg"])
        left.pack(side="left", fill="y", padx=(0, 8 * S))
        W.label(left, sk, "Subjects", bold=True).pack(anchor="w", pady=(0, 2 * S))
        box = tk.Frame(left, bg=sk["dark"], padx=S, pady=S)
        box.pack(fill="y", expand=True)
        self.subjects = tk.Listbox(box, font=sk.f, bg=sk["field"], fg=sk["text"], selectbackground=sk["sel"],
                                   selectforeground=sk["seltext"], relief="flat", bd=0, highlightthickness=0,
                                   activestyle="none", width=30, exportselection=False)
        self.subjects.pack(fill="y", expand=True)
        self.subjects.bind("<<ListboxSelect>>", lambda e: self.pick_subject())
        self.fill_subjects()

        right = tk.Frame(self, bg=self["bg"])
        right.pack(side="left", fill="both", expand=True)
        bar = tk.Frame(right, bg=self["bg"])
        bar.pack(fill="x", pady=(0, 6 * S))
        W.label(bar, sk, "Search:").pack(side="left")
        self.search = W.entry(bar, sk, self.q, width=24)
        self.search.pack(side="left", padx=(6 * S, 14 * S))
        self.q.trace_add("write", lambda *a: self.soon())
        W.label(bar, sk, "Grade:").pack(side="left")
        W.Dropdown(bar, sk, [(x, grade_label(x)) for x in GRADE_CHOICES], self.grade, lambda v: self.refresh(),
                   width=13).pack(side="left", padx=(6 * S, 14 * S))
        if selected is not None:
            self.show_sel.trace_add("write", lambda *a: self.refresh())
            W.Check(bar, sk, "Show only checked", self.show_sel).pack(side="left")
        cols = [("code", "Code", 150, False), ("grades", "Grades", 80, False), ("text", "Standard", 520, True)]
        self.list = W.ListView(right, sk, cols, height=12, tree=True)
        self.list.tv.column("#0", width=(44 if selected is not None else 30) * S)
        self.list.pack(fill="both", expand=True)
        self.list.on_select(self.show_detail)
        if selected is not None:
            self.list.tv.bind("<space>", lambda e: (self.toggle(), "break")[1])
            self.list.on_open(self.toggle)
            self.list.tv.bind("<Button-1>", self.click, add="+")
        self.detail = W.textbox(right, sk, height=6, width=60, bg=sk["window"], cursor="arrow")
        self.detail.configure(state="disabled", highlightthickness=0)
        tk.Frame(right, bg=sk["dark"], height=S).pack(fill="x", pady=(6 * S, 0))
        self.detail.pack(fill="x")
        self.search.bind("<Down>", lambda e: self.list.focus())
        self.refresh()

    def fill_subjects(self):
        self.subject_keys = [ALL] + list(self.gui.store.subjects)
        if any(s.get("custom") for s in self.gui.store.standards):
            self.subject_keys.append("Custom")
        lb = self.subjects
        lb.delete(0, "end")
        for k in self.subject_keys:
            lb.insert("end", "All subjects" if k == ALL else k)
        if self.subject not in self.subject_keys:
            self.subject = ALL
        i = self.subject_keys.index(self.subject)
        lb.selection_set(i)
        lb.see(i)

    def pick_subject(self):
        sel = self.subjects.curselection()
        if sel:
            self.subject = self.subject_keys[sel[0]]
            self.gui.std_subject = self.subject
            self.refresh()

    def soon(self):
        if self.pending:
            self.after_cancel(self.pending)
        self.pending = self.after(250, self.refresh)

    def items(self):
        items = self.gui.store.standards
        if self.selected is not None and self.show_sel.get():
            idx = self.gui.store.std_index
            return [idx[c] for c in self.selected if c in idx]
        if self.subject == "Custom":
            items = [s for s in items if s.get("custom")]
        elif self.subject != ALL:
            items = [s for s in items if s["subject"] == self.subject or s.get("custom")]
        g = self.grade.get()
        if g != ALL:
            items = [s for s in items if grades_match(s["grades"], g)]
        q = self.q.get().strip().lower()
        if q:
            items = [s for s in items if q in s["code"].lower() or q in s["text"].lower()
                     or q in s["cluster"].lower() or q in s["strand"].lower()]
        return items

    def refresh(self):
        self.pending = None
        tv = self.list.tv
        keep = self.list.selected()
        tv.delete(*tv.get_children())
        self.list.rows = {}
        self.iid = {}
        items = self.items()
        flat = bool(self.q.get().strip()) or (self.selected is not None and self.show_sel.get())
        for s in items:
            parent = "" if flat or not s["parent"] else self.iid.get(s["parent"], "")
            mark = (CHECK_ON if s["code"] in self.selected else CHECK_OFF) if self.selected is not None else ""
            iid = tv.insert(parent, "end", text=mark, values=(s["code"], s["grades"], " ".join(s["text"].split())),
                            open=False)
            self.list.rows[iid] = s
            self.iid[s["code"]] = iid
        tops = sum(1 for s in items if not s["parent"])
        if keep is not None and keep["code"] in self.iid:
            iid = self.iid[keep["code"]]
            tv.see(iid)
            tv.selection_set(iid)
        elif tv.get_children():
            first = tv.get_children()[0]
            tv.selection_set(first)
            tv.focus(first)
        if not items:
            self.list.empty.configure(text="No standards match. Try another subject or grade, or clear the search."
                                      if self.gui.store.standards else
                                      "No standards yet. Use Import File… to bring in a CSV or JSON file.")
            self.list.empty.place(relx=0.5, rely=0.35, anchor="center")
        else:
            self.list.empty.place_forget()
        subj = "All subjects" if self.subject == ALL else self.subject
        note = f"{tops:,} standard{'s' if tops != 1 else ''}" + (f" + {len(items) - tops:,} parts" if len(items) > tops else "")
        self.gui.status(f"{subj} · {grade_label(self.grade.get())} · {note}",
                        f"{len(self.selected)} checked" if self.selected is not None else None)
        self.show_detail()

    def click(self, e):
        if self.list.tv.identify_column(e.x) == "#0" and self.list.tv.identify_element(e.x, e.y) != "Treeitem.indicator":
            row = self.list.tv.identify_row(e.y)
            if row:
                self.list.tv.selection_set(row)
                self.toggle()
                return "break"

    def toggle(self):
        s = self.list.selected()
        if s is None or self.selected is None:
            return
        code = s["code"]
        if code in self.selected:
            self.selected.remove(code)
        else:
            self.selected.append(code)
        iid = self.iid.get(code)
        if iid:
            self.list.tv.item(iid, text=CHECK_ON if code in self.selected else CHECK_OFF)
        self.gui.status(None, f"{len(self.selected)} checked")
        if self.on_change:
            self.on_change()

    def show_detail(self):
        s = self.list.selected()
        t = self.detail
        t.configure(state="normal")
        t.delete("1.0", "end")
        sk = self.gui.skin
        t.tag_configure("code", font=sk.fb)
        t.tag_configure("dim", foreground=sk["dim"])
        if s:
            idx = self.gui.store.std_index
            t.insert("end", s["code"], "code")
            path = "  /  ".join(x for x in (s["subject"], s["strand"], s["cluster"], grade_label(s["grades"])) if x)
            t.insert("end", "   " + path + "\n", "dim")
            if s["parent"]:
                p = idx[s["parent"]]
                t.insert("end", f"Part of {p['code']}: {p['text']}\n", "dim")
                t.insert("end", sub_label(p, s["label"]) + s["text"] + "\n")
            else:
                t.insert("end", s["text"] + "\n")
                for k, v in s["subs"]:
                    t.insert("end", "   " + sub_label(s, k) + v + "\n")
            t.insert("end", "Source: " + s["source"], "dim")
        t.configure(state="disabled")


class StandardsLibrary:
    title = "Standards Library"

    def __init__(self, gui, parent):
        self.gui = gui
        sk, S = gui.skin, gui.skin.S
        f = tk.Frame(parent, bg=sk["window"])
        f.pack(fill="both", expand=True)
        bar = toolbar(f, sk)
        tool(bar, sk, "Import File…", self.import_file)
        tool(bar, sk, "Remove Imported Subject…", self.remove_subject)
        tool(bar, sk, "Add My Own…", self.add_custom)
        self.del_btn = tool(bar, sk, "Delete Mine", self.delete_custom)
        self.build(f)

    def build(self, f=None):
        sk, S = self.gui.skin, self.gui.skin.S
        if f is None:
            self.browser.destroy()
            f = self.parent
        self.parent = f
        self.browser = StandardsBrowser(f, self.gui)
        self.browser.pack(fill="both", expand=True, padx=8 * S, pady=(0, 8 * S))
        self.browser.list.on_select(self.selected_changed)
        self.selected_changed()

    def selected_changed(self):
        self.browser.show_detail()
        s = self.browser.list.selected()
        self.del_btn.set_enabled(bool(s and s.get("custom")))

    def cmd_find(self):
        self.browser.search.focus_set()

    def escape(self):
        if self.browser.q.get():
            self.browser.q.set("")
            return True
        return False

    def reloaded(self):
        self.build()

    def import_file(self):
        gui = self.gui
        path = filedialog.askopenfilename(parent=gui.root, title="Import standards",
                                          filetypes=[("Standards files", "*.csv *.json *.CSV *.JSON"),
                                                     ("All files", "*")])
        if not path:
            return
        try:
            done = gui.store.import_standards(path)
        except (ValueError, OSError) as e:
            W.alert(gui, "Import Didn't Work", str(e)[:1].upper() + str(e)[1:].lower(), "warn")
            return
        if gui.store.warning:
            W.alert(gui, "Import", gui.store.warning.lstrip("?"), "warn")
            gui.store.warning = None
            return
        lines = [f"{subj}: {n:,} standards" + (f" ({skip} skipped: code already used)" if skip else "")
                 for subj, n, skip in done]
        gui.std_subject = done[0][0]
        self.build()
        W.alert(gui, "Imported", "\n".join(lines))

    def remove_subject(self):
        gui = self.gui
        opts = gui.store.imported_subjects()
        if not opts:
            W.alert(gui, "Remove Subject", "You haven't imported any subjects yet. Use Import File… to bring "
                                           "in a CSV or JSON file of standards.")
            return
        s = W.choose(gui, "Remove Imported Subject", "Which subject?", [(o, o) for o in opts])
        if s and W.confirm(gui, "Remove Subject", f"Remove {s}? Lessons keep the codes they already use.", "Remove",
                           icon="warn"):
            gui.store.remove_subject(s)
            self.build()

    def add_custom(self):
        gui = self.gui
        sk, S = gui.skin, gui.skin.S
        d = W.Dialog(gui, "Add My Own Standard")
        W.label(d.body, sk, "Code (like SL.9-10.1 or DISTRICT-1):").pack(anchor="w")
        code = tk.StringVar()
        e = W.entry(d.body, sk, code, width=24)
        e.pack(anchor="w", pady=(2 * S, 8 * S))
        W.label(d.body, sk, "Full text of the standard:").pack(anchor="w")
        t = W.textbox(d.body, sk, height=5, width=56)
        t.pack(fill="both", expand=True, pady=(2 * S, 0))
        got = {}
        d.harvest = lambda: got.update(text=" ".join(t.get("1.0", "end").split()))
        d.buttons([("Add", True), ("Cancel", None)])
        if not d.run(focus=e):
            return
        c, text = code.get().strip(), got.get("text", "")
        if not c or not text:
            return
        if c in gui.store.std_index:
            W.alert(gui, "Add My Own Standard", f"There's already a standard with the code {c}.", "warn")
            return
        gui.store.data["custom_standards"].append({"code": c, "text": text, "grades": "Custom"})
        gui.store.reload_standards()
        gui.save()
        self.build()
        gui.status(f"Added {c}.")

    def delete_custom(self):
        gui = self.gui
        s = self.browser.list.selected()
        if not s or not s.get("custom"):
            return
        if W.confirm(gui, "Delete Standard", f"Delete your standard {s['code']}?", "Delete", icon="warn"):
            gui.store.data["custom_standards"] = [x for x in gui.store.data["custom_standards"] if x["code"] != s["code"]]
            gui.store.reload_standards()
            gui.save()
            self.build()


def pick_standards(gui, current, grades=None):
    """Check off standards; returns the new list of codes, or None if cancelled."""
    sel = list(current)
    d = W.Dialog(gui, "Choose Standards", size=(0.9, 0.86))
    sk, S = gui.skin, gui.skin.S
    info = tk.StringVar()

    def update():
        info.set(f"Checked ({len(sel)}): " + (", ".join(sel) if sel else "none yet"))
    W.label(d.body, sk, "Click the box (or press Space) to check a standard. Click ▸ to see its parts.",
            dim=True).pack(anchor="w", pady=(0, 6 * S))
    b = StandardsBrowser(d.body, gui, grades=grades, selected=sel, on_change=update)
    b.pack(fill="both", expand=True)
    row = tk.Frame(d.body, bg=sk["window"])
    row.pack(side="bottom", fill="x", pady=(8 * S, 0))
    tk.Label(row, textvariable=info, font=sk.fb, bg=sk["window"], fg=sk["text"], anchor="w", justify="left",
             wraplength=900 * S).pack(side="left", fill="x", expand=True)

    def clear():
        sel.clear()
        b.refresh()
        update()
    W.Button(row, sk, "Uncheck All", clear, small=True).pack(side="right")
    update()
    d.buttons([("OK", True), ("Cancel", None)])
    row.pack_configure(after=d.button_row)
    return sel if d.run(focus=b.list.tv) else None
