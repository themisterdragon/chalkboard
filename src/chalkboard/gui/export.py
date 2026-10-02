"""Export: the options dialog, quick exports, and the "files saved" dialog."""

import os
import tkinter as tk
from tkinter import filedialog

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
