"""Settings: you and your class, documents, board slides, and how this window looks."""

import os
import tkinter as tk
from tkinter import filedialog

from ..store import GRADE_CHOICES
from . import widgets as W
from .common import AutoText, LineField
from .desktop import grade_label
from .export import STYLE_TEXT, board_options, browse, class_periods, logo_options, logo_text, periods_text
from .skin import SKINS, TEXT_SIZES, THEMES, auto_scale


def sections_text(gui):
    from ..store import FIXED_FIELDS, LESSON_FIELDS
    hide = set(gui.settings.get("lesson_hide") or [])
    off = [label for k, label, _ in LESSON_FIELDS if k in hide and k not in FIXED_FIELDS]
    return ("Hidden from the lesson editor: " + ", ".join(off) + ".") if off else "The lesson editor shows every section."


class SettingsView:
    title = "Settings"

    def __init__(self, gui, parent):
        self.gui = gui
        st = gui.settings
        sk, S = gui.skin, gui.skin.S
        area = W.ScrollArea(parent, sk, gui, maxwidth=1000 * S)
        area.pack(fill="both", expand=True)
        p = area.inner
        cols = tk.Frame(p, bg=sk["window"])
        cols.pack(fill="both", expand=True, padx=12 * S, pady=10 * S)
        left = tk.Frame(cols, bg=sk["window"])
        left.pack(side="left", fill="both", expand=True, anchor="n")
        right = tk.Frame(cols, bg=sk["window"])
        right.pack(side="left", fill="both", expand=True, anchor="n", padx=(12 * S, 0))

        g = W.group(left, sk, "You and Your Class")
        g.pack(fill="x")
        g.columnconfigure(1, weight=1)
        for r, (key, label) in enumerate((("teacher", "Your name (for footers)"), ("school", "School"),
                                          ("course", "Usual course"))):
            W.label(g, sk, label + ":").grid(row=r, column=0, sticky="w", pady=3 * S, padx=(0, 8 * S))
            LineField(g, gui, st, key, width=24).entry.grid(row=r, column=1, sticky="ew", pady=3 * S)
        W.label(g, sk, "Usual subject:").grid(row=3, column=0, sticky="w", pady=3 * S)
        subj = tk.StringVar(value=st.get("subject", ""))
        W.Dropdown(g, sk, [(s, s) for s in gui.store.subjects] or [("", "(import standards first)")], subj,
                   lambda v: self.set(subject=v), width=22).grid(row=3, column=1, sticky="w", pady=3 * S)
        W.label(g, sk, "Usual grades:").grid(row=4, column=0, sticky="w", pady=3 * S)
        gr = tk.StringVar(value=st.get("grades", "9-10"))
        W.Dropdown(g, sk, [(x, grade_label(x)) for x in GRADE_CHOICES], gr, lambda v: self.set(grades=v),
                   width=14).grid(row=4, column=1, sticky="w", pady=3 * S)
        W.label(g, sk, "Materials every new lesson starts with:").grid(row=5, column=0, columnspan=2, sticky="w",
                                                                         pady=(8 * S, 2 * S))
        AutoText(g, gui, st, "default_materials", min_lines=2, width=30).text.grid(row=6, column=0, columnspan=2,
                                                                                   sticky="ew")

        g = W.group(left, sk, "Printed Documents")
        g.pack(fill="x", pady=(10 * S, 0))
        font = tk.StringVar(value=st.get("font", "Times"))
        page = tk.StringVar(value=st.get("page", "Letter"))
        font.trace_add("write", lambda *a: self.set(font=font.get()))
        page.trace_add("write", lambda *a: self.set(page=page.get()))
        row = tk.Frame(g, bg=g["bg"])
        row.pack(anchor="w")
        W.label(row, sk, "Font:").pack(side="left", padx=(0, 8 * S))
        for v, t in (("Times", "Times (serif)"), ("Helvetica", "Helvetica (sans)")):
            W.Radio(row, sk, t, font, v).pack(side="left", padx=(0, 12 * S))
        row = tk.Frame(g, bg=g["bg"])
        row.pack(anchor="w")
        W.label(row, sk, "Paper:").pack(side="left", padx=(0, 8 * S))
        for v in ("Letter", "A4"):
            W.Radio(row, sk, v, page, v).pack(side="left", padx=(0, 12 * S))
        W.label(g, sk, "Export folder:").pack(anchor="w", pady=(8 * S, 2 * S))
        row = tk.Frame(g, bg=g["bg"])
        row.pack(fill="x")
        folder = tk.StringVar(value=gui.store.export_dir())
        folder.trace_add("write", lambda *a: self.set(export_dir=folder.get().strip()))
        W.entry(row, sk, folder, width=30).pack(side="left", fill="x", expand=True)
        W.Button(row, sk, "Browse…", lambda: browse(gui, folder), small=True).pack(side="left", padx=(6 * S, 0))

        g = W.group(left, sk, "Lesson Sections")
        g.pack(fill="x", pady=(10 * S, 0))
        sections = W.label(g, sk, sections_text(gui), wrap=400 * S)
        sections.pack(anchor="w")

        def pick_sections():
            from .lessons import lesson_sections
            if lesson_sections(gui):
                sections.configure(text=sections_text(gui))
        W.Button(g, sk, "Choose Sections…", pick_sections, small=True).pack(anchor="w", pady=(6 * S, 0))

        g = W.group(right, sk, "Board Slides")
        g.pack(fill="x")
        style = tk.StringVar(value=st.get("board_style", "chalk"))
        for k, t in STYLE_TEXT.items():
            W.Radio(g, sk, t, style, k).pack(anchor="w")
        style.trace_add("write", lambda *a: self.set(board_style=style.get()))
        W.Button(g, sk, "Sections & School Colors…", lambda: (board_options(gui), style.set(st.get("board_style"))),
                 small=True).pack(anchor="w", pady=(6 * S, 0))
        logo = W.label(g, sk, logo_text(gui), dim=True, wrap=440 * S)
        periods = W.label(g, sk, periods_text(gui), dim=True, wrap=440 * S)

        def update():
            logo.configure(text=logo_text(gui))
            periods.configure(text=periods_text(gui))
        W.Button(g, sk, "School Logo…", lambda: logo_options(gui, update), small=True).pack(anchor="w", pady=(10 * S, 0))
        logo.pack(anchor="w", pady=(2 * S, 0))
        W.Button(g, sk, "Class Periods & Codes…", lambda: class_periods(gui, update), small=True).pack(
            anchor="w", pady=(8 * S, 0))
        periods.pack(anchor="w", pady=(2 * S, 0))

        g = W.group(right, sk, "This Window")
        g.pack(fill="x", pady=(10 * S, 0))
        W.label(g, sk, "Look:").pack(anchor="w")
        look = tk.StringVar(value=sk.kind)
        for k, s in SKINS.items():
            W.Radio(g, sk, f"{s['name']} — {s['about']}", look, k).pack(anchor="w", padx=(12 * S, 0))
        look.trace_add("write", lambda *a: gui.root.after_idle(lambda: gui.set_look(gui_skin=look.get())))
        W.label(g, sk, "Light or dark (every look has both):").pack(anchor="w", pady=(8 * S, 0))
        theme = tk.StringVar(value=gui.theme())
        row = tk.Frame(g, bg=g["bg"])
        row.pack(anchor="w", padx=(12 * S, 0))
        for k, t in THEMES:
            W.Radio(row, sk, t, theme, k).pack(side="left", padx=(0, 12 * S))
        theme.trace_add("write", lambda *a: gui.root.after_idle(lambda: gui.set_look(gui_theme=theme.get())))
        row = tk.Frame(g, bg=g["bg"])
        row.pack(anchor="w", pady=(8 * S, 0))
        W.label(row, sk, "Text size:").pack(side="left")
        size = tk.StringVar(value=str(gui.text_px()))
        W.Dropdown(row, sk, [(str(n), {13: f"{n} (normal)"}.get(n, str(n))) for n in TEXT_SIZES], size,
                   lambda v: gui.root.after_idle(lambda: gui.set_look(gui_text=int(v))), width=10).pack(
            side="left", padx=(6 * S, 16 * S))
        W.label(row, sk, "Size of everything:").pack(side="left")
        scale = tk.StringVar(value=str(st.get("gui_scale", 0) or 0))
        W.Dropdown(row, sk, [("0", f"Automatic ({auto_scale(gui.root)}×)"),
                             ("1", "1× (normal)"), ("2", "2× (sharp screens)"), ("3", "3×")], scale,
                   lambda v: gui.root.after_idle(lambda: gui.set_look(gui_scale=int(v))), width=18).pack(
            side="left", padx=(6 * S, 0))
        boot = tk.BooleanVar(value=st.get("boot", True))
        boot.trace_add("write", lambda *a: self.set(boot=boot.get()))
        W.Check(g, sk, "Show the welcome screen (retro looks)", boot).pack(anchor="w", pady=(8 * S, 0))
        W.Button(g, sk, "Run Setup Again…", self.setup, small=True).pack(anchor="w", pady=(8 * S, 0))

        g = W.group(right, sk, "Your Data")
        g.pack(fill="x", pady=(10 * S, 0))
        texts = [W.label(g, sk, f"Everything is saved to\n{gui.store.path}\nThe terminal version (chalkboard) uses "
                                "the same file.", wrap=440 * S),
                 W.label(g, sk, "A backup is one file with all your lessons, assessments, standards, and settings. "
                                "Import it on another computer, or here to get your work back.", wrap=440 * S)]
        texts[0].pack(anchor="w")
        texts[1].pack(anchor="w", pady=(8 * S, 0))
        row = tk.Frame(g, bg=g["bg"])
        row.pack(anchor="w", pady=(8 * S, 0))
        W.Button(row, sk, "Back Up Now…", lambda: backup_now(gui), small=True).pack(side="left")
        W.Button(row, sk, "Import Backup…", lambda: import_backup(gui), small=True).pack(side="left", padx=(6 * S, 0))
        self.last = W.label(g, sk, last_backup_text(gui), dim=True, wrap=440 * S)
        self.last.pack(anchor="w", pady=(6 * S, 0))
        texts.append(self.last)
        # wrap to the column, however wide the window is
        g.bind("<Configure>", lambda e: [t.configure(wraplength=max(200 * S, e.width - 24 * S)) for t in texts])
        gui.status("Changes save by themselves.")

    def reloaded(self):
        self.gui.show(SettingsView, push=False)

    def setup(self):
        from .setup import run_setup
        self.gui.root.after(1, lambda: run_setup(self.gui))  # not after_idle: see TermHost.start

    def set(self, **kw):
        self.gui.settings.update(kw)
        self.gui.save_soon()


def last_backup_text(gui):
    st = gui.settings
    if not st.get("last_backup"):
        return "No backups yet."
    return f"Last backup: {st['last_backup']}, in {gui.store.backup_dir()}"


def backup_now(gui):
    folder = filedialog.askdirectory(parent=gui.root, initialdir=gui.store.backup_dir()
                                     if os.path.isdir(gui.store.backup_dir()) else os.path.expanduser("~"),
                                     title="Save a backup in which folder?")
    if not folder or not gui.save():
        return
    try:
        path = gui.store.backup(folder)
    except OSError as e:
        W.alert(gui, "Backup Didn't Work", f"Chalkboard couldn't save the backup:\n{e}", "warn")
        return
    gui.mtime = gui.file_mtime()
    if isinstance(gui.view, SettingsView):
        gui.view.last.configure(text=last_backup_text(gui))
    W.alert(gui, "Backed Up", f"Everything is saved in\n{path}\n\nKeep it somewhere safe, like a flash drive or "
                              "cloud folder.")


def import_backup(gui):
    path = filedialog.askopenfilename(parent=gui.root, title="Import a backup",
                                      initialdir=gui.store.backup_dir() if os.path.isdir(gui.store.backup_dir())
                                      else os.path.expanduser("~"),
                                      filetypes=[("Chalkboard backups", "*.json *.JSON"), ("All files", "*")])
    if not path:
        return
    how = W.choose(gui, "Import Backup", f"How should {os.path.basename(path)} come in?",
                   [("add", "Add what I don't have (keeps all my work)"),
                    ("replace", "Replace everything with the backup")], "add")
    if how is None:
        return
    if how == "replace" and not W.confirm(
            gui, "Replace Everything?", "Your lessons, assessments, standards, and settings will be swapped for the "
                                        "backup's. (Chalkboard saves a copy of them first.)", yes="Replace",
            icon="warn"):
        return
    if not gui.save():
        return
    try:
        got = gui.store.import_backup(path, replace=how == "replace")
    except (ValueError, OSError) as e:
        W.alert(gui, "Import Didn't Work", str(e)[:1].upper() + str(e)[1:].lower(), "warn")
        return
    gui.mtime = gui.file_mtime()
    if gui.view is not None and hasattr(gui.view, "reloaded"):
        gui.view.reloaded()
    n = lambda k, word, many=None: f"{got[k]} {word if got[k] == 1 else many or word + 's'}"
    if how == "replace":
        msg = f"Chalkboard now has the backup's {n('lessons', 'lesson')} and {n('assessments', 'assessment')}."
    else:
        msg = (f"Added {n('lessons', 'lesson')}, {n('assessments', 'assessment')}, {n('subjects', 'standards subject')}, "
               f"and {n('standards', 'custom standard')}.")
        if got["updated"]:
            msg += f" {n('updated', 'lesson or assessment', 'lessons or assessments')} got the backup's newer copy."
    W.alert(gui, "Backup Imported", f"{msg}\n\nWhat you had before is saved in\n{got['safety']}")
