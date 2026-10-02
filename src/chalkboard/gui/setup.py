"""The setup wizard: name, school, class, school colors, materials, look, and installing the app."""

import tkinter as tk
from tkinter import colorchooser

from ..store import GRADE_CHOICES, parse_hex
from . import widgets as W
from .desktop import APP, grade_label
from .skin import SKINS
from . import install as I

PREVIEW = {"primary_color": "#1F5A3A", "secondary_color": "#F2E6A0", "text_color": "#FFFFFF"}


def needed(gui):
    """True for a brand-new copy: setup never ran and there's no sign of earlier use."""
    st, d = gui.settings, gui.store.data
    if st.get("setup_done"):
        return False
    if st.get("teacher") or d["lessons"] or d["assessments"]:
        st["setup_done"] = True  # someone who used Chalkboard before setup existed
        return False
    return True


def run_setup(gui):
    """Show the wizard. Saves what was entered (even on Skip) and marks setup as done."""
    Wizard(gui).run()


class Wizard:
    def __init__(self, gui):
        self.gui = gui
        st = gui.settings
        self.v = {k: tk.StringVar(value=str(st.get(k) or "")) for k in
                  ("teacher", "school", "course", "subject", "grades", "primary_color", "secondary_color", "text_color")}
        if not self.v["grades"].get():
            self.v["grades"].set("9-10")
        self.look = tk.StringVar(value=st.get("gui_skin", "bevel"))
        self.materials = st.get("default_materials", "")
        self.plan = I.plan()
        self.do_install = tk.BooleanVar(value=bool(self.plan))
        self.desktop = tk.BooleanVar(value=True)
        self.pages = [self.p_welcome, self.p_you, self.p_class, self.p_colors, self.p_materials, self.p_look]
        if self.plan:
            self.pages.append(self.p_install)
        self.pages.append(self.p_done)
        self.i = 0

    # ------------------------------------------------------------ frame
    def run(self):
        gui = self.gui
        sk, S = gui.skin, gui.skin.S
        d = self.d = W.Dialog(gui, f"{APP} Setup")
        d.cancel_value = "skip"
        row = tk.Frame(d.body, bg=sk["window"])
        row.pack(fill="both", expand=True)
        art = tk.Frame(row, bg=sk["sel"] if sk.bevel else sk["dark"], width=190 * S, padx=14 * S, pady=14 * S)
        art.pack(side="left", fill="y")
        art.pack_propagate(False)
        tk.Label(art, image=sk.icon("logo", 6), bg=art["bg"]).pack(pady=(20 * S, 10 * S))
        tk.Label(art, text=APP, font=sk.fbig, bg=art["bg"], fg="#FFFFFF").pack()
        self.steps = tk.Label(art, text="", font=sk.fsmall, bg=art["bg"], fg="#FFFFFF", justify="left", anchor="w")
        self.steps.pack(fill="x", pady=(24 * S, 0))
        self.page = tk.Frame(row, bg=sk["window"], width=560 * S, height=400 * S, padx=18 * S)
        self.page.pack(side="left", fill="both", expand=True)
        self.page.pack_propagate(False)
        bar = tk.Frame(d.body, bg=sk["window"])
        bar.pack(fill="x", pady=(12 * S, 0))
        tk.Frame(d.body, bg=sk["dark"], height=S).pack(fill="x", before=bar, pady=(10 * S, 0))
        self.skip = W.Button(bar, sk, "Skip Setup", self.cancel, minwidth=90)
        self.skip.pack(side="left")
        self.next = W.Button(bar, sk, "Next ►", self.forward, default=True, minwidth=90)
        self.next.pack(side="right")
        self.prev = W.Button(bar, sk, "◄ Back", self.backward, minwidth=90)
        self.prev.pack(side="right", padx=(0, 8 * S))
        d.default = self.next
        self.show()
        d.run(focus=self.next)
        self.save(final=True)

    def show(self):
        for w in self.page.winfo_children():
            w.destroy()
        self.pages[self.i]()
        last = self.i == len(self.pages) - 1
        self.prev.set_enabled(self.i > 0)
        self.next.set_text("Finish" if last else "Next ►")
        self.skip.set_enabled(not last)
        names = ["Welcome", "About you", "Your class", "School colors", "Materials", "Look"] + \
                (["Install"] if self.plan else []) + ["Done"]
        self.steps.configure(text="\n".join(("► " if j == self.i else "   ") + n for j, n in enumerate(names)))
        # new widgets need the dialog's key bindings (Return = Next, Esc = Skip)
        tag = f"dlg{id(self.d)}"
        for w in W.descendants(self.page):
            if tag not in w.bindtags():
                w.bindtags(w.bindtags() + (tag,))
        first = next((w for w in W.descendants(self.page) if isinstance(w, tk.Entry)), None)
        (first or self.next).focus_set()

    def forward(self):
        self.collect()
        if self.i == len(self.pages) - 1:
            self.d.close("done")
            return
        if self.pages[self.i] == self.p_install and self.do_install.get():
            if not self.install_now():
                return
        self.i += 1
        self.show()

    def backward(self):
        self.collect()
        if self.i > 0:
            self.i -= 1
            self.show()

    def cancel(self):
        if W.confirm(self.gui, "Skip Setup", "Skip the rest of setup? What you've typed so far is kept, and you can "
                     "run setup again any time from Settings.", "Skip", "Keep Going"):
            self.collect()
            self.d.close("skip")

    # ------------------------------------------------------------ pages
    def head(self, title, text=""):
        sk, S = self.gui.skin, self.gui.skin.S
        tk.Label(self.page, text=title, font=sk.fbig, bg=sk["window"], fg=sk["text"], anchor="w").pack(
            fill="x", pady=(8 * S, 6 * S))
        if text:
            W.label(self.page, sk, text, wrap=520 * S).pack(anchor="w", pady=(0, 12 * S))

    def field(self, label, key, hint="", width=34):
        sk, S = self.gui.skin, self.gui.skin.S
        W.label(self.page, sk, label, bold=True).pack(anchor="w", pady=(6 * S, 2 * S))
        W.entry(self.page, sk, self.v[key], width=width).pack(anchor="w")
        if hint:
            W.label(self.page, sk, hint, dim=True, small=True).pack(anchor="w", pady=(2 * S, 0))

    def p_welcome(self):
        self.head(f"Welcome to {APP}",
                  "This gets Chalkboard ready for your classroom. It takes about a minute, and you can change any of "
                  "it later in Settings.")
        W.label(self.page, self.gui.skin, "Click Next to begin.").pack(anchor="w")

    def p_you(self):
        self.head("About You", "Your name and school go in the footer of handouts and make-up sheets.")
        self.field("Your name, the way students see it", "teacher", "Like Mrs. Rivera or Mr. Thompson")
        self.field("School", "school")

    def p_class(self):
        sk, S = self.gui.skin, self.gui.skin.S
        self.head("Your Class", "New lessons and quizzes start with these. You can still plan for any class.")
        self.field("The class you teach most", "course", "Like English 10 or Biology", width=26)
        W.label(self.page, sk, "Grades", bold=True).pack(anchor="w", pady=(10 * S, 2 * S))
        W.Dropdown(self.page, sk, [(g, grade_label(g)) for g in GRADE_CHOICES], self.v["grades"], width=16).pack(
            anchor="w")
        subjects = self.gui.store.subjects
        if subjects:
            if self.v["subject"].get() not in subjects:
                self.v["subject"].set(subjects[0])
            W.label(self.page, sk, "Subject (for picking standards)", bold=True).pack(anchor="w", pady=(10 * S, 2 * S))
            W.Dropdown(self.page, sk, [(s, s) for s in subjects], self.v["subject"], width=28).pack(anchor="w")

    def p_colors(self):
        sk, S = self.gui.skin, self.gui.skin.S
        self.head("School Colors", "Board slides can use your school's colors. Type HEX codes (like #7A0019), click "
                                   "Pick… to choose, or leave them blank for a green chalkboard.")
        grid = tk.Frame(self.page, bg=sk["window"])
        grid.pack(anchor="w")
        for r, (key, label) in enumerate((("primary_color", "Background"), ("secondary_color", "Headings"),
                                          ("text_color", "Text"))):
            W.label(grid, sk, label + ":").grid(row=r, column=0, sticky="w", pady=3 * S, padx=(0, 8 * S))
            W.entry(grid, sk, self.v[key], width=10).grid(row=r, column=1, sticky="w", pady=3 * S)
            W.Button(grid, sk, "Pick…", lambda k=key: self.pick(k), small=True).grid(row=r, column=2, padx=(6 * S, 0))
        self.preview = tk.Canvas(self.page, width=320 * S, height=150 * S, highlightthickness=S,
                                 highlightbackground=sk["dark"], bg="#000000")
        self.preview.pack(anchor="w", pady=(12 * S, 0))
        for key in PREVIEW:
            self.v[key].trace_add("write", lambda *a: self.draw_preview())
        self.draw_preview()

    def pick(self, key):
        cur = parse_hex(self.v[key].get()) or PREVIEW[key]
        got = colorchooser.askcolor(color=cur, parent=self.gui.root, title="School color")
        if got and got[1]:
            self.v[key].set(got[1].upper())

    def draw_preview(self):
        c = getattr(self, "preview", None)
        if c is None or not c.winfo_exists():
            return
        sk, S = self.gui.skin, self.gui.skin.S
        col = {k: parse_hex(self.v[k].get()) or PREVIEW[k] for k in PREVIEW}
        c.delete("all")
        c.configure(bg=col["primary_color"])
        c.create_text(14 * S, 14 * S, text="I Can…", anchor="nw", fill=col["secondary_color"], font=sk.fb)
        c.create_text(14 * S, 40 * S, text="- Cite strong evidence from the text\n- Explain what it shows",
                      anchor="nw", fill=col["text_color"], font=sk.f)
        c.create_text(14 * S, 92 * S, text="Bell Ringer", anchor="nw", fill=col["secondary_color"], font=sk.fb)
        c.create_text(14 * S, 116 * S, text="What's one thing that made you smile this week?", anchor="nw",
                      fill=col["text_color"], font=sk.f)

    def p_materials(self):
        sk, S = self.gui.skin, self.gui.skin.S
        self.head("Materials", "What do students need every day? Every new lesson starts with this list. Put each "
                               "item on its own line.")
        t = self.mat = W.textbox(self.page, sk, height=8, width=46)
        t.insert("1.0", self.materials or "- Notebook\n- Pencil or pen\n- Chromebook")
        t.pack(anchor="w", fill="x")
        t.bind("<Tab>", lambda e: (self.next.focus_set(), "break")[1])
        t.after_idle(t.focus_set)

    def p_look(self):
        sk, S = self.gui.skin, self.gui.skin.S
        self.head("Look", "Pick the look you like. You can switch any time from the View menu.")
        for k, s in SKINS.items():
            W.Radio(self.page, sk, f"{s['name']}: {s['about']}", self.look, k).pack(anchor="w", pady=2 * S)

    def p_install(self):
        sk, S = self.gui.skin, self.gui.skin.S
        self.head("Install", "Put Chalkboard on this computer so it's easy to open next time?")
        W.Check(self.page, sk, "Install Chalkboard on this computer", self.do_install).pack(anchor="w")
        W.label(self.page, sk, self.plan["text"], dim=True, wrap=500 * S).pack(anchor="w", padx=(24 * S, 0),
                                                                              pady=(2 * S, 8 * S))
        if self.plan["kind"] == "windows":
            W.Check(self.page, sk, "Also put a shortcut on the desktop", self.desktop).pack(anchor="w")

    def p_done(self):
        st = self.gui.settings
        name = st.get("teacher") or "you"
        self.head("All Set!", f"Chalkboard is ready for {name}. Click Finish to start planning.")
        tips = ["Lesson Plans: plan a lesson, then export it or make a board slide.",
                "Assessments: build quizzes, tests, and worksheets.",
                "Back and Home are at the top of every window."]
        for t in tips:
            W.label(self.page, self.gui.skin, "•  " + t, wrap=500 * self.gui.skin.S).pack(anchor="w", pady=2)

    # ------------------------------------------------------------ saving
    def collect(self):
        """Copy what's on the current page into the settings (no saving to disk yet)."""
        st = self.gui.settings
        for k in ("teacher", "school", "course", "grades", "subject"):
            v = self.v[k].get().strip()
            if v or k in ("teacher", "school"):
                st[k] = v
        colors = {}
        for k in PREVIEW:
            raw = self.v[k].get().strip()
            code = parse_hex(raw) if raw else ""
            if code is not None:
                colors[k] = code
        st.update(colors)
        if any(st.get(k) for k in PREVIEW):
            st["board_style"] = "school"
        mat = getattr(self, "mat", None)
        if mat is not None and mat.winfo_exists():
            self.materials = mat.get("1.0", "end-1c").strip()
            st["default_materials"] = self.materials

    def install_now(self):
        gui = self.gui
        try:
            note = I.install(self.plan, desktop_shortcut=self.desktop.get())
        except Exception as e:  # noqa: BLE001 - tell the teacher, let them carry on without installing
            W.alert(gui, "Couldn't Install", f"Chalkboard couldn't be installed:\n{e}\n\nYou can still use it from "
                                             "where it is now.", "warn")
            return True
        self.installed = note
        return True

    def save(self, final=False):
        gui = self.gui
        st = gui.settings
        st["setup_done"] = True
        look = self.look.get()
        changed_look = look != gui.skin.kind
        gui.save()
        if getattr(self, "installed", None) and self.plan and I.relaunch(self.plan):
            gui.quit()  # the Mac copy in Applications takes over
            return
        if changed_look:
            gui.set_look(gui_skin=look)
        else:
            gui.show(gui.current, push=False)
        if getattr(self, "installed", None):
            gui.status(self.installed)
