"""Settings: you and your class, documents, board slides, and how this window looks."""

import tkinter as tk

from ..store import GRADE_CHOICES
from . import widgets as W
from .common import AutoText, LineField
from .desktop import grade_label
from .export import STYLE_TEXT, board_options, browse
from .skin import SKINS, TEXT_SIZES, auto_scale


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

        g = W.group(right, sk, "Board Slides")
        g.pack(fill="x")
        style = tk.StringVar(value=st.get("board_style", "chalk"))
        for k, t in STYLE_TEXT.items():
            W.Radio(g, sk, t, style, k).pack(anchor="w")
        style.trace_add("write", lambda *a: self.set(board_style=style.get()))
        W.Button(g, sk, "Sections & School Colors…", lambda: (board_options(gui), style.set(st.get("board_style"))),
                 small=True).pack(anchor="w", pady=(6 * S, 0))

        g = W.group(right, sk, "This Window")
        g.pack(fill="x", pady=(10 * S, 0))
        W.label(g, sk, "Look:").pack(anchor="w")
        look = tk.StringVar(value=sk.kind)
        for k, s in SKINS.items():
            W.Radio(g, sk, f"{s['name']} — {s['about']}", look, k).pack(anchor="w", padx=(12 * S, 0))
        look.trace_add("write", lambda *a: gui.root.after_idle(lambda: gui.set_look(gui_skin=look.get())))
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
        W.Check(g, sk, "Show the welcome screen", boot).pack(anchor="w", pady=(8 * S, 0))
        W.Button(g, sk, "Run Setup Again…", self.setup, small=True).pack(anchor="w", pady=(8 * S, 0))

        g = W.group(right, sk, "Your Data")
        g.pack(fill="x", pady=(10 * S, 0))
        W.label(g, sk, f"Everything is saved to\n{gui.store.path}\nThe terminal version (chalkboard) uses the same "
                       "file. Copy it to move your work to another computer.", wrap=440 * S).pack(anchor="w")
        gui.status("Changes save by themselves.")

    def setup(self):
        from .setup import run_setup
        self.gui.root.after_idle(lambda: run_setup(self.gui))

    def set(self, **kw):
        self.gui.settings.update(kw)
        self.gui.save_soon()
