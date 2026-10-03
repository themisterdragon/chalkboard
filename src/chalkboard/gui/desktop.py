"""Chalkboard in a window: the same planner and data file as the terminal app, in a retro or modern look."""

import argparse
import os
import sys

try:
    import tkinter as tk
    from tkinter import ttk
except ImportError:  # Linux without the Tk package; main() explains
    tk = None

from .. import __version__, offline, plugins
from ..banner import APP, big
from ..exporting import open_path
from ..export_txt import render_lines
from ..markup import plain
from ..store import ALL, Store, now

if tk:
    from .skin import (MAC, TEXT_SIZES, THEMES, Skin, SKINS, auto_scale, native_title_bar, system_accent,
                       system_dark)
    from . import widgets as W

MOD = "Command" if sys.platform == "darwin" else "Control"
MOD_LABEL = "Cmd" if sys.platform == "darwin" else "Ctrl"


def grade_label(g):
    if g == ALL or not g:
        return "All grades"
    if g == "K":
        return "Kindergarten"
    if not all(p.isdigit() or p == "K" for p in g.split("-")):
        return g
    return f"Grades {g}" if "-" in g else f"Grade {g}"


def first_line(s):
    return plain(next((line.strip() for line in (s or "").split("\n") if line.strip()), ""))


def plural(n, word, many=None):
    return f"{n:,} {word if n == 1 else (many or word + 's')}"


class Gui:
    def __init__(self, args):
        self.args = args
        self.store = Store()
        if not getattr(args, "no_plugins", False):
            plugins.load(self.store.dir)
        self.root = tk.Tk(className=APP)
        self.root.title(APP)
        self.root.protocol("WM_DELETE_WINDOW", self.quit)
        self.view = None
        self.stack = []        # factories for the windows behind the current one (the close box goes back)
        self.trail = []        # their titles, for the "Chalkboard › Lesson Plans › ..." trail
        self.current_title = APP
        self.modals = []       # open dialogs, innermost last
        self.scrollers = {}    # ScrollArea canvases, for the mouse wheel
        self.pending = None    # after() id of a scheduled save
        self.term = None       # the terminal view, while it's showing
        self.maximized = bool(self.settings.get("gui_maximized", False))
        self.mtime = self.file_mtime()
        self.apply_skin(first=True)
        try:
            self.root.iconphoto(True, self.skin.icon("logo", 2))
        except tk.TclError:
            pass
        S = self.skin.S
        sw, sh = self.root.winfo_screenwidth(), self.root.winfo_screenheight()
        self.root.geometry(f"{min(sw - 40, 1180 * S)}x{min(sh - 80, 800 * S)}")
        self.root.minsize(760 * S, 520 * S)
        for seq in ("<MouseWheel>", "<Button-4>", "<Button-5>"):
            self.root.bind_all(seq, self.wheel, add="+")
        self.bind_keys()
        from .setup import needed, run_setup
        first_time = not self.store.warning and needed(self)
        terminal = self.settings.get("gui_mode") == "terminal" and not first_time and not self.store.warning
        welcome = (self.settings.get("boot", True) and not args.no_boot and not first_time and not self.skin.modern
                   and not terminal)
        self.show(Welcome if welcome else Home, push=False)
        if terminal:
            self.root.after(50, lambda: self.enter_terminal(boot=not args.no_boot))
        if self.store.warning:
            self.root.after(300, lambda: W.alert(self, "Chalkboard", self.store.warning.lstrip("?"), "warn"))
        elif plugins.problems:
            note = "\n".join(p.capitalize() for p in plugins.take_problems())
            self.root.after(300, lambda: W.alert(self, "Plugins", note, "warn"))
        elif first_time:
            self.root.after(300, lambda: run_setup(self))
        self.root.after(2000, self.watch_file)
        self.root.after(3000, self.watch_os_look)

    # ------------------------------------------------------------ plumbing
    @property
    def settings(self):
        return self.store.settings

    def scale(self):
        v = self.settings.get("gui_scale", 0)
        return v if v in (1, 2, 3) else auto_scale(self.root)

    def text_px(self):
        v = self.settings.get("gui_text", 13)
        return v if v in TEXT_SIZES else 13

    def theme(self):
        v = self.settings.get("gui_theme", "system")
        return v if v in dict(THEMES) else "system"

    def os_look(self):
        """What the window follows from the OS: dark mode (when matching the computer), and Modern's accent color."""
        follow = self.theme() == "system"
        modern = self.settings.get("gui_skin") == "modern"
        return (system_dark() if follow else None, system_accent() if modern else None)

    def apply_skin(self, first=False):
        """(Re)build everything for the current skin, light or dark, and sizes."""
        st = self.settings
        self.seen_os_look = os_dark, accent = self.os_look()
        dark = os_dark if self.theme() == "system" else self.theme() == "dark"
        self.skin = Skin(self.root, st.get("gui_skin", "bevel"), self.scale(), self.text_px(), dark, accent)
        native_title_bar(self.root, dark, self.theme() == "system")
        self.skin.style_ttk()
        self.root.configure(bg=self.skin["desk"])
        self.root.option_add("*Menu.tearOff", 0)
        old = [] if first else [w for w in self.root.winfo_children() if not isinstance(w, tk.Menu)]
        self.scrollers = {}
        self.build_desktop()
        self.build_menu()
        return old

    def build_desktop(self):
        sk = self.skin
        self.desk = tk.Canvas(self.root, bg=sk["desk"], highlightthickness=0, bd=0)
        self.desk.place(x=0, y=0, relwidth=1, relheight=1)  # on top of the old desktop until that's removed
        if sk["desk2"]:
            self.desk.bind("<Configure>", self.tile_desk)
        self.window = None
        self.shadow = None

    def tile_desk(self, e):
        c, img = self.desk, self.skin.desk_tile()
        c.delete("tile")
        n = img.width()
        for y in range(0, e.height + n, n):
            for x in range(0, e.width + n, n):
                c.create_image(x, y, image=img, anchor="nw", tags="tile")
        c.tag_lower("tile")

    def place_window(self):
        if not self.window:
            return
        flat = self.skin.kind != "pinstripe"  # Pinstripe leaves room for its drop shadow
        m = 0 if self.maximized or self.skin.modern else 14 * self.skin.S
        self.window.place(x=m, y=m, relwidth=1, relheight=1, width=-2 * m - (0 if flat else 2 * self.skin.S),
                          height=-2 * m - (0 if flat else 2 * self.skin.S))
        if self.shadow:
            S2 = 2 * self.skin.S
            self.shadow.place(x=m + S2, y=m + S2, relwidth=1, relheight=1, width=-2 * m - S2, height=-2 * m - S2)
            self.shadow.lower(self.window)

    def toggle_zoom(self):
        if self.skin.modern:  # the real OS window
            try:
                self.root.state("normal" if self.root.state() == "zoomed" else "zoomed")
            except tk.TclError:  # X11
                self.root.attributes("-zoomed", not self.root.attributes("-zoomed"))
            return
        self.maximized = not self.maximized
        self.settings["gui_maximized"] = self.maximized
        self.place_window()
        self.save()

    def file_mtime(self):
        try:
            return os.path.getmtime(self.store.path)
        except OSError:
            return None

    def save(self):
        """Copy edits in progress into the data, then write the data file."""
        if self.pending:
            self.root.after_cancel(self.pending)
            self.pending = None
        if self.view is not None and hasattr(self.view, "collect"):
            self.view.collect()
        try:
            self.store.save()
            self.mtime = self.file_mtime()
            return True
        except OSError as e:
            W.alert(self, "Couldn't Save", f"Chalkboard couldn't save your work:\n{e}", "warn")
            return False

    def save_soon(self):
        """Typing autosaves a moment after the last keystroke."""
        if self.pending:
            self.root.after_cancel(self.pending)
        self.pending = self.root.after(700, self.autosave)

    def autosave(self):
        self.pending = None
        if self.save():
            self.status(f"Saved at {now()[-5:]}")

    def watch_file(self):
        """Pick up changes saved by the terminal Chalkboard (or another window)."""
        m = self.file_mtime()
        if m != self.mtime and not self.pending and not self.modals and not self.term:
            self.mtime = m
            self.store.load()
            self.store.reload_standards()
            if self.view is not None and hasattr(self.view, "reloaded"):
                self.view.reloaded()
            self.status("Loaded changes saved by another Chalkboard window.")
        self.root.after(2000, self.watch_file)

    def watch_os_look(self):
        """Switch light/dark (and Modern's accent color) when the computer's setting changes."""
        if self.theme() == "system" or self.skin.modern:
            look = self.os_look()
            if look != self.seen_os_look and not self.modals and not self.pending and not self.term:
                self.set_look()
        self.root.after(3000, self.watch_os_look)

    def quit(self):
        if self.term:  # let the terminal view finish up first; it calls back here
            self.term.request("quit")
            return
        while self.modals:
            self.modals[-1].close(None)
        self.save()
        self.root.destroy()

    def status(self, text, right=None):
        if self.window:
            self.window.status(text, right)

    def progress(self, frac):
        if self.window:
            self.window.progress(frac)

    def wheel(self, e):
        """The mouse wheel scrolls whichever scroll area is under the pointer."""
        w = self.root.winfo_containing(e.x_root, e.y_root)
        while w is not None:
            if isinstance(w, (tk.Listbox,)) or w.winfo_class() in ("Treeview",):
                return
            c = self.scrollers.get(str(w))
            if c is not None:
                break
            w = w.master
        else:
            return
        if e.num == 4:
            d = -1
        elif e.num == 5:
            d = 1
        elif MAC:
            d = -e.delta
        else:
            d = -int(e.delta / 120) or (-1 if e.delta > 0 else 1)
        try:
            top, bottom = c.yview()
            if (d < 0 and top <= 0) or (d > 0 and bottom >= 1):
                return
            c.yview_scroll(d * 3, "units")
        except tk.TclError:
            pass

    # ------------------------------------------------------------ menus and keys
    def build_menu(self):
        sk = self.skin
        m = getattr(self, "menubar", None)
        fresh = m is None
        if fresh:
            m = tk.Menu(self.root)
        else:  # restyle the same menu bar, so it never blinks out when the look changes
            m.delete(0, "end")
            for sub in m.winfo_children():
                sub.destroy()
        W.skin_menu(m, sk)
        if not MAC:
            m.configure(relief="flat", bd=0, activeborderwidth=0)

        def menu(label):
            sub = tk.Menu(m, tearoff=False)
            W.skin_menu(sub, sk)
            m.add_cascade(label=label, menu=sub, underline=0)
            return sub

        f = menu("File")
        f.add_command(label="New Lesson", accelerator=f"{MOD_LABEL}+N", command=self.cmd_new_lesson)
        f.add_command(label="New Assessment…", accelerator=f"{MOD_LABEL}+Shift+N", command=self.cmd_new_assessment)
        self.recent_menu = tk.Menu(f, tearoff=False, postcommand=self.fill_recent_menu)
        W.skin_menu(self.recent_menu, sk)
        f.add_cascade(label="Open Recent", menu=self.recent_menu)
        f.add_separator()
        f.add_command(label="Import Standards…", command=self.cmd_import)
        f.add_command(label="Open Export Folder", command=self.open_export_folder)
        f.add_command(label="Export Everything…", command=lambda: self.top_level(self.export_everything))
        f.add_separator()
        f.add_command(label="Back Up Everything…", command=lambda: self.backup("backup_now"))
        f.add_command(label="Import Backup…", command=lambda: self.backup("import_backup"))
        f.add_separator()
        f.add_command(label="Close Window", accelerator=f"{MOD_LABEL}+W", command=self.back)
        f.add_command(label="Quit", accelerator=f"{MOD_LABEL}+Q", command=self.quit)

        e = menu("Edit")
        for label, ev, key in (("Undo", "<<Undo>>", "Z"), ("Redo", "<<Redo>>", "Shift+Z"), (None, None, None),
                               ("Cut", "<<Cut>>", "X"), ("Copy", "<<Copy>>", "C"), ("Paste", "<<Paste>>", "V"),
                               ("Select All", "<<SelectAll>>", "A")):
            if label is None:
                e.add_separator()
            else:
                e.add_command(label=label, accelerator=f"{MOD_LABEL}+{key}", command=lambda ev=ev: self.edit(ev))

        g = menu("Go")
        g.add_command(label="Chalkboard (Home)", accelerator=f"{MOD_LABEL}+0", command=lambda: self.go_section(None))
        for i, (key, label) in enumerate(SECTIONS, 1):
            g.add_command(label=label, accelerator=f"{MOD_LABEL}+{i}", command=lambda k=key: self.go_section(k))

        v = menu("View")
        v.add_command(label="Switch to Terminal View", accelerator=f"{MOD_LABEL}+Shift+W",
                      command=lambda: self.top_level(self.enter_terminal))
        v.add_separator()
        self.skin_var = tk.StringVar(value=sk.kind)
        for key, s in SKINS.items():
            v.add_radiobutton(label=f"{s['name']} Look", value=key, variable=self.skin_var,
                              command=lambda: self.set_look(gui_skin=self.skin_var.get()))
        v.add_separator()
        self.theme_var = tk.StringVar(value=self.theme())
        for key, label in (("system", "Light or Dark: Match My Computer"), ("light", "Light Mode"),
                           ("dark", "Dark Mode")):
            v.add_radiobutton(label=label, value=key, variable=self.theme_var,
                              command=lambda: self.set_look(gui_theme=self.theme_var.get()))
        v.add_separator()
        v.add_command(label="Bigger Text", accelerator=f"{MOD_LABEL}+=", command=lambda: self.zoom(1))
        v.add_command(label="Smaller Text", accelerator=f"{MOD_LABEL}+-", command=lambda: self.zoom(-1))
        v.add_separator()
        v.add_command(label="Fill the Screen / Restore", command=self.toggle_zoom)

        h = menu("Help")
        h.add_command(label="Keyboard Shortcuts", command=self.shortcuts)
        h.add_command(label="Show Data File", command=lambda: open_path(os.path.dirname(self.store.path)))
        h.add_separator()
        h.add_command(label=f"About {APP}…", command=self.about)
        if fresh:
            self.root.config(menu=m)
        self.menubar = m

    def edit(self, ev):
        w = self.root.focus_get()
        if w is not None:
            w.event_generate(ev)

    def bind_keys(self):
        r = self.root
        keys = {"n": "cmd_new", "o": "cmd_open", "p": "cmd_preview", "e": "cmd_export", "f": "cmd_find",
                "d": "cmd_duplicate"}
        for k, name in keys.items():
            r.bind_all(f"<{MOD}-{k}>", lambda e, n=name: self.dispatch(n, e))
        r.bind_all(f"<{MOD}-Shift-N>", lambda e: self.top_level(self.cmd_new_assessment))
        for k in ("W", "w"):
            r.bind_all(f"<{MOD}-Shift-{k}>", lambda e: self.top_level(self.enter_terminal))
        r.bind_all(f"<{MOD}-w>", lambda e: self.top_level(self.back))
        r.bind_all(f"<{MOD}-q>", lambda e: self.quit())
        r.bind_all(f"<{MOD}-equal>", lambda e: self.top_level(lambda: self.zoom(1)))
        r.bind_all(f"<{MOD}-plus>", lambda e: self.top_level(lambda: self.zoom(1)))
        r.bind_all(f"<{MOD}-minus>", lambda e: self.top_level(lambda: self.zoom(-1)))
        r.bind_all(f"<{MOD}-Key-0>", lambda e: self.top_level(lambda: self.go_section(None)))
        for i, (key, _) in enumerate(SECTIONS, 1):
            r.bind_all(f"<{MOD}-Key-{i}>", lambda e, k=key: self.top_level(lambda: self.go_section(k)))
        r.bind_all("<Escape>", lambda e: self.top_level(self.escape), add="+")

    def top_level(self, fn):
        if not self.modals and not self.term:
            fn()
        return "break"

    def dispatch(self, name, e=None):
        if self.modals or self.term:
            return "break"
        fn = getattr(self.view, name, None)
        if fn is None and name == "cmd_new":
            fn = self.cmd_new_lesson
        if fn:
            fn()
        return "break"

    def escape(self):
        if hasattr(self.view, "escape") and self.view.escape():
            return
        if self.stack:
            self.back()

    def enter_terminal(self, boot=False):
        """Swap the windows for the terminal view (the terminal app, running on this same data)."""
        if self.term or self.modals:
            return
        self.save()
        # "terminal" is remembered only when you quit from it (left_terminal), so a terminal view
        # that hangs can't come back on every launch
        from .term import TermHost
        self.term = TermHost(self)
        self.term_text = self.text_px()
        try:
            self.term.start(boot)
        except Exception as e:  # noqa: BLE001 - never leave the window stuck without its views
            self.term.close()
            self.term = None
            self.settings["gui_mode"] = "window"
            W.alert(self, "Terminal View", f"The terminal view couldn't start:\n{e}", "warn")

    def left_terminal(self, why, crash=None):
        """Called by the terminal view when it closes: back to the windows, or quit."""
        self.term = None
        self.view = None  # its boxes still hold what they showed before; the terminal view may have changed it
        self.mtime = self.file_mtime()
        if why == "quit":
            self.settings["gui_mode"] = "terminal"
            self.quit()
            return
        self.settings["gui_mode"] = "window"
        self.save()
        self.stack, self.trail = [], []
        self.show(Home, push=False)  # what was open may have changed in the terminal view
        if self.text_px() != getattr(self, "term_text", self.text_px()):
            self.set_look()  # the text size changed in the terminal view
        if crash is not None:
            W.alert(self, "Terminal View", f"The terminal view stopped because of an error:\n{crash}", "warn")

    def zoom(self, d):
        i = TEXT_SIZES.index(self.text_px()) + d
        if 0 <= i < len(TEXT_SIZES):
            self.set_look(gui_text=TEXT_SIZES[i])

    def set_look(self, **changes):
        """Change the skin or sizes, then rebuild the screen in place."""
        self.save()
        self.settings.update(changes)
        self.save()
        factory = self.current
        old = self.apply_skin()
        self.view = None
        self.show(factory, push=False)
        self.root.update_idletasks()
        for w in old:
            w.destroy()

    # ------------------------------------------------------------ windows
    def show(self, factory, push=True):
        """Replace the window's contents with factory(gui, body). push=True: the close box comes back here."""
        if self.view is not None:
            self.save()
            if push:
                self.stack.append(self.current)
                self.trail.append(self.current_title)
        sk = self.skin
        if self.window is None:
            self.shadow = tk.Frame(self.desk, bg=sk["dark"]) if sk.kind == "pinstripe" else None
            self.window = W.Window(self.desk, sk, APP, on_close=self.back, on_zoom=self.toggle_zoom,
                                   on_min=self.root.iconify if sk.bevel else None)
            self.place_window()
        else:
            for w in self.window.body.winfo_children():
                w.destroy()
        self.scrollers = {}
        self.current = factory
        self.view = None
        self.window.status("", "")
        body, S = self.window.body, sk.S
        self.crumb = None
        if factory not in (Home, Welcome):
            nav = tk.Frame(body, bg=sk["window"], padx=8 * S, pady=5 * S)
            nav.pack(fill="x")
            W.Button(nav, sk, "◄ Back", self.back).pack(side="left")
            W.Button(nav, sk, "Home", lambda: self.go_section(None)).pack(side="left", padx=(6 * S, 0))
            self.crumb = tk.Label(nav, font=sk.f, bg=sk["window"], fg=sk["dim"], anchor="w")
            self.crumb.pack(side="left", fill="x", expand=True, padx=(14 * S, 0))
            tk.Frame(body, bg=sk["dark"], height=S).pack(fill="x")
        holder = tk.Frame(body, bg=sk["window"])
        holder.pack(fill="both", expand=True)
        self.view = factory(self, holder)
        self.set_title(getattr(self.view, "title", APP))
        self.window.titlebar.on_close = self.back if self.stack else None
        self.window.titlebar.draw()

    def back(self):
        if self.modals:
            return
        if self.stack:
            self.trail.pop()
            self.show(self.stack.pop(), push=False)

    def go_section(self, key):
        """Jump to a top-level section (Home, Lessons, ...), dropping the windows in between."""
        if self.modals:
            return
        self.save()
        self.stack = []
        self.trail = []
        self.view = None
        if key is None:
            self.show(Home, push=False)
            return
        self.show(Home, push=False)
        self.show(section_factory(key))

    def set_title(self, title):
        self.current_title = title
        # the OS title bar names the window in Modern (and screen magnifiers and task switchers show it)
        self.root.title(f"{title} – {APP}" if self.skin.modern and title != APP else APP)
        if self.window:
            self.window.set_title(title)
        if self.crumb is not None:
            self.crumb.configure(text="  ›  ".join(self.trail + [title]))

    def open_item(self, kind, x):
        """Open a lesson or assessment from anywhere (File > Open Recent)."""
        if self.modals:
            return
        if kind == "lesson":
            from .lessons import editor_factory
            self.go_section("lessons")
            self.show(editor_factory(x["id"]))
        else:
            from .assessments import open_factory
            self.go_section("assessments")
            self.show(open_factory(x))

    def fill_recent_menu(self):
        m = self.recent_menu
        m.delete(0, "end")
        d = self.store.data
        items = [("lesson", x) for x in d["lessons"]] + [("assessment", x) for x in d["assessments"]]
        items.sort(key=lambda p: p[1].get("updated") or "", reverse=True)
        for kind, x in items[:10]:
            what = "Lesson" if kind == "lesson" else (x.get("kind") or "Assessment")
            m.add_command(label=f"{x.get('title') or '(untitled)'}   ({what})",
                          command=lambda k=kind, x=x: self.open_item(k, x))
        if not items:
            m.add_command(label="(nothing yet)", state="disabled")

    # ------------------------------------------------------------ commands
    def cmd_new_lesson(self):
        from .lessons import LessonList
        if not isinstance(self.view, LessonList):
            self.go_section("lessons")
        self.view.cmd_new()

    def cmd_new_assessment(self):
        from .assessments import AssessmentList
        if not isinstance(self.view, AssessmentList):
            self.go_section("assessments")
        self.view.cmd_new()

    def cmd_import(self):
        from .standards import StandardsLibrary
        if not isinstance(self.view, StandardsLibrary):
            self.go_section("standards")
        self.view.import_file()

    def backup(self, name):
        from . import settings
        getattr(settings, name)(self)

    def export_everything(self):
        from .export import export_everything_dialog
        export_everything_dialog(self)

    def open_export_folder(self):
        try:
            os.makedirs(self.store.export_dir(), exist_ok=True)
        except OSError:
            pass
        open_path(self.store.export_dir())

    def about(self):
        d = W.Dialog(self, f"About {APP}")
        sk, S = self.skin, self.skin.S
        top = tk.Frame(d.body, bg=sk["window"])
        top.pack(fill="x")
        tk.Label(top, image=sk.icon("logo", 4), bg=sk["window"]).pack(side="left", padx=(0, 16 * S))
        txt = tk.Frame(top, bg=sk["window"])
        txt.pack(side="left", fill="x")
        W.label(txt, sk, APP).pack(anchor="w")
        txt.winfo_children()[-1].configure(font=sk.fbig)
        W.label(txt, sk, f"Lesson planning system, version {__version__}").pack(anchor="w")
        W.label(txt, sk, "Free software under the GNU GPL v3.", dim=True).pack(anchor="w", pady=(2 * S, 0))
        d_ = self.store.data
        info = (f"{plural(len(d_['lessons']), 'lesson plan')}, {plural(len(d_['assessments']), 'assessment')}\n"
                f"{self.store.kas_count:,} standards in {plural(len(self.store.subjects), 'subject')}\n\n"
                f"Your work saves by itself to:\n{self.store.path}\n\n"
                "The terminal version (chalkboard) uses the same file, so you can switch any time.")
        W.label(d.body, sk, info, wrap=480 * S).pack(anchor="w", pady=(14 * S, 0))
        d.buttons([("OK", True)])
        d.run()

    def shortcuts(self):
        d = W.Dialog(self, "Keyboard Shortcuts")
        sk, S = self.skin, self.skin.S
        rows = [("Ctrl+N", "New lesson (or new item in the open list)"), ("Ctrl+Shift+N", "New assessment"),
                ("Ctrl+O / Return", "Open the selected item"), ("Ctrl+D", "Duplicate the selected item"),
                ("Delete", "Delete the selected item"), ("Ctrl+P", "Preview"), ("Ctrl+E", "Export…"),
                ("Ctrl+F", "Search"), ("Ctrl+W / Esc", "Close this window (go back)"),
                ("Ctrl+0 … 4", "Home, Lessons, Assessments, Standards, Settings"),
                ("Ctrl+= / Ctrl+-", "Bigger / smaller text"), ("Tab", "Next field"),
                ("Ctrl+B / Ctrl+I / Ctrl+U", "Bold, italic, or underline the word (or what's selected)"),
                ("Ctrl+Shift+W", "Switch between the window view and the terminal view"),
                ("Ctrl+Q", "Quit (everything is already saved)")]
        grid = tk.Frame(d.body, bg=sk["window"])
        grid.pack(fill="x")
        for i, (k, v) in enumerate(rows):
            k = k.replace("Ctrl", MOD_LABEL)
            W.label(grid, sk, k, bold=True).grid(row=i, column=0, sticky="w", padx=(0, 18 * S), pady=S)
            W.label(grid, sk, v).grid(row=i, column=1, sticky="w", pady=S)
        d.buttons([("OK", True)])
        d.run()

    def preview(self, doc, title=None):
        """A read-only look at a document, the way the plain-text export prints it."""
        sk, S = self.skin, self.skin.S
        d = W.Dialog(self, "Preview: " + (title or doc.get("title") or "Untitled"), size=(0.78, 0.86))
        page = tk.Frame(d.body, bg=sk["dark"], padx=S, pady=S)
        page.pack(fill="both", expand=True)
        t = tk.Text(page, wrap="none", font=sk.fmono, bg=sk["field"], fg=sk["text"], relief="flat", bd=0,
                    padx=24 * S, pady=18 * S, highlightthickness=0, selectbackground=sk["sel"],
                    selectforeground=sk["seltext"])
        sb = ttk.Scrollbar(page, orient="vertical", command=t.yview)
        t.configure(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y")
        t.pack(fill="both", expand=True)
        t.insert("1.0", "\n".join(render_lines(doc, width=78)))
        t.configure(state="disabled")
        d.buttons([("Close", True)])
        d.run(focus=t)


SECTIONS = [("lessons", "Lesson Plans"), ("assessments", "Assessments"),
            ("standards", "Standards Library"), ("settings", "Settings")]


def section_factory(key):
    if key == "lessons":
        from .lessons import LessonList
        return LessonList
    if key == "assessments":
        from .assessments import AssessmentList
        return AssessmentList
    if key == "standards":
        from .standards import StandardsLibrary
        return StandardsLibrary
    from .settings import SettingsView
    return SettingsView


# ================================================================ welcome & home
class Welcome:
    """A quick hello with the logo, then the main menu. Click or press any key to skip ahead."""

    title = APP

    def __init__(self, gui, parent):
        self.gui, self.done = gui, False
        sk, S = gui.skin, gui.skin.S
        f = self.frame = tk.Frame(parent, bg=sk["field"] if sk.bevel else sk["window"], takefocus=1)
        f.pack(fill="both", expand=True)
        box = tk.Frame(f, bg=f["bg"])
        box.place(relx=0.5, rely=0.44, anchor="center")
        tk.Label(box, image=sk.icon("logo", 10), bg=f["bg"]).pack()
        tk.Label(box, text=APP, font=sk.fhuge, bg=f["bg"], fg=sk["text"]).pack(pady=(10 * S, 0))
        tk.Label(box, text=f"Lesson Planning System   ·   Version {__version__}", font=sk.f, bg=f["bg"],
                 fg=sk["text"]).pack()
        tk.Label(box, text="Click anywhere or press any key to begin.", font=sk.fsmall, bg=f["bg"],
                 fg=sk["dim"]).pack(pady=(22 * S, 0))
        for w in W.descendants(f):
            w.bind("<Button-1>", lambda e: self.go())
        f.bind("<Key>", lambda e: self.go())
        f.after_idle(f.focus_set)
        f.after(2800, self.go)

    def go(self):
        if not self.done and self.frame.winfo_exists() and self.gui.view is self and not self.gui.modals:
            self.done = True
            self.gui.show(Home, push=False)

    def escape(self):
        self.go()
        return True


class Home:
    """The program group: a big icon for each part of Chalkboard."""

    title = APP
    ITEMS = [("lessons", "Lesson Plans", "lessons"), ("assessments", "Assessments", "assessments"),
             ("standards", "Standards", "standards"), ("settings", "Settings", "settings"),
             ("exports", "My Exports", "folder")]

    def __init__(self, gui, parent):
        self.gui = gui
        sk, S = gui.skin, gui.skin.S
        self.frame = f = tk.Frame(parent, bg=sk["field"] if sk.bevel else sk["window"])
        f.pack(fill="both", expand=True)
        box = tk.Frame(f, bg=f["bg"])
        box.place(relx=0.5, rely=0.42, anchor="center")
        grid = tk.Frame(box, bg=f["bg"])
        grid.pack()
        self.cells = []
        for i, (key, text, icon) in enumerate(self.ITEMS):
            cell = tk.Frame(grid, bg=f["bg"], takefocus=1, highlightthickness=0)
            cell.grid(row=0, column=i, padx=18 * S, pady=4 * S, sticky="n")
            img = tk.Label(cell, image=sk.icon(icon, 4), bg=f["bg"])
            img.pack()
            lab = tk.Label(cell, text=text, font=sk.f, bg=f["bg"], fg=sk["text"], padx=3 * S)
            lab.pack(pady=(6 * S, 0))
            for w in (cell, img, lab):
                w.bind("<Button-1>", lambda e, i=i: self.pick(i))
                w.bind("<Double-1>", lambda e, k=key: self.open(k))
            cell.bind("<Return>", lambda e, k=key: self.open(k))
            cell.bind("<KP_Enter>", lambda e, k=key: self.open(k))
            cell.bind("<space>", lambda e, k=key: self.open(k))
            cell.bind("<Left>", lambda e, i=i: self.pick(i - 1))
            cell.bind("<Right>", lambda e, i=i: self.pick(i + 1))
            cell.bind("<FocusIn>", lambda e, i=i: self.mark(i))
            self.cells.append((cell, lab))
        W.label(box, sk, "Double-click an icon (or use the arrow keys and Return) to open it.", dim=True).pack(pady=(26 * S, 0))
        from .buddy import Buddy
        self.buddy = Buddy(f, gui)  # the school mascot, if one is picked: click it for a cheer
        self.buddy.place(relx=1.0, rely=1.0, x=-16 * S, y=-10 * S, anchor="se")
        d = gui.store.data
        gui.status(f"{plural(len(d['lessons']), 'lesson plan')}   ·   {plural(len(d['assessments']), 'assessment')}",
                   f"{gui.store.kas_count:,} standards")
        self.sel = 0
        self.mark(0)
        cell = self.cells[0][0]
        cell.after_idle(cell.focus_set)

    def mark(self, i):
        sk = self.gui.skin
        self.sel = i
        for j, (cell, lab) in enumerate(self.cells):
            on = j == i
            lab.configure(bg=sk["sel"] if on else cell["bg"], fg=sk["seltext"] if on else sk["text"])

    def pick(self, i):
        i = max(0, min(len(self.cells) - 1, i))
        self.mark(i)
        self.cells[i][0].focus_set()

    def open(self, key):
        if key == "exports":
            self.gui.open_export_folder()
            self.gui.status(f"Opened {self.gui.store.export_dir()}")
            return
        self.gui.show(section_factory(key))

    def cmd_open(self):
        self.open(self.ITEMS[self.sel][0])

    def reloaded(self):
        d = self.gui.store.data
        self.gui.status(f"{plural(len(d['lessons']), 'lesson plan')}   ·   {plural(len(d['assessments']), 'assessment')}")


def main():
    ap = argparse.ArgumentParser(prog="chalkboard-gui", description="Chalkboard in a window")
    ap.add_argument("--no-boot", action="store_true", help="skip the startup screen")
    ap.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    ap.add_argument("--no-plugins", action="store_true", help="start without plugins")
    args = ap.parse_args()
    offline.enforce()  # before anything else runs, plugins included
    if tk is None:
        sys.exit("chalkboard-gui needs Tk. On Arch: sudo pacman -S tk  On Debian/Ubuntu: sudo apt install python3-tk")
    if sys.platform == "win32":
        try:  # crisp text on high-DPI screens
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except (AttributeError, OSError):
            pass
    Gui(args).root.mainloop()


if __name__ == "__main__":
    main()
