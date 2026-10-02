"""Export: the options dialog, quick exports, and the "files saved" dialog."""

import os
import threading
import tkinter as tk
from tkinter import filedialog

from ..images import read_image
from ..exporting import FORMAT_ORDER, ExportError, export, export_folder, open_path
from ..store import BOARD_SECTIONS, BOARD_STYLES, SHEET_KINDS
from . import widgets as W

FORMAT_TEXT = {
    "PDF": "PDF", "DOCX": "Word (.docx, also opens in Google Docs)", "TXT": "Plain text (for Google Classroom)",
    "PNG": "Board slide (1920×1080 PNG for a classroom display)",
    "MAKEUP": "Make-up sheet for absent students (PDF + Word)", "ALL": "All of the above",
}
INCLUDE_TEXT = {"BOTH": "Student copy and answer key", "STUDENT": "Student copy only", "KEY": "Answer key only"}
STYLE_TEXT = {"chalk": "Chalkboard (dark green)", "white": "Whiteboard (white)", "school": "School colors"}
assert set(STYLE_TEXT) == set(BOARD_STYLES)


def run_export(gui, kind, obj, fmt):
    gui.save()
    gui.status("Exporting…")
    gui.root.update_idletasks()
    try:
        files = export(gui.store, kind, obj, fmt)
    except ExportError as e:
        W.alert(gui, "Export Didn't Work", str(e)[:1].upper() + str(e)[1:].lower(), "warn")
        gui.status("")
        return
    export_done(gui, files)


def export_dialog(gui, kind, obj):
    gui.save()
    st = gui.settings
    sk, S = gui.skin, gui.skin.S
    d = W.Dialog(gui, "Export: " + (obj.get("title") or "untitled"))
    fkey = "lesson_export_format" if kind == "lesson" else "export_format"
    formats = FORMAT_ORDER[kind]
    fmt = tk.StringVar(value=st.get(fkey) if st.get(fkey) in formats else "PDF")
    cols = tk.Frame(d.body, bg=sk["window"])
    cols.pack(fill="both", expand=True)
    left = tk.Frame(cols, bg=sk["window"])
    left.pack(side="left", fill="y", anchor="n")
    right = tk.Frame(cols, bg=sk["window"])
    right.pack(side="left", fill="both", expand=True, padx=(14 * S, 0), anchor="n")

    g = W.group(left, sk, "Format")
    g.pack(fill="x", anchor="n")
    for f in formats:
        W.Radio(g, sk, FORMAT_TEXT[f], fmt, f).pack(anchor="w")
    linked = gui.store.attached(obj) if kind == "lesson" else []
    note = W.label(g, sk, "", dim=True, small=True, wrap=330 * S)
    if linked:
        note.pack(anchor="w", pady=(4 * S, 0))

    vars_ = {}
    if kind == "assessment" and obj.get("kind") not in SHEET_KINDS:
        g = W.group(right, sk, "Copies")
        g.pack(fill="x")
        inc = vars_["include"] = tk.StringVar(value=st.get("export_include", "BOTH"))
        for k, t in INCLUDE_TEXT.items():
            W.Radio(g, sk, t, inc, k).pack(anchor="w")
        row = tk.Frame(g, bg=g["bg"])
        row.pack(anchor="w", pady=(6 * S, 0))
        W.label(row, sk, "Versions:").pack(side="left")
        ver = vars_["versions"] = tk.StringVar(value=str(st.get("export_versions", 1)))
        W.Dropdown(row, sk, [(str(n), "1 (as written)" if n == 1 else f"{n} (A–{'ABCD'[n - 1]}, shuffled)")
                             for n in range(1, 5)], ver, width=18).pack(side="left", padx=(6 * S, 0))

    board = W.group(right, sk, "Board Slide")
    style = vars_["board_style"] = tk.StringVar(value=st.get("board_style", "chalk"))
    W.Dropdown(board, sk, list(STYLE_TEXT.items()), style, width=24).pack(anchor="w")
    W.Button(board, sk, "Sections & Colors…", lambda: board_options(gui), small=True).pack(anchor="w", pady=(6 * S, 0))

    paper = W.group(right, sk, "Font & Paper")
    font = vars_["font"] = tk.StringVar(value=st.get("font", "Times"))
    page = vars_["page"] = tk.StringVar(value=st.get("page", "Letter"))
    row = tk.Frame(paper, bg=paper["bg"])
    row.pack(anchor="w")
    for v, t in (("Times", "Times (serif)"), ("Helvetica", "Helvetica (sans)")):
        W.Radio(row, sk, t, font, v).pack(side="left", padx=(0, 12 * S))
    row = tk.Frame(paper, bg=paper["bg"])
    row.pack(anchor="w")
    for v in ("Letter", "A4"):
        W.Radio(row, sk, v, page, v).pack(side="left", padx=(0, 12 * S))

    where = W.group(d.body, sk, "Save To")
    where.pack(fill="x", pady=(10 * S, 0))
    folder = tk.StringVar(value=gui.store.export_dir())
    row = tk.Frame(where, bg=where["bg"])
    row.pack(fill="x")
    W.entry(row, sk, folder, width=46).pack(side="left", fill="x", expand=True)
    W.Button(row, sk, "Browse…", lambda: browse(gui, folder), small=True).pack(side="left", padx=(6 * S, 0))
    sub = W.label(where, sk, "", dim=True, small=True, wrap=560 * S)
    sub.pack(anchor="w", pady=(4 * S, 0))

    def update(*a):
        f = fmt.get()
        board.pack_forget()
        paper.pack_forget()
        if kind == "lesson" and f in ("PNG", "ALL"):
            board.pack(fill="x", pady=(0, 8 * S))
        if f != "PNG":
            paper.pack(fill="x")
        if linked:
            n = len(linked)
            note.configure(text=f"{n} linked worksheet{'s' if n != 1 else ''}: "
                           + ("included (PDF + Word)." if f == "ALL" else "pick All of the above to include them."))
        rel = os.path.relpath(export_folder(gui.store, kind, obj), gui.store.export_dir())
        sub.configure(text=f"Files go in the subfolder {rel} (class, then unit{', then lesson' if kind == 'lesson' else ''}).")
    fmt.trace_add("write", update)
    update()

    d.buttons([("Export", True), ("Cancel", None)])
    if not d.run():
        return
    st[fkey] = fmt.get()
    st["board_style"], st["font"], st["page"] = style.get(), font.get(), page.get()
    if "include" in vars_:
        st["export_include"] = vars_["include"].get()
        st["export_versions"] = int(vars_["versions"].get())
    path = folder.get().strip()
    if path and os.path.abspath(os.path.expanduser(path)) != os.path.abspath(gui.store.export_dir()):
        st["export_dir"] = path
    gui.save()
    run_export(gui, kind, obj, fmt.get())


def browse(gui, var):
    d = filedialog.askdirectory(parent=gui.root, initialdir=var.get(), title="Export folder")
    if d:
        var.set(d)


def export_done(gui, files):
    sk, S = gui.skin, gui.skin.S
    folder = os.path.dirname(files[0])
    n = len(files)
    d = W.Dialog(gui, "Export Complete")
    top = tk.Frame(d.body, bg=sk["window"])
    top.pack(fill="x")
    tk.Label(top, image=sk.icon("folder", 2), bg=sk["window"]).pack(side="left", padx=(0, 12 * S))
    W.label(top, sk, f"Saved {n} file{'s' if n != 1 else ''} to\n{folder}", wrap=520 * S).pack(side="left")
    lv = W.ListView(d.body, sk, [("name", "File", 520, True)], height=min(8, max(3, n)))
    lv.pack(fill="both", expand=True, pady=(10 * S, 0))
    lv.set_rows([(p, [os.path.basename(p)]) for p in files])

    def open_file():
        p = lv.selected()
        if p:
            open_path(p)
    lv.on_open(open_file)
    W.label(d.body, sk, "Double-click a file to open it.", dim=True, small=True).pack(anchor="w", pady=(4 * S, 0))
    btns = d.buttons([("Done", True), ("Open File", "file"), ("Open Folder", "folder")])
    btns[1].command = open_file
    btns[2].command = lambda: open_path(folder)
    gui.status(f"Exported {n} file{'s' if n != 1 else ''}.")
    d.run(focus=lv.tv)


def board_options(gui):
    """Which sections a board slide shows, and the school colors."""
    from ..export_png import low_contrast
    from ..store import parse_hex
    st = gui.settings
    sk, S = gui.skin, gui.skin.S
    d = W.Dialog(gui, "Board Slide Options")
    W.label(d.body, sk, "A 1920×1080 picture for a classroom display. Empty sections are left off, and text sizes itself "
                        "to fit.", wrap=520 * S).pack(anchor="w", pady=(0, 8 * S))
    g = W.group(d.body, sk, "Sections")
    g.pack(fill="x")
    on = list(st.get("board_sections") or [])
    secs = {}
    for i, (key, label, col) in enumerate(BOARD_SECTIONS):
        v = secs[key] = tk.BooleanVar(value=key in on)
        W.Check(g, sk, f"{label}  ({col} side)", v).grid(row=i // 2, column=i % 2, sticky="w", padx=(0, 18 * S))
    stdtext = tk.BooleanVar(value=st.get("board_std_text", True))
    W.Check(g, sk, "Show each standard's full text (not just its code)", stdtext).grid(
        row=9, column=0, columnspan=2, sticky="w", pady=(6 * S, 0))
    extra = tk.Frame(d.body, bg=sk["window"])
    extra.pack(fill="x", pady=(8 * S, 0))
    W.Button(extra, sk, "School Logo…", lambda: logo_options(gui), small=True).pack(side="left")
    W.Button(extra, sk, "Class Periods & Codes…", lambda: (class_periods(gui), secs["class_codes"].set(
        "class_codes" in st.get("board_sections", []))), small=True).pack(side="left", padx=(6 * S, 0))
    cg = W.group(d.body, sk, "School Colors (HEX, like #7A0019)")
    cg.pack(fill="x", pady=(10 * S, 0))
    colors = {}
    for i, (key, label) in enumerate((("primary_color", "Background"), ("secondary_color", "Headings"),
                                      ("text_color", "Text"))):
        W.label(cg, sk, label + ":").grid(row=i, column=0, sticky="w", pady=2 * S)
        v = colors[key] = tk.StringVar(value=st.get(key, ""))
        W.entry(cg, sk, v, width=10).grid(row=i, column=1, sticky="w", padx=6 * S, pady=2 * S)
        sw = tk.Frame(cg, width=28 * S, height=18 * S, bg=parse_hex(v.get()) or cg["bg"],
                      highlightthickness=S, highlightbackground=sk["dark"])
        sw.grid(row=i, column=2, sticky="w")
        v.trace_add("write", lambda *a, v=v, sw=sw: sw.configure(bg=parse_hex(v.get()) or cg["bg"]))
    d.buttons([("OK", True), ("Cancel", None)])
    if not d.run():
        return
    st["board_sections"] = [k for k, _, _ in BOARD_SECTIONS if secs[k].get()]
    st["board_std_text"] = stdtext.get()
    bad = []
    for key, v in colors.items():
        raw = v.get().strip()
        code = parse_hex(raw) if raw else ""
        if code is None:
            bad.append(raw)
        else:
            st[key] = code
    if any(st.get(k) for k in colors) and not bad:
        st["board_style"] = "school"
    gui.save()
    if bad:
        W.alert(gui, "School Colors", f"“{bad[0]}” isn't a HEX color. Use something like #7A0019.", "warn")
    else:
        weak = low_contrast(st) if st.get("board_style") == "school" else []
        if weak:
            W.alert(gui, "School Colors", f"Heads up: the {' and '.join(w.lower() for w in weak)} may be hard to "
                                          "read on that background.", "warn")


def logo_text(gui):
    img = gui.store.logo()
    if not img:
        return "No logo. Board slides show just the title."
    where = "left of the title" if gui.settings.get("logo_place", "left") != "right" else "top right corner"
    return f"{img.get('name') or 'Logo'} ({img['w']}×{img['h']}), {where}"


def logo_options(gui, done=None):
    """The school logo on board slides: pick a PNG or JPEG, where it goes, or remove it."""
    st, sk, S = gui.settings, gui.skin, gui.skin.S
    d = W.Dialog(gui, "School Logo")
    W.label(d.body, sk, "A PNG or JPEG of your school's logo goes in the corner of every board slide. A PNG with a "
                        "transparent background looks best.", wrap=460 * S).pack(anchor="w")
    info = W.label(d.body, sk, logo_text(gui), bold=True, wrap=460 * S)
    info.pack(anchor="w", pady=(10 * S, 6 * S))
    place = tk.StringVar(value=st.get("logo_place", "left"))
    row = tk.Frame(d.body, bg=sk["window"])
    row.pack(anchor="w")
    W.label(row, sk, "Put it:").pack(side="left", padx=(0, 8 * S))
    for v, t in (("left", "Left of the title"), ("right", "Top right corner")):
        W.Radio(row, sk, t, place, v).pack(side="left", padx=(0, 12 * S))

    def changed():
        st["logo_place"] = place.get()
        gui.save()
        info.configure(text=logo_text(gui))
        if done:
            done()
    place.trace_add("write", lambda *a: changed())

    def choose():
        path = filedialog.askopenfilename(parent=gui.root, title="School logo",
                                          filetypes=[("Pictures", "*.png *.jpg *.jpeg *.PNG *.JPG *.JPEG"),
                                                     ("All files", "*")])
        if not path:
            return
        info.configure(text="Reading the picture…")
        box = {}

        def work():
            try:
                box["img"] = read_image(path)
                box["img"]["name"] = os.path.basename(path)
            except ValueError as e:
                box["err"] = str(e)

        t = threading.Thread(target=work, daemon=True)
        t.start()

        def check():
            if t.is_alive():
                gui.root.after(100, check)
                return
            if "err" in box:
                info.configure(text=logo_text(gui))
                W.alert(gui, "School Logo", box["err"][:1] + box["err"][1:].lower(), "warn")
                return
            try:
                gui.store.set_logo(img=box["img"])
            except OSError as e:
                W.alert(gui, "School Logo", f"Couldn't save the logo:\n{e}", "warn")
            changed()
        check()

    def remove():
        if gui.store.logo() and W.confirm(gui, "School Logo", "Take the logo off your board slides?", "Remove"):
            gui.store.remove_logo()
            changed()

    row = tk.Frame(d.body, bg=sk["window"])
    row.pack(anchor="w", pady=(10 * S, 0))
    W.Button(row, sk, "Choose Picture…", choose, small=True).pack(side="left")
    W.Button(row, sk, "Remove", remove, small=True).pack(side="left", padx=(6 * S, 0))
    d.buttons([("Done", True)], cancel=True)
    d.run()


def periods_text(gui):
    ps = [p for p in gui.settings.get("class_periods") or [] if (p.get("codes") or "").strip()]
    if not ps:
        return "No class codes. Each lesson gets one board slide."
    return f"{len(ps)} class period{'s' if len(ps) != 1 else ''} with codes: a board slide for each"


def edit_period(gui, p):
    """Name, course, and codes for one class period; True if saved."""
    sk, S = gui.skin, gui.skin.S
    d = W.Dialog(gui, "Class Period")
    g = tk.Frame(d.body, bg=sk["window"])
    g.pack(fill="x")
    g.columnconfigure(1, weight=1)
    name, course = tk.StringVar(value=p.get("name", "")), tk.StringVar(value=p.get("course", ""))
    W.label(g, sk, "Name:").grid(row=0, column=0, sticky="w", pady=3 * S, padx=(0, 8 * S))
    e = W.entry(g, sk, name, width=24)
    e.grid(row=0, column=1, sticky="ew", pady=3 * S)
    W.label(g, sk, "Course:").grid(row=1, column=0, sticky="w", pady=3 * S, padx=(0, 8 * S))
    W.entry(g, sk, course, width=24).grid(row=1, column=1, sticky="ew", pady=3 * S)
    W.label(d.body, sk, "Leave the course blank to make this period's slide for every lesson, or type a course "
                        "(like English 10) to make it only for that course's lessons.", dim=True, small=True,
            wrap=460 * S).pack(anchor="w", pady=(2 * S, 8 * S))
    W.label(d.body, sk, "Codes, one per line, like  Google Classroom: abc123").pack(anchor="w")
    t = W.textbox(d.body, sk, height=5, width=50)
    t.insert("1.0", p.get("codes", ""))
    t.pack(fill="both", expand=True, pady=(2 * S, 0))
    got = {}
    d.harvest = lambda: got.update(codes=t.get("1.0", "end").strip())
    d.buttons([("OK", True), ("Cancel", None)])
    if not d.run(focus=e):
        return False
    p.update(name=name.get().strip() or "Class", course=course.get().strip(), codes=got["codes"])
    return True


def class_periods(gui, done=None):
    """Settings > Class Periods: each period's codes go on its own copy of the board slide."""
    st, sk, S = gui.settings, gui.skin, gui.skin.S
    periods = st.setdefault("class_periods", [])
    d = W.Dialog(gui, "Class Periods & Codes")
    W.label(d.body, sk, "Teach the same lesson to more than one class? Add each period with its class codes "
                        "(type the app's name yourself, like Google Classroom: abc123). Exporting a board slide "
                        "then makes one slide per period, each with its own codes.", wrap=520 * S).pack(anchor="w")
    lv = W.ListView(d.body, sk, [("name", "Period", 130, False), ("course", "Course", 130, False),
                                 ("codes", "Codes", 260, True)], height=6)
    lv.pack(fill="both", expand=True, pady=(10 * S, 0))

    def refresh(keep=None):
        lv.set_rows([(p, [p.get("name", ""), p.get("course") or "Any", " · ".join(
            x.strip() for x in (p.get("codes") or "").split("\n") if x.strip())]) for p in periods], keep=keep,
            empty_text="No periods yet. Click Add.")
        gui.save()
        if done:
            done()

    def add():
        p = {"name": f"Period {len(periods) + 1}", "course": "", "codes": ""}
        if edit_period(gui, p):
            periods.append(p)
            sections = st.setdefault("board_sections", [])
            if "class_codes" not in sections:
                sections.append("class_codes")  # adding codes means you want them on the slide
            refresh(p)

    def edit():
        p = lv.selected()
        if p is not None and edit_period(gui, p):
            refresh(p)

    def remove():
        p = lv.selected()
        if p is not None and W.confirm(gui, "Remove Period", f"Remove {p.get('name') or 'this period'}?", "Remove"):
            periods.remove(p)
            refresh()

    def move(step):
        p = lv.selected()
        if p is None:
            return
        i = periods.index(p)
        j = i + step
        if 0 <= j < len(periods):
            periods[i], periods[j] = periods[j], periods[i]
            refresh(p)

    lv.on_open(edit)
    lv.on_delete(remove)
    row = tk.Frame(d.body, bg=sk["window"])
    row.pack(anchor="w", pady=(8 * S, 0))
    for text, fn in (("Add…", add), ("Edit…", edit), ("Remove", remove), ("Move Up", lambda: move(-1)),
                     ("Move Down", lambda: move(1))):
        W.Button(row, sk, text, fn, small=True).pack(side="left", padx=(0, 6 * S))
    refresh()
    d.buttons([("Done", True)], cancel=True)
    d.run(focus=lv.tv)
