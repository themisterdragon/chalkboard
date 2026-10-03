"""The Board Designer: fonts and where things go on a board slide, with a live preview.

Everything here is the same settings the terminal app's Board Designer changes, so a slide
designed in one looks the same from the other.
"""

import os
import random
import threading
import tkinter as tk
from tkinter import font as tkfont

from .. import fontlib
from ..doc import preview_doc
from ..export_png import render_preview
from ..pixelfont import FAMILIES, lettering
from ..store import BOARD_CHOICES, BOARD_SIDES, board_sections, move_board_section
from . import widgets as W

KEYS = ("board_font", "board_head_font", "board_layout", "board_panels", "board_title_align", "board_codes_place",
        "board_big_text", "board_sides", "board_order", "board_sections", "board_std_text")
KIND_TEXT = {"chalkboard": "Chalkboard's own", "standard": "Standard", "system": "On this computer"}


def font_text(name, heading=False):
    if heading and not name:
        return "Same as the text font"
    return fontlib.label(name)


def board_designer(gui):
    """Returns True when the teacher saved a new design."""
    st = gui.settings
    sk, S = gui.skin, gui.skin.S
    draft = {k: (dict(st.get(k) or {}) if k == "board_sides" else list(st.get(k) or []) if k in (
        "board_order", "board_sections") else st.get(k)) for k in KEYS}
    for k, v in (("board_font", ""), ("board_head_font", ""), ("board_big_text", False), ("board_std_text", True)):
        if draft[k] is None:
            draft[k] = v
    d = W.Dialog(gui, "Board Designer", size=(0.94, 0.92))
    d.buttons([("Save", True), ("Cancel", None)])  # first, so the buttons keep their room in a small window
    cols = tk.Frame(d.body, bg=sk["window"])
    cols.pack(side="top", fill="both", expand=True)
    left = tk.Frame(cols, bg=sk["window"])
    left.pack(side="left", fill="y", anchor="n")
    right = tk.Frame(cols, bg=sk["window"])
    right.pack(side="left", fill="both", expand=True, padx=(14 * S, 0))

    # -- the preview
    W.label(right, sk, "Preview", bold=True).pack(anchor="w")
    note = W.label(right, sk, "", dim=True, wrap=560 * S)
    note.pack(anchor="w", pady=(0, 6 * S))
    pv = tk.Canvas(right, bg=sk["field"], highlightthickness=S, highlightbackground=sk["edge"], bd=0)
    pv.pack(fill="both", expand=True)
    state = {"job": None, "busy": False, "again": False, "img": None, "path": None}
    tmp = os.path.join(gui.store.dir, "board-preview.png")

    def merged():
        out = dict(st)
        out.update(draft)
        return out

    def refresh(*_):
        if state["job"]:
            gui.root.after_cancel(state["job"])
        state["job"] = gui.root.after(350, start)

    def start():
        state["job"] = None
        if state["busy"]:
            state["again"] = True
            return
        state["busy"] = True
        note.configure(text="Drawing the preview…")
        box = {}
        doc = preview_doc(gui.store, merged())
        lesson_name = doc["title"]

        def work():
            try:
                box["n"] = render_preview(doc, tmp)
            except Exception as e:  # noqa: BLE001 - shown in the dialog, never a crash
                box["err"] = str(e)

        t = threading.Thread(target=work, daemon=True)
        t.start()

        def check():
            if t.is_alive():
                gui.root.after(80, check)
                return
            state["busy"] = False
            if not pv.winfo_exists():
                return
            if "err" in box:
                note.configure(text="Couldn't draw the preview: " + box["err"][:1] + box["err"][1:].lower())
            else:
                show()
                more = f" It continues onto {box['n'] - 1} more slide{'s' if box['n'] > 2 else ''}." if box["n"] > 1 else ""
                note.configure(text=f"The first slide of “{lesson_name}”.{more}")
            if state["again"]:
                state["again"] = False
                start()
        gui.root.after(80, check)

    def show(*_):
        if not os.path.exists(tmp):
            return
        try:
            full = tk.PhotoImage(file=tmp)
        except tk.TclError:
            note.configure(text="This copy of Tk can't show PNG pictures, but exported slides are fine.")
            return
        w, h = max(1, pv.winfo_width() - 8), max(1, pv.winfo_height() - 8)
        fit = min(w / full.width(), h / full.height(), 1.0)
        # Tk scales pictures by whole numbers, so zoom by a then shrink by b for the closest a/b that fits
        a, b = max(((a, b) for a in (1, 2, 3) for b in range(1, 13) if a / b <= fit), key=lambda x: x[0] / x[1],
                   default=(1, 12))
        img = full.zoom(a, a) if a > 1 else full
        img = img.subsample(b, b) if b > 1 else img
        state["img"] = img
        pv.delete("all")
        pv.create_image(pv.winfo_width() // 2, pv.winfo_height() // 2, image=img)
    pv.bind("<Configure>", lambda e: show() if state["img"] else None)

    # -- fonts
    g = W.group(left, sk, "Fonts")
    g.pack(fill="x")
    labels = {}
    for key, title, heading in (("board_font", "Text:", False), ("board_head_font", "Headings and title:", True)):
        row = tk.Frame(g, bg=g["bg"])
        row.pack(fill="x", pady=(0, 4 * S))
        W.label(row, sk, title).pack(anchor="w")
        inner = tk.Frame(row, bg=g["bg"])
        inner.pack(fill="x")
        lab = labels[key] = W.label(inner, sk, font_text(draft[key], heading), bold=True)
        lab.pack(side="left")

        def pick(key=key, heading=heading):
            got = font_picker(gui, draft[key], heading)
            if got is not None:
                draft[key] = got
                labels[key].configure(text=font_text(got, heading))
                refresh()
        W.Button(inner, sk, "Change…", pick, small=True).pack(side="right", padx=(10 * S, 0))
    inst = W.label(g, sk, "", dim=True, wrap=330 * S)
    inst.pack(anchor="w", pady=(4 * S, 0))

    def inst_text():
        inst.configure(text="Chalkboard's fonts are on this computer, so the slideshow shows them in PowerPoint "
                            "and Keynote here." if fontlib.fonts_installed() else
                       "Board pictures always show Chalkboard's fonts. For the slideshow to show them in "
                       "PowerPoint or Keynote, put them on the computer that opens it:")
    inst_text()

    def install():
        try:
            folder = fontlib.install_fonts()
        except OSError as e:
            W.alert(gui, "Chalkboard Fonts", f"Couldn't save the fonts:\n{e}", "warn")
            return
        inst_text()
        W.alert(gui, "Chalkboard Fonts", f"Saved Chalkboard's fonts to:\n{folder}\n\nIf PowerPoint or Keynote is "
                                         "open, quit and reopen it to see them.")
    W.Button(g, sk, "Put Chalkboard's Fonts on This Computer", install, small=True).pack(anchor="w", pady=(4 * S, 0))

    # -- layout
    g = W.group(left, sk, "Layout")
    g.pack(fill="x", pady=(10 * S, 0))
    names = {"board_layout": "Columns:", "board_panels": "Sections look like:", "board_title_align": "Title:",
             "board_codes_place": "Class codes:"}
    grid = tk.Frame(g, bg=g["bg"])
    grid.pack(fill="x")
    for r, (key, title) in enumerate(names.items()):
        W.label(grid, sk, title).grid(row=r, column=0, sticky="w", pady=2 * S, padx=(0, 8 * S))
        v = tk.StringVar(value=draft[key] if draft[key] in BOARD_CHOICES[key] else next(iter(BOARD_CHOICES[key])))
        W.Dropdown(grid, sk, list(BOARD_CHOICES[key].items()), v, width=30).grid(row=r, column=1, sticky="w",
                                                                                pady=2 * S)
        v.trace_add("write", lambda *a, key=key, v=v: (draft.__setitem__(key, v.get()), refresh()))
    big = tk.BooleanVar(value=bool(draft["board_big_text"]))
    W.Check(g, sk, "Extra-big text (a full lesson may use more slides)", big, wrap=320 * S).pack(
        anchor="w", pady=(8 * S, 0))
    big.trace_add("write", lambda *a: (draft.__setitem__("board_big_text", big.get()), refresh()))

    def sections():
        if section_order(gui, draft):
            refresh()
    W.Button(g, sk, "Sections, Sides & Order…", sections, small=True).pack(anchor="w", pady=(10 * S, 0))

    gui.root.after(1, start)
    if not d.run():
        return False
    st.update(draft)
    gui.save()
    gui.status("Saved the board design. Your next board export uses it.")
    return True


def section_order(gui, draft):
    """Which sections show, which side each goes on, and their order. Changes draft; True if changed."""
    sk, S = gui.skin, gui.skin.S
    work = {"board_sides": dict(draft["board_sides"] or {}), "board_order": list(draft["board_order"] or []),
            "board_sections": list(draft["board_sections"] or []), "board_std_text": draft["board_std_text"]}
    d = W.Dialog(gui, "Sections, Sides & Order")
    W.label(d.body, sk, "Sections show in this order, top to bottom. Empty ones are left off each slide. "
                        "“Keep each section on its side” under Columns uses the sides here exactly; Balanced "
                        "moves a section over when that makes the text bigger.", wrap=520 * S).pack(anchor="w")
    lv = W.ListView(d.body, sk, [("name", "Section", 200, True), ("on", "Shown", 70, False),
                                 ("side", "Side", 130, False)], height=10)
    lv.pack(fill="both", expand=True, pady=(8 * S, 0))

    def fill(keep=None):
        on = work["board_sections"]
        rows = [(key, (label, "Yes" if key in on else "No", BOARD_SIDES[col].capitalize()))
                for key, label, col in board_sections(work)]
        lv.set_rows(rows, keep=keep)
        if keep:
            for iid, o in lv.rows.items():
                if o == keep:
                    lv.tv.selection_set(iid)
                    lv.tv.focus(iid)

    def act(what):
        key = lv.selected()
        if not key:
            return
        side = dict((k, c) for k, _, c in board_sections(work)).get(key)
        if what == "show":
            on = work["board_sections"]
            if key in on:
                on.remove(key)
            else:
                on.append(key)
        elif what == "side" and side in ("left", "right"):
            work["board_sides"][key] = "right" if side == "left" else "left"
        elif what in ("up", "down") and side != "top":
            move_board_section(work, key, -1 if what == "up" else 1)
        fill(key)

    row = tk.Frame(d.body, bg=sk["window"])
    row.pack(anchor="w", pady=(8 * S, 0))
    for text, what in (("Show / Hide", "show"), ("Switch Side", "side"), ("Move Up", "up"), ("Move Down", "down")):
        W.Button(row, sk, text, lambda w=what: act(w), small=True).pack(side="left", padx=(0, 6 * S))

    def reset():
        work["board_sides"], work["board_order"] = {}, []
        fill(lv.selected())
    W.Button(row, sk, "Usual Places", reset, small=True).pack(side="left", padx=(12 * S, 0))
    lv.on_open(lambda: act("show"))
    std = tk.BooleanVar(value=work["board_std_text"] is not False)
    W.Check(d.body, sk, "Show each standard's full text (not just its code)", std).pack(anchor="w", pady=(8 * S, 0))
    fill()
    d.buttons([("OK", True), ("Cancel", None)])
    if not d.run(focus=lv.tv):
        return False
    work["board_std_text"] = std.get()
    draft.update(work)
    return True


# -- picking a font

def font_picker(gui, current, heading=False):
    """A font family name ("" for the standard one / same as text), or None if cancelled."""
    sk, S = gui.skin, gui.skin.S
    d = W.Dialog(gui, "Heading Font" if heading else "Text Font")
    top = tk.Frame(d.body, bg=sk["window"])
    top.pack(fill="x")
    W.label(top, sk, "Find:").pack(side="left")
    q = tk.StringVar()
    e = W.entry(top, sk, q, width=28)
    e.pack(side="left", fill="x", expand=True, padx=(6 * S, 0))
    lv = W.ListView(d.body, sk, [("name", "Font", 260, True), ("kind", "Kind", 140, False)], height=12)
    lv.pack(fill="both", expand=True, pady=(8 * S, 0))
    sample = tk.Canvas(d.body, height=74 * S, bg=sk["field"], highlightthickness=S, highlightbackground=sk["edge"])
    sample.pack(fill="x", pady=(8 * S, 0))
    hint = W.label(d.body, sk, "", dim=True, wrap=460 * S)
    hint.pack(anchor="w", pady=(4 * S, 0))
    first = [("", "Same as the text font" if heading else fontlib.label(""), "")]
    status = W.label(d.body, sk, "Looking for this computer's fonts…", dim=True)
    status.pack(anchor="w")

    def rows():
        found = fontlib.families() if fontlib._found is not None else [(n, "chalkboard") for n in FAMILIES]
        return first + [(n, n, KIND_TEXT[k]) for n, k in found if not (n == "Helvetica" and not heading)]
    every = {"rows": rows()}
    tk_families = set(tkfont.families(gui.root))

    def fill(*_):
        text = q.get().strip().lower()
        rows = [(x[0], (x[1], x[2])) for x in every["rows"] if not text or text in x[1].lower()]
        keep = lv.selected()
        lv.set_rows(rows, keep=keep if keep is not None else current, empty_text="No font has that in its name.")
        for iid, o in lv.rows.items():
            if o == (keep if keep is not None else current):
                lv.tv.selection_set(iid)
                lv.tv.see(iid)
                break
        draw_sample()
    q.trace_add("write", fill)

    def draw_sample():
        name = lv.selected()
        sample.delete("all")
        if name is None:
            return
        text = "The quick brown fox · 123"
        h = int(sample.cget("height"))
        if name in FAMILIES:
            pixel_sample(sample, name, text, 10 * S, h // 2, max(2, 3 * S), sk["text"])
            hint.configure(text=FAMILIES[name].capitalize() + ". Made for Chalkboard.")
            return
        fam = {"": "Helvetica", "Helvetica": "Helvetica", "Times": "Times"}.get(name, name)
        if fam in ("Helvetica", "Times") and fam not in tk_families:  # a look-alike this computer has
            alike = ("Arial", "Liberation Sans", "Nimbus Sans") if fam == "Helvetica" else (
                "Times New Roman", "Liberation Serif", "Nimbus Roman")
            fam = next((f for f in alike if f in tk_families), fam)
        sample.create_text(10 * S, h // 2, text=text, anchor="w", fill=sk["text"],
                           font=(fam, -int(30 * S)))
        hint.configure(text="Works everywhere: Arial in the slideshow." if name in ("", "Helvetica") else
                       "Works everywhere: Times New Roman in the slideshow." if name == "Times" else
                       "The slideshow shows this font on computers that have it." if name else "")
    lv.on_select(draw_sample)

    def scan():
        if fontlib._found is None:
            t = threading.Thread(target=fontlib.system_fonts, daemon=True)
            t.start()

            def check():
                if t.is_alive():
                    gui.root.after(100, check)
                    return
                if not status.winfo_exists():
                    return
                every["rows"] = rows()
                status.configure(text=f"{len(every['rows']) - 1} fonts.")
                fill()
            gui.root.after(100, check)
        else:
            status.configure(text=f"{len(every['rows']) - 1} fonts.")
    fill()
    lv.on_open(lambda: d.close(True))
    d.buttons([("OK", True), ("Cancel", None)])
    gui.root.after(1, scan)
    picked = {}
    d.harvest = lambda: picked.setdefault("v", lv.selected())
    if not d.run(focus=e):
        return None
    return picked.get("v")


def pixel_sample(c, family, text, x, mid, px, color):
    """Draw text in one of Chalkboard's own fonts, pixel by pixel (Tk can't load a font file)."""
    glyphs = lettering()
    top = mid - px * 4
    rnd = random.Random(7)
    for ch in text:
        rows = glyphs.get(ch) or glyphs.get("?")
        for r, line in enumerate(rows):
            for k, v in enumerate(line):
                if v != "#":
                    continue
                x0, y0 = x + k * px, top + r * px
                if family == "Chalkboard Marquee":
                    c.create_oval(x0 + px * 0.1, y0 + px * 0.1, x0 + px * 0.9, y0 + px * 0.9, fill=color, outline="")
                elif family == "Chalkboard Chalk":
                    if rnd.random() < 0.12:
                        continue
                    j = px * 0.15
                    c.create_rectangle(x0 + rnd.random() * j, y0 + rnd.random() * j, x0 + px - rnd.random() * j,
                                       y0 + px - rnd.random() * j, fill=color, outline="")
                else:
                    c.create_rectangle(x0, y0, x0 + px, y0 + px, fill=color, outline="")
        x += (len(rows[0]) + 1) * px
