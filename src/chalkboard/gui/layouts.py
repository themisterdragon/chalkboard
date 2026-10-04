"""Advanced Mode > Page Layouts: name lines, titles, headings, and footers for each kind of printed page.

Every change shows in a preview right away, and Reset to Default puts the page back the way
Chalkboard comes. The terminal app changes the same settings (Settings > PAGE LAYOUTS).
"""

import tkinter as tk
from tkinter import ttk

from ..doc import layout_sample
from ..export_txt import render_lines
from ..store import LAYOUT_CHOICES, PAGE_LAYOUTS, page_layout, set_page_layout
from . import widgets as W


def page_layout_dialog(gui, kind):
    """Returns True when the teacher saved a change."""
    st = gui.settings
    sk, S = gui.skin, gui.skin.S
    name, options = PAGE_LAYOUTS[kind]
    current = page_layout(st, kind)
    d = W.Dialog(gui, "Page Layout: " + name, size=(0.94, 0.92))
    d.buttons([("Save", True), ("Cancel", None)])
    cols = tk.Frame(d.body, bg=sk["window"])
    cols.pack(side="top", fill="both", expand=True)
    left = tk.Frame(cols, bg=sk["window"])
    left.pack(side="left", fill="both", anchor="n")
    right = tk.Frame(cols, bg=sk["window"])
    right.pack(side="left", fill="both", expand=True, padx=(14 * S, 0))

    # -- the options
    area = W.ScrollArea(left, sk, gui)
    area.pack(fill="both", expand=True)
    area.canvas.configure(width=430 * S)
    p = area.inner
    W.label(p, sk, "Leave anything as it is to keep the usual layout. Separate name-line labels with commas, "
                   "like: Name, Date, Period", dim=True, wrap=400 * S).pack(anchor="w", pady=(0, 6 * S))
    vars_, boxes = {}, {}
    for key, label, typ, default in options:
        row = tk.Frame(p, bg=sk["window"])
        row.pack(fill="x", pady=(4 * S, 0), padx=(0, 8 * S))
        if typ == "bool":
            v = vars_[key] = tk.BooleanVar(value=current[key])
            W.Check(row, sk, label, v, wrap=380 * S).pack(anchor="w")
            continue
        W.label(row, sk, label + ":", wrap=400 * S).pack(anchor="w")
        if typ in LAYOUT_CHOICES:
            v = vars_[key] = tk.StringVar(value=current[key])
            W.Dropdown(row, sk, list(LAYOUT_CHOICES[typ].items()), v, width=26).pack(anchor="w")
        elif typ == "num":
            v = vars_[key] = tk.StringVar(value=str(current[key]))
            W.Dropdown(row, sk, [(str(n), "None" if n == 0 else str(n)) for n in range(13)], v, width=6).pack(
                anchor="w")
        elif typ == "text":
            t = boxes[key] = W.textbox(row, sk, height=3, width=36)
            t.insert("1.0", current[key])
            t.edit_reset()
            t.pack(fill="x")
            t.bind("<<Modified>>", lambda e, t=t: (t.edit_modified(False), refresh()))
        else:
            v = vars_[key] = tk.StringVar(value=current[key])
            W.entry(row, sk, v, width=36).pack(fill="x")
    for v in vars_.values():
        v.trace_add("write", lambda *a: refresh())

    def values():
        out = {}
        for key, _, typ, _ in options:
            if key in boxes:
                out[key] = boxes[key].get("1.0", "end-1c").strip()
            elif typ == "num":
                out[key] = int(vars_[key].get() or 0)
            else:
                out[key] = vars_[key].get()
        return out

    def reset():
        if not W.confirm(gui, "Reset to Default?", f"Put the {name} layout back the way Chalkboard comes?",
                         yes="Reset", icon="ask"):
            return
        for key, _, typ, default in options:
            if key in boxes:
                boxes[key].delete("1.0", "end")
                boxes[key].insert("1.0", default)
            else:
                vars_[key].set(str(default) if typ == "num" else default)
        refresh()
    row = tk.Frame(p, bg=sk["window"])
    row.pack(fill="x", pady=(14 * S, 4 * S))
    W.Button(row, sk, "Reset to Default…", reset, small=True).pack(anchor="w")

    # -- the preview: the plain-text export, which follows the same layout as the PDF and Word files
    W.label(right, sk, "Preview", bold=True).pack(anchor="w")
    note = W.label(right, sk, "", dim=True, wrap=560 * S)
    note.pack(anchor="w", pady=(0, 6 * S))
    page = tk.Frame(right, bg=sk["dark"], padx=S, pady=S)
    page.pack(fill="both", expand=True)
    t = tk.Text(page, wrap="none", font=sk.fmono, bg=sk["field"], fg=sk["text"], relief="flat", bd=0,
                padx=18 * S, pady=14 * S, highlightthickness=0, selectbackground=sk["sel"],
                selectforeground=sk["seltext"], width=60)
    sb = ttk.Scrollbar(page, orient="vertical", command=t.yview)
    t.configure(yscrollcommand=sb.set)
    sb.pack(side="right", fill="y")
    t.pack(fill="both", expand=True)
    job = {"id": None}

    def refresh():
        if job["id"]:
            gui.root.after_cancel(job["id"])
        job["id"] = gui.root.after(200, draw)

    def draw():
        job["id"] = None
        if not t.winfo_exists():
            return
        trying = dict(st)
        set_page_layout(trying, kind, values())
        doc = layout_sample(gui.store, kind, trying)
        note.configure(text=f"“{doc['title']}”, the way the text export lays it out. PDF and Word files follow "
                            "the same layout with real fonts and lines.")
        lines = render_lines(doc, width=72)
        top = t.yview()[0]
        t.configure(state="normal")
        t.delete("1.0", "end")
        t.insert("1.0", "\n".join(lines))
        t.configure(state="disabled")
        t.yview_moveto(top)
    draw()

    d.harvest = lambda: d.__dict__.__setitem__("values", values())
    if not d.run():
        return False
    set_page_layout(st, kind, d.values)
    gui.save()
    gui.status(f"Saved the {name} layout. Your next export uses it.")
    return True
