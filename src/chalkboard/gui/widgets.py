"""Chalkboard's own controls, drawn on Tk canvases so every look (and its dark mode) is the same everywhere.

Every control can be reached and used with the keyboard, and shows a clear focus ring when it has
the keyboard focus. Colors come from the Skin, whose palettes all meet WCAG AA contrast.

Everything takes the Skin as its second argument. Dialogs are frames inside the main window
(not separate OS windows), so tiling window managers and native themes leave them alone.
"""

from bisect import bisect_right
import tkinter as tk
from tkinter import ttk

from .skin import MAC, fit
from ..markup import B, I, U, WORD, flags_of, marked, tidy


def bg_of(w):
    try:
        return w.cget("bg")
    except tk.TclError:
        return "#FFFFFF"


def descendants(w):
    yield w
    for c in w.winfo_children():
        yield from descendants(c)


def rounded(c, x0, y0, x1, y1, r, **kw):
    """A rounded rectangle on canvas c."""
    pts = [x0 + r, y0, x1 - r, y0, x1, y0, x1, y0 + r, x1, y1 - r, x1, y1, x1 - r, y1, x0 + r, y1,
           x0, y1, x0, y1 - r, x0, y0 + r, x0, y0]
    return c.create_polygon(pts, smooth=True, **kw)


def bevel_box(c, x0, y0, x1, y1, skin, pressed=False, width=None):
    """A raised (or pressed) 3-D box inside x0..x1 (inclusive pixel edges)."""
    S = skin.S
    t = width or 2 * S
    hi, lo = (skin["shadow"], skin["face"]) if pressed else (skin["light"], skin["shadow"])
    c.create_rectangle(x0, y0, x1, y1, fill=skin["face"], outline="")
    for i in range(t if not pressed else S):
        c.create_line(x0 + i, y1 - i, x0 + i, y0 + i, x1 - i, y0 + i, fill=hi)
        if not pressed:
            c.create_line(x0 + i, y1 - i, x1 - i, y1 - i, x1 - i, y0 + i - 1, fill=lo)


class Button(tk.Canvas):
    """A push button. default=True draws the heavier border Return activates."""

    def __init__(self, parent, skin, text, command=None, default=False, minwidth=0, small=False):
        self.skin, self.text, self.command, self.default = skin, text, command, default
        self.enabled, self.pressed, self.inside = True, False, False
        S = skin.S
        self.font = skin.fsmall if small else (skin.f if skin.modern else skin.fb)
        h = self.font.metrics("linespace") + (8 if small else 12) * S
        w = max(minwidth * S, self.font.measure(text) + (16 if small else 26) * S)
        if skin.modern or (default and not skin.bevel):  # room for the focus ring (or the default ring)
            w, h = w + 6 * S, h + 6 * S
        super().__init__(parent, width=w, height=h, bg=bg_of(parent), highlightthickness=0, bd=0, takefocus=1)
        self.bind("<Configure>", lambda e: self.draw())
        self.bind("<ButtonPress-1>", self.down)
        self.bind("<ButtonRelease-1>", self.up)
        self.bind("<Enter>", lambda e: self.hover(True))
        self.bind("<Leave>", lambda e: self.hover(False))
        self.bind("<FocusIn>", lambda e: self.draw())
        self.bind("<FocusOut>", lambda e: self.draw())
        self.bind("<space>", lambda e: self.invoke())
        self.bind("<Return>", lambda e: (self.invoke(), "break")[1])
        self.bind("<KP_Enter>", lambda e: (self.invoke(), "break")[1])

    def set_enabled(self, on):
        if on != self.enabled:
            self.enabled = on
            self.configure(takefocus=1 if on else 0)
            self.draw()

    def set_text(self, text):
        self.text = text
        self.draw()

    def hover(self, inside):
        self.inside = inside
        if self.pressed or self.skin.modern:
            self.draw()

    def down(self, e):
        if self.enabled:
            self.pressed = True
            self.focus_set()
            self.draw()

    def up(self, e):
        fire = self.pressed and self.inside
        self.pressed = False
        self.draw()
        if fire:
            self.invoke()

    def invoke(self):
        if self.enabled and self.command:
            self.after(1, self.command)  # not after_idle: see TermHost.start

    def draw(self):
        sk, S = self.skin, self.skin.S
        c = self
        c.delete("all")
        w, h = c.winfo_width(), c.winfo_height()
        if w < 4:
            w, h = int(c.cget("width")), int(c.cget("height"))
        down = self.pressed and self.inside
        fg = sk["text"] if self.enabled else sk["off"]
        if sk.modern:
            self.draw_modern(w, h, down)
            return
        if sk.bevel:
            edge = 2 * S if self.default else S
            c.create_rectangle(0, 0, w, h, fill=sk["dark"], outline="")
            # rounded-off corners, like the old buttons
            for x, y in ((0, 0), (w - S, 0), (0, h - S), (w - S, h - S)):
                c.create_rectangle(x, y, x + S, y + S, fill=bg_of(self.master), outline="")
            bevel_box(c, edge, edge, w - edge - 1, h - edge - 1, sk, pressed=down)
            off = S if down else 0
            tx, ty = w // 2 + off, h // 2 + off
            if not self.enabled:
                c.create_text(tx + S, ty + S, text=self.text, font=self.font, fill=sk["light"])
            c.create_text(tx, ty, text=self.text, font=self.font, fill=fg)
        else:
            pad = 3 * S if self.default else 0
            if self.default:
                rounded(c, S, S, w - S - 1, h - S - 1, 9 * S, fill=sk["face"], outline=sk["dark"], width=3 * S)
            rounded(c, pad + S, pad + S, w - pad - S - 1, h - pad - S - 1, 6 * S,
                    fill=sk["dark"] if down else sk["face"], outline=sk["dark"] if self.enabled else sk["off"],
                    width=S)
            tx, ty = w // 2, h // 2
            c.create_text(tx, ty, text=self.text, font=self.font,
                          fill=sk["light"] if down else fg)
        if self.focus_get() is self and self.enabled:
            tw = self.font.measure(self.text) // 2 + 3 * S
            th = self.font.metrics("linespace") // 2 + S
            c.create_rectangle(tx - tw, ty - th, tx + tw, ty + th, outline=sk["focus"], dash=(1, 1))

    def draw_modern(self, w, h, down):
        """A flat rounded button; the default one is filled with the accent color."""
        sk, S, c = self.skin, self.skin.S, self
        r, m = 6 * S, 3 * S
        if not self.enabled:
            fill, edge, fg = sk["window"], sk["shadow"], sk["off"]
        elif self.default:
            fill = sk["sel"]
            if down or self.inside:
                fill = mix_color(fill, "#000000", 0.18 if down else 0.1)
            edge, fg = fill, sk["seltext"]
        else:
            fill = sk["face"]
            if down or self.inside:
                fill = mix_color(fill, sk["text"], 0.16 if down else 0.07)
            edge, fg = sk["edge"], sk["text"]
        if self.focus_get() is self and self.enabled:
            rounded(c, S, S, w - S - 1, h - S - 1, r + 2 * S, fill="", outline=sk["focus"], width=2 * S)
        rounded(c, m, m, w - m - 1, h - m - 1, r, fill=fill, outline=edge, width=S)
        c.create_text(w // 2, h // 2, text=self.text, font=self.font, fill=fg)


def mix_color(a, b, t):
    from .skin import mix
    return mix(a, b, t)


def focus_ring(c, skin, x0, y0, x1, y1, round_=False):
    """The keyboard focus mark: a solid ring in Modern, the classic dotted box in the retro looks."""
    S = skin.S
    if skin.modern:
        if round_:
            c.create_oval(x0 - 3 * S, y0 - 3 * S, x1 + 3 * S, y1 + 3 * S, outline=skin["focus"], width=2 * S)
        else:
            rounded(c, x0 - 3 * S, y0 - 3 * S, x1 + 3 * S, y1 + 3 * S, 5 * S, fill="", outline=skin["focus"],
                    width=2 * S)
    else:
        c.create_rectangle(x0, y0, x1, y1, outline=skin["focus"], dash=(1, 1))


class _Toggle(tk.Canvas):
    """Shared code for Check and Radio: an indicator then a label, all on one canvas."""

    def __init__(self, parent, skin, text, var, wrap=0):
        self.skin, self.text, self.var = skin, text, var
        self.enabled = True
        S = skin.S
        self.box = skin.line - 2 * S
        self.x0 = 4 * S if skin.modern else 0  # room for the focus ring
        self.wrap = wrap
        tw = skin.f.measure(text) if not wrap else min(wrap, skin.f.measure(text))
        lines = 1 if not wrap else max(1, -(-skin.f.measure(text) // wrap))
        super().__init__(parent, width=self.x0 + self.box + 8 * S + tw + 4 * S,
                         height=max(self.box, lines * skin.line) + (8 if skin.modern else 6) * S,
                         bg=bg_of(parent), highlightthickness=0, bd=0, takefocus=1)
        self.trace = var.trace_add("write", lambda *a: self.draw())
        self.bind("<Destroy>", lambda e: self._untrace() if e.widget is self else None)
        self.bind("<Configure>", lambda e: self.draw())
        self.bind("<ButtonRelease-1>", lambda e: self.click())
        self.bind("<space>", lambda e: self.click())
        self.bind("<FocusIn>", lambda e: self.draw())
        self.bind("<FocusOut>", lambda e: self.draw())

    def _untrace(self):
        try:
            self.var.trace_remove("write", self.trace)
        except tk.TclError:
            pass

    def set_enabled(self, on):
        self.enabled = on
        self.configure(takefocus=1 if on else 0)
        self.draw()

    def draw(self):
        sk, S = self.skin, self.skin.S
        c = self
        try:
            c.delete("all")
        except tk.TclError:
            return
        b = self.box
        y0 = 4 * S if sk.modern else 3 * S
        x = self.x0
        self.indicator(c, x, y0, b)
        fg = sk["text"] if self.enabled else sk["off"]
        t = c.create_text(x + b + 6 * S, y0 + b // 2 - sk.line // 2, text=self.text, font=sk.f, fill=fg,
                          anchor="nw", width=self.wrap or 0)
        if self.focus_get() is self:
            if sk.modern:
                focus_ring(c, sk, x + S, y0 + S, x + b - S, y0 + b - S, round_=isinstance(self, Radio))
            else:
                x0, y0b, x1, y1 = c.bbox(t)
                focus_ring(c, sk, x0 - S, y0b, x1 + S, y1)


class Check(_Toggle):
    def click(self):
        if self.enabled:
            self.var.set(not self.var.get())

    def indicator(self, c, x, y, b):
        sk, S = self.skin, self.skin.S
        on = bool(self.var.get())
        if sk.modern:
            fill = sk["sel"] if on and self.enabled else sk["field"]
            rounded(c, x, y, x + b, y + b, 4 * S, fill=fill, outline=sk["sel"] if on else sk["edge"], width=S)
            if on:  # a check mark
                c.create_line(x + b * 0.24, y + b * 0.52, x + b * 0.43, y + b * 0.70, x + b * 0.78, y + b * 0.30,
                              fill=sk["seltext"] if self.enabled else sk["off"], width=2 * S, capstyle="round",
                              joinstyle="round")
            return
        c.create_rectangle(x, y, x + b, y + b, fill=sk["field"], outline=sk["edge"], width=S)
        if on:
            m = 3 * S
            c.create_line(x + m, y + m, x + b - m + S, y + b - m + S, fill=sk["text"], width=S + 1)
            c.create_line(x + m, y + b - m, x + b - m + S, y + m - S, fill=sk["text"], width=S + 1)


class Radio(_Toggle):
    def __init__(self, parent, skin, text, var, value, wrap=0):
        self.value = value
        super().__init__(parent, skin, text, var, wrap)

    def click(self):
        if self.enabled:
            self.var.set(self.value)

    def indicator(self, c, x, y, b):
        sk, S = self.skin, self.skin.S
        on = self.var.get() == self.value
        if sk.modern:
            c.create_oval(x, y, x + b, y + b, fill=sk["sel"] if on else sk["field"],
                          outline=sk["sel"] if on else sk["edge"], width=S)
            if on:
                m = b // 3
                c.create_oval(x + m, y + m, x + b - m, y + b - m, fill=sk["seltext"], outline="")
            return
        c.create_oval(x, y, x + b, y + b, fill=sk["field"], outline=sk["edge"], width=S)
        if on:
            m = b // 4 + S
            c.create_oval(x + m, y + m, x + b - m, y + b - m, fill=sk["text"], outline=sk["text"])


class Dropdown(tk.Canvas):
    """A pop-up list: shows the current choice; click (or Space) to pick another.

    options: [(value, label)]. var holds the value. command(value) runs after a change.
    """

    def __init__(self, parent, skin, options, var, command=None, width=0):
        self.skin, self.var, self.command = skin, var, command
        self.options = list(options)
        S = skin.S
        self.enabled = True
        chars = width or max([8] + [len(l) for _, l in self.options])
        w = min(skin.f.measure("0") * chars, 520 * S) + skin.line + 16 * S
        h = skin.line + 8 * S
        if skin.modern:  # room for the focus ring
            w, h = w + 6 * S, h + 10 * S
        super().__init__(parent, width=w, height=h, bg=bg_of(parent), highlightthickness=0, bd=0,
                         takefocus=1)
        self.trace = var.trace_add("write", lambda *a: self.draw())
        self.bind("<Destroy>", lambda e: self._untrace() if e.widget is self else None)
        self.bind("<Configure>", lambda e: self.draw())
        self.bind("<ButtonPress-1>", lambda e: self.post())
        for k in ("<space>", "<Return>", "<KP_Enter>", "<Alt-Down>"):
            self.bind(k, lambda e: (self.post(), "break")[1])
        self.bind("<Down>", lambda e: (self.step(1), "break")[1])
        self.bind("<Up>", lambda e: (self.step(-1), "break")[1])
        self.bind("<FocusIn>", lambda e: self.draw())
        self.bind("<FocusOut>", lambda e: self.draw())

    def _untrace(self):
        try:
            self.var.trace_remove("write", self.trace)
        except tk.TclError:
            pass

    def set_options(self, options):
        self.options = list(options)
        self.draw()

    def set_enabled(self, on):
        self.enabled = on
        self.draw()

    def label(self):
        v = self.var.get()
        return next((l for k, l in self.options if k == v), str(v))

    def choose(self, value):
        if self.var.get() != value:
            self.var.set(value)
            if self.command:
                self.command(value)

    def step(self, d):
        keys = [k for k, _ in self.options]
        if not keys or not self.enabled:
            return
        i = keys.index(self.var.get()) if self.var.get() in keys else -1
        self.choose(keys[max(0, min(len(keys) - 1, i + d))])

    def post(self):
        if not self.enabled or not self.options:
            return
        self.focus_set()
        m = tk.Menu(self, tearoff=False)
        self.skin_menu(m)
        cur = tk.StringVar(value=str(self.var.get()))
        for k, l in self.options:
            m.add_radiobutton(label=l, value=str(k), variable=cur, command=lambda k=k: self.choose(k))
        popup_menu(m, self.winfo_rootx(), self.winfo_rooty() + self.winfo_height())

    def skin_menu(self, m):
        skin_menu(m, self.skin)

    def draw(self):
        sk, S = self.skin, self.skin.S
        c = self
        try:
            c.delete("all")
        except tk.TclError:
            return
        w, h = c.winfo_width(), c.winfo_height()
        if w < 4:
            w, h = int(c.cget("width")), int(c.cget("height"))
        fg = sk["text"] if self.enabled else sk["off"]
        if sk.modern:
            m = 3 * S
            if self.focus_get() is self:
                rounded(c, S, S, w - S - 1, h - S - 1, 8 * S, fill="", outline=sk["focus"], width=2 * S)
            rounded(c, m, m, w - m - 1, h - m - 1, 6 * S, fill=sk["field"], outline=sk["edge"], width=S)
            ax, ay, a = w - m - (h - 2 * m) // 2, h // 2, max(3 * S, h // 8)
            c.create_line(ax - a, ay - a // 2, ax, ay + a // 2, ax + a, ay - a // 2, fill=fg, width=2 * S,
                          capstyle="round", joinstyle="round")
            text, room = self.label(), w - (h - 2 * m) - 10 * S - m
            while text and sk.f.measure(text) > room:
                text = (text[:-2] + "…").replace("……", "…") if len(text) > 2 else ""
            c.create_text(m + 8 * S, h // 2, text=text, font=sk.f, fill=fg, anchor="w")
            return
        if sk.bevel:
            bw = h
            c.create_rectangle(0, 0, w - bw, h - 1, fill=sk["field"], outline=sk["edge"], width=S)
            c.create_rectangle(w - bw, 0, w - 1, h - 1, fill=sk["dark"], outline="")
            bevel_box(c, w - bw + S, S, w - S - 1, h - S - 1, sk)
            ax, ay, a = w - bw // 2, h // 2, max(3 * S, h // 6)
            c.create_polygon(ax - a, ay - a // 2, ax + a, ay - a // 2, ax, ay + a // 2 + S, fill=fg, outline=fg)
            tx = 5 * S
        else:
            c.create_rectangle(S, S, w - 1, h - 1, fill=sk["edge"], outline="")
            c.create_rectangle(0, 0, w - S - 1, h - S - 1, fill=sk["field"], outline=sk["edge"], width=S)
            ax, ay, a = w - h // 2 - S, h // 2, max(3 * S, h // 6)
            c.create_polygon(ax - a, ay - a // 2, ax + a, ay - a // 2, ax, ay + a // 2 + S, fill=fg, outline=fg)
            bw = h
            tx = 6 * S
        text = self.label()
        room = w - bw - tx - 4 * S
        while text and sk.f.measure(text) > room:
            text = text[:-2] + "…" if len(text) > 2 else ""
            text = text.replace("……", "…")
        t = c.create_text(tx, h // 2, text=text, font=sk.f, fill=fg, anchor="w")
        if self.focus_get() is self:
            x0, y0, x1, y1 = c.bbox(t)
            c.create_rectangle(x0 - 2 * S, y0, x1 + 2 * S, y1, fill=sk["sel"], outline="")
            c.tag_raise(t)
            c.itemconfigure(t, fill=sk["seltext"])


def skin_menu(m, skin):
    if MAC:
        return
    m.configure(bg=skin["menu"], fg=skin["text"], activebackground=skin["sel"], activeforeground=skin["seltext"],
                font=skin.fb if skin.kind == "pinstripe" else skin.f, relief="solid", bd=skin.S, activeborderwidth=0,
                selectcolor=skin["text"], disabledforeground=skin["off"])


def popup_menu(m, x, y):
    """Show a pop-up menu that closes again when the teacher clicks anywhere else (or presses Esc).

    On Windows and the Mac tk_popup waits until the menu closes, so letting go of the grab
    afterwards is right. On Linux it returns at once, and letting go there would leave the menu
    stuck open until something is picked, so the menu keeps its grab.
    """
    try:
        m.tk_popup(x, y)
    finally:
        if m.tk.call("tk", "windowingsystem") != "x11":
            m.grab_release()


def entry(parent, skin, var, width=20, **kw):
    S = skin.S
    e = tk.Entry(parent, textvariable=var, width=width, font=skin.f, bg=skin["field"], fg=skin["text"],
                 relief="flat", bd=(5 if skin.modern else 3) * S, highlightthickness=ring(skin),
                 highlightbackground=skin["edge"], highlightcolor=skin["focus"], insertbackground=skin["text"],
                 insertwidth=max(2, S + 1), selectbackground=skin["sel"], selectforeground=skin["seltext"],
                 disabledbackground=skin["window"], disabledforeground=skin["off"],
                 readonlybackground=skin["window"], **kw)
    return e


def ring(skin):
    """Outline width of text boxes. Modern's is thick enough that its focus color reads as a focus ring."""
    return 2 * skin.S if skin.modern else skin.S


def textbox(parent, skin, height=3, width=40, **kw):
    S = skin.S
    opts = dict(height=height, width=width, wrap="word", font=skin.f, bg=skin["field"], fg=skin["text"],
                relief="flat", bd=0, padx=(6 if skin.modern else 4) * S, pady=(4 if skin.modern else 3) * S,
                highlightthickness=ring(skin), highlightbackground=skin["edge"],
                highlightcolor=skin["focus"], insertbackground=skin["text"], insertwidth=max(2, S + 1),
                selectbackground=skin["sel"], selectforeground=skin["seltext"], spacing1=S, spacing3=S)
    opts.update(kw)
    return RichText(parent, skin, **opts)


_CLIP = {}  # the last thing copied from a text box: {"plain": ..., "marked": ...}
_NAV = {"Left", "Right", "Up", "Down", "Home", "End", "Prior", "Next", "KP_Left", "KP_Right", "KP_Up", "KP_Down",
        "KP_Home", "KP_End", "KP_Prior", "KP_Next"}
_BIT = {"b": B, "i": I, "u": U}


class RichText(tk.Text):
    """A text box that shows **bold**, *italic*, and __underline__ as formatting, without the marks.

    What's saved is the same marked text as everywhere else: get() writes the marks and insert()
    reads them, so callers and the terminal app see no difference. Ctrl-B/I/U (Ctrl-T too, Cmd on
    a Mac) or a FormatBar format the selection, the word at the cursor, or what you type next.
    Undo is its own, since Tk's forgets formatting."""

    focused = None  # the text box last typed in
    listeners = []  # FormatBar refreshers

    def __init__(self, parent, skin, **opts):
        from tkinter import font as tkfont
        opts["undo"] = False
        super().__init__(parent, **opts)
        fonts = getattr(skin, "_rich", None)
        if fonts is None:
            base = tkfont.Font(font=self.cget("font")).actual()
            fonts = skin._rich = {k: tkfont.Font(self, **dict(base, **v)) for k, v in
                                  {"b": {"weight": "bold"}, "i": {"slant": "italic"},
                                   "bi": {"weight": "bold", "slant": "italic"}}.items()}
        for k, f in fonts.items():
            self.tag_configure("f" + k, font=f)
        self.tag_configure("u", underline=True)
        self.tag_raise("sel")
        self.style = None  # bits for what's typed next; None = like the text at the cursor
        self._undo, self._redo, self._last, self._pre = [], [], None, None
        tags = list(self.bindtags())
        tags.insert(tags.index("Text") + 1, "RichPost")
        self.bindtags(tags)
        for seq in ("<KeyPress>", "<<Cut>>", "<ButtonRelease-2>", "<<PasteSelection>>", "<<Clear>>"):
            self.bind(seq, self._before, add="+")
            self.bind_class("RichPost", seq, lambda e: e.widget._after(e))
        mod = "Command" if MAC else "Control"
        for key, kind in (("b", "b"), ("i", "i"), ("u", "u"), ("t", "i")):
            self.bind(f"<{mod}-{key}>", lambda e, k=kind: self.toggle(k))
            if MAC:
                self.bind(f"<Control-{key}>", lambda e, k=kind: self.toggle(k))
        self.bind("<<Paste>>", self._paste)
        self.bind("<<Copy>>", self._remember, add="+")
        self.bind("<<Cut>>", self._remember, add="+")
        self.bind("<<Undo>>", lambda e: self.undo())
        self.bind("<<Redo>>", lambda e: self.redo())
        self.bind("<ButtonPress-1>", self._moved, add="+")
        self.bind("<ButtonRelease-1>", lambda e: self._notify(), add="+")
        self.bind("<KeyRelease>", lambda e: self._notify(), add="+")
        self.bind("<FocusIn>", self._focus, add="+")

    # -- the text as plain characters and B/I/U bits (Tk always keeps a final newline)
    def _off(self, index):
        return len(super().get("1.0", index))

    @staticmethod
    def _lines(plain):
        """Where each line starts, so positions convert without asking Tk for the text again (that made
        typing in a long box take a third of a second a key). None when Tk and Python could count a
        line's characters differently (characters past U+FFFF, like emoji)."""
        if plain and max(plain) > "\uffff":
            return None
        starts = [0]
        at = plain.find("\n")
        while at >= 0:
            starts.append(at + 1)
            at = plain.find("\n", at + 1)
        return starts

    def _fast_off(self, index, starts):
        if starts is None:
            return self._off(index)
        line, col = str(index).split(".")
        return starts[int(line) - 1] + int(col)

    def _ix(self, k, starts):
        if starts is None:
            return f"1.0+{k}c"
        line = bisect_right(starts, k)
        return f"{line}.{k - starts[line - 1]}"

    def _state(self):
        plain = super().get("1.0", "end")
        starts = self._lines(plain)
        fl = [0] * len(plain)
        for bit, tag in ((B, "b"), (I, "i"), (U, "u")):
            r = self.tag_ranges(tag)
            for s, e in zip(r[::2], r[1::2]):
                for k in range(self._fast_off(s, starts), self._fast_off(e, starts)):
                    fl[k] |= bit
        return plain, fl

    def _apply(self, plain, fl):
        """Show (plain, bits), tidied; marks typed into the text turn into formatting."""
        text, fl = tidy(plain[:-1], fl[:-1])
        if text != plain[:-1]:  # take out the marks that became formatting (marks like the cursor follow)
            k = 0
            for ch in text:
                while plain[k] != ch:
                    super().delete(f"1.0+{k}c")
                    plain = plain[:k] + plain[k + 1:]
                k += 1
            while len(plain) - 1 > len(text):
                super().delete(f"1.0+{len(text)}c")
                plain = plain[:len(text)] + plain[len(text) + 1:]
        for tag in ("b", "i", "u", "fb", "fi", "fbi"):
            self.tag_remove(tag, "1.0", "end")
        starts = self._lines(text + "\n")
        k, n = 0, len(text)
        while k < n:
            j = k
            while j < n and fl[j] == fl[k]:
                j += 1
            f = fl[k]
            if f:
                a, b = self._ix(k, starts), self._ix(j, starts)
                for bit, tag in ((B, "b"), (I, "i"), (U, "u")):
                    if f & bit:
                        self.tag_add(tag, a, b)
                face = {B: "fb", I: "fi", B | I: "fbi"}.get(f & (B | I))
                if face:
                    self.tag_add(face, a, b)
            k = j

    def restyle(self):
        self._apply(*self._state())

    # -- what callers see: marked text in, marked text out
    def get(self, index1, index2=None):
        if index2 is None:
            return super().get(index1)
        a, b = self._off(index1), self._off(index2)
        plain, fl = self._state()
        return marked(plain[a:b], fl[a:b])

    def insert(self, index, chars, *args):
        if args:
            return super().insert(index, chars, *args)
        text, bits = flags_of(chars)
        at = self._off(index)
        super().insert(index, text)
        plain, fl = self._state()
        fl[at:at + len(text)] = bits
        self._apply(plain, fl)

    def edit_reset(self):
        super().edit_reset()
        self._undo, self._redo, self._last = [], [], None

    # -- typing
    def context(self):
        """The bits of the text at the cursor: the character before it, or after it at a line's start."""
        plain, fl = self._state()
        p = self._off("insert")
        if p > 0 and plain[p - 1] != "\n":
            return fl[p - 1]
        return fl[p] if p < len(plain) and plain[p] != "\n" else 0

    def _moved(self, e=None):
        self.style, self._last = None, None

    def _focus(self, e=None):
        RichText.focused = self
        self._notify()

    def _before(self, e):
        if e.type == tk.EventType.KeyPress and e.keysym in _NAV:
            self._moved()
            return
        if self.style is None and e.type == tk.EventType.KeyPress and e.char and e.char.isprintable():
            if self.tag_ranges("sel") and self.compare("sel.first", "<=", "insert") and \
                    self.compare("insert", "<=", "sel.last"):
                self.mark_set("insert", "sel.first")
            self.style = self.context()
        self._pre = self._state() + (self._off("insert"),)

    def _after(self, e):
        pre, self._pre = self._pre, None
        if pre is None:
            return
        plain, fl = self._state()
        if plain == pre[0]:
            return
        i = 0
        while i < min(len(plain), len(pre[0])) and plain[i] == pre[0][i]:
            i += 1
        j = 0
        while j < min(len(plain), len(pre[0])) - i and plain[-1 - j] == pre[0][-1 - j]:
            j += 1
        typed = e.type == tk.EventType.KeyPress and bool(e.char) and len(plain) - j > i
        style = self.style if (typed and self.style is not None) else self.context_of(pre, i)
        for k in range(i, len(plain) - j):
            fl[k] = style
        if typed and style & U and self._last == "type":  # keep the underline going across a typed space
            k = i
            while k > 0 and plain[k - 1] in " \t":
                k -= 1
            if k < i and k > 0 and fl[k - 1] & U:
                for x in range(k, i):
                    fl[x] |= U
        kind = "type" if typed else "delete" if len(plain) < len(pre[0]) else "other"
        self._push(pre, kind, boundary=typed and not e.char.strip())
        if kind != "type":
            self.style = None
        self._apply(plain, fl)
        self.edit_modified(True)
        self._notify()

    @staticmethod
    def context_of(pre, at):
        plain, fl = pre[0], pre[1]
        if at > 0 and plain[at - 1] != "\n":
            return fl[at - 1]
        return 0

    def _remember(self, e=None):
        if self.tag_ranges("sel"):
            _CLIP.update(plain=super().get("sel.first", "sel.last"), marked=self.get("sel.first", "sel.last"))

    def _paste(self, e=None):
        if str(self.cget("state")) == "disabled":
            return "break"
        try:
            clip = self.clipboard_get()
        except tk.TclError:
            return "break"
        pre = self._state() + (self._off("insert"),)
        if self.tag_ranges("sel"):
            super().delete("sel.first", "sel.last")
        self.insert("insert", _CLIP["marked"] if clip == _CLIP.get("plain") else clip)
        self._push(pre, "other")
        self.style = None
        self.see("insert")
        self.edit_modified(True)
        self._notify()
        return "break"

    # -- formatting
    def formats(self):
        """The bits the B/I/U buttons show: all of the selection, or what typing would get."""
        if self.tag_ranges("sel"):
            plain, fl = self._state()
            a, b = self._off("sel.first"), self._off("sel.last")
            ks = [k for k in range(a, b) if not plain[k].isspace()]
            return sum(bit for bit in (B, I, U) if ks and all(fl[k] & bit for k in ks))
        return self.style if self.style is not None else self.context()

    def toggle(self, kind):
        if str(self.cget("state")) == "disabled":
            return "break"
        bit = _BIT[kind]
        plain, fl = self._state()
        p = self._off("insert")
        if self.tag_ranges("sel"):
            a, b = self._off("sel.first"), self._off("sel.last")
        elif 0 < p < len(plain) and WORD.match(plain[p - 1]) and WORD.match(plain[p]):  # inside a word
            a = b = p
            while a > 0 and WORD.match(plain[a - 1]):
                a -= 1
            while b < len(plain) and WORD.match(plain[b]):
                b += 1
        else:  # what's typed next
            self.style = (self.style if self.style is not None else self.context()) ^ bit
            self._last = None
            self._notify()
            return "break"
        ks = [k for k in range(a, b) if not plain[k].isspace()]
        on = not all(fl[k] & bit for k in ks) if ks else True
        new = [(f | bit if on else f & ~bit) if a <= k < b else f for k, f in enumerate(fl)]
        if tidy(plain[:-1], new[:-1]) != tidy(plain[:-1], fl[:-1]):
            self._push((plain, fl, p), "format")
            self._apply(plain, new)
            self.edit_modified(True)
        self.style = None
        self._notify()
        return "break"

    # -- undo, in whole words, formatting included
    def _push(self, pre, kind, boundary=False):
        if kind == "type" and self._last == "type" and not boundary:
            return
        snap = (marked(pre[0][:-1], pre[1][:-1]), pre[2])
        if not self._undo or self._undo[-1][0] != snap[0]:
            self._undo = self._undo[-199:] + [snap]
        self._redo, self._last = [], kind

    def _snap(self):
        return self.get("1.0", "end-1c"), self._off("insert")

    def _restore(self, snap):
        super().delete("1.0", "end")
        self.insert("1.0", snap[0])
        self.mark_set("insert", f"1.0+{snap[1]}c")
        self.see("insert")
        self.style, self._last = None, None
        self.edit_modified(True)
        self._notify()

    def undo(self):
        cur = self._snap()
        while self._undo and self._undo[-1][0] == cur[0]:
            self._undo.pop()
        if self._undo and str(self.cget("state")) != "disabled":
            self._redo.append(cur)
            self._restore(self._undo.pop())
        return "break"

    def redo(self):
        if self._redo and str(self.cget("state")) != "disabled":
            self._undo.append(self._snap())
            self._restore(self._redo.pop())
        return "break"

    def _notify(self):
        for fn in list(RichText.listeners):
            fn()


class FormatBar(tk.Frame):
    """B, I, U buttons for the text box last typed in under `scope`. They light up for the formatting
    at the cursor, and never take the keyboard focus from the text."""

    def __init__(self, parent, skin, scope):
        from tkinter import font as tkfont
        super().__init__(parent, bg=bg_of(parent))
        self.scope = scope
        base = tkfont.Font(font=skin.f).actual()
        self.buttons = {}
        for kind, extra, tip in (("b", {"weight": "bold"}, "Bold"), ("i", {"slant": "italic"}, "Italic"),
                                 ("u", {"underline": 1}, "Underline")):
            b = _FormatButton(self, skin, kind.upper(), lambda k=kind: self.press(k))
            b.font = tkfont.Font(self, **dict(base, **extra))
            b.tip = tip
            b.pack(side="left", padx=(0, 2 * skin.S))
            self.buttons[kind] = b
        RichText.listeners.append(self.refresh)
        self.bind("<Destroy>", lambda e: e.widget is self and self.refresh in RichText.listeners and
                  RichText.listeners.remove(self.refresh))
        self.refresh()

    def target(self):
        t = RichText.focused
        try:
            if t is not None and t.winfo_exists() and str(t).startswith(str(self.scope) + "."):
                return t
        except tk.TclError:
            pass
        return None

    def press(self, kind):
        t = self.target()
        if t is not None:
            t.toggle(kind)
            t.focus_set()

    def refresh(self):
        try:
            if not self.winfo_exists():
                return
        except tk.TclError:
            return
        t = self.target()
        bits = t.formats() if t is not None else 0
        for kind, b in self.buttons.items():
            b.set_enabled(t is not None and str(t.cget("state")) != "disabled")
            b.set_latched(bool(bits & _BIT[kind]))


class _FormatButton(Button):
    """A small square toggle button that leaves the focus where it is."""

    def __init__(self, parent, skin, text, command):
        super().__init__(parent, skin, text, command, small=True, minwidth=26)
        self.configure(takefocus=0)
        self.latched = False

    def set_enabled(self, on):
        super().set_enabled(on)
        self.configure(takefocus=0)

    def set_latched(self, on):
        if on != self.latched:
            self.latched = on
            self.draw()

    def down(self, e):
        if self.enabled:
            self.pressed = True
            self.draw()

    def draw(self):
        if getattr(self, "latched", False) and self.enabled and not self.pressed:
            self.pressed, inside = True, self.inside
            self.inside = True
            try:
                super().draw()
            finally:
                self.pressed, self.inside = False, inside
        else:
            super().draw()


def label(parent, skin, text="", bold=False, dim=False, small=False, wrap=0, **kw):
    font = skin.fsmall if small else (skin.fb if bold else skin.f)
    return tk.Label(parent, text=text, font=font, bg=bg_of(parent), fg=skin["dim"] if dim else skin["text"],
                    anchor="w", justify="left", wraplength=wrap, **kw)


def frame(parent, skin, bg=None, **kw):
    return tk.Frame(parent, bg=bg or bg_of(parent), **kw)


def group(parent, skin, title):
    S = skin.S
    if skin.modern:  # a card with its title inside
        return tk.LabelFrame(parent, text=title, font=skin.fb, bg=skin["card"], fg=skin["text"], relief="flat",
                             bd=0, highlightthickness=S, highlightbackground=skin["shadow"],
                             highlightcolor=skin["shadow"], padx=12 * S, pady=8 * S, labelanchor="nw")
    return tk.LabelFrame(parent, text=f" {title} ", font=skin.fb, bg=bg_of(parent), fg=skin["text"],
                         relief="groove" if skin.bevel else "solid", bd=2 * S if skin.bevel else S,
                         padx=8 * S, pady=6 * S, labelanchor="nw")


class ListView(tk.Frame):
    """A bordered list with columns (a ttk Treeview).

    columns: [(key, heading, width, stretch)]. Rows are added with set_rows([(obj, values, image)]).
    """

    def __init__(self, parent, skin, columns, height=10, tree=False, selectmode="browse"):
        S = skin.S
        super().__init__(parent, bg=skin["edge"], padx=S, pady=S)
        self.skin = skin
        self.tv = ttk.Treeview(self, columns=[c[0] for c in columns], height=height, selectmode=selectmode,
                               show=("tree", "headings") if tree else ("headings",))
        if tree:
            self.tv.column("#0", width=40 * S, minwidth=30 * S, stretch=False)
        for key, head, width, stretch in columns:
            self.tv.heading(key, text=head, anchor="w")
            self.tv.column(key, width=width * S, minwidth=30 * S, stretch=stretch, anchor="w")
        self.sb = ttk.Scrollbar(self, orient="vertical", command=self.tv.yview)
        self.tv.configure(yscrollcommand=self.sb.set)
        self.sb.pack(side="right", fill="y")
        self.tv.pack(side="left", fill="both", expand=True)
        self.rows = {}
        self.empty = tk.Label(self.tv, text="", font=skin.f, bg=skin["field"], fg=skin["dim"], justify="center")

    def set_rows(self, rows, keep=None, empty_text=""):
        """rows: [(obj, values)] or [(obj, values, image)]. Keeps obj (or the old selection) selected."""
        tv = self.tv
        keep = keep if keep is not None else self.selected()
        tv.delete(*tv.get_children())
        self.rows = {}
        for r in rows:
            obj, values = r[0], r[1]
            kw = {"image": r[2]} if len(r) > 2 and r[2] is not None else {}
            iid = tv.insert("", "end", values=[str(v) for v in values], **kw)
            self.rows[iid] = obj
        if keep is not None:
            self.select(keep)
        if not tv.selection() and self.rows:
            first = next(iter(self.rows))
            tv.selection_set(first)
            tv.focus(first)
        if self.rows or not empty_text:
            self.empty.place_forget()
        else:
            self.empty.configure(text=empty_text, wraplength=max(200, self.tv.winfo_width() - 40))
            self.empty.place(relx=0.5, rely=0.55 if int(self.tv.cget("height")) < 6 else 0.35, anchor="center")

    def select(self, obj):
        for iid, o in self.rows.items():
            if o is obj:
                self.tv.selection_set(iid)
                self.tv.focus(iid)
                self.tv.see(iid)
                return True
        return False

    def selected(self):
        sel = self.tv.selection()
        return self.rows.get(sel[0]) if sel else None

    def index(self):
        sel = self.tv.selection()
        return self.tv.index(sel[0]) if sel else None

    def on_open(self, fn):
        self.tv.bind("<Double-1>", lambda e: fn() if self.tv.identify_row(e.y) else None)
        self.tv.bind("<Return>", lambda e: (fn(), "break")[1])
        self.tv.bind("<KP_Enter>", lambda e: (fn(), "break")[1])

    def on_select(self, fn):
        self.tv.bind("<<TreeviewSelect>>", lambda e: fn())

    def on_menu(self, fn):
        def popup(e):
            row = self.tv.identify_row(e.y)
            if row:
                self.tv.selection_set(row)
                self.tv.focus(row)
            fn(e)
        self.tv.bind("<Button-3>", popup)
        if MAC:
            self.tv.bind("<Button-2>", popup)
            self.tv.bind("<Control-Button-1>", popup)

    def on_delete(self, fn):
        self.tv.bind("<Delete>", lambda e: fn())
        if MAC:
            self.tv.bind("<BackSpace>", lambda e: fn())

    def focus(self):
        self.tv.focus_set()


class ScrollArea(tk.Frame):
    """A vertically scrolling area. Put children in .inner. The mouse wheel works anywhere over it."""

    def __init__(self, parent, skin, gui, maxwidth=0):
        super().__init__(parent, bg=bg_of(parent))
        self.canvas = c = tk.Canvas(self, bg=bg_of(parent), highlightthickness=0, bd=0)
        self.sb = ttk.Scrollbar(self, orient="vertical", command=c.yview)
        c.configure(yscrollcommand=self.sb.set)
        self.sb.pack(side="right", fill="y")
        c.pack(side="left", fill="both", expand=True)
        self.inner = tk.Frame(c, bg=bg_of(parent))
        win = c.create_window(0, 0, window=self.inner, anchor="nw")

        def fit(e):
            c.itemconfigure(win, width=min(e.width, maxwidth) if maxwidth else e.width)
        c.bind("<Configure>", fit)
        self.inner.bind("<Configure>", lambda e: c.configure(scrollregion=(0, 0, e.width, e.height)))
        gui.scrollers[str(c)] = c

    def see(self, widget):
        """Scroll so widget is visible."""
        c = self.canvas
        self.update_idletasks()
        total = self.inner.winfo_height()
        if total <= 0:
            return
        y = widget.winfo_rooty() - self.inner.winfo_rooty()
        h = widget.winfo_height()
        top = c.canvasy(0)
        view = c.winfo_height()
        if y < top or y + h > top + view:
            c.yview_moveto(max(0, (y - 20) / total))


# ------------------------------------------------------------------ window chrome
class TitleBar(tk.Canvas):
    def __init__(self, parent, skin, title, on_close=None, on_zoom=None, on_min=None, dialog=False):
        self.skin, self.title = skin, title
        self.on_close, self.on_zoom, self.on_min = on_close, on_zoom, on_min
        self.dialog = dialog
        self.active = True
        S = skin.S
        h = skin.line + (14 if skin.modern else 8 if skin.bevel else 6) * S
        super().__init__(parent, height=h, bg=skin["title"], highlightthickness=0, bd=0)
        self.boxes = []
        self.bind("<Configure>", lambda e: self.draw())
        self.bind("<ButtonRelease-1>", self.click)

    def set_title(self, title):
        self.title = title
        self.draw()

    def click(self, e):
        for x0, x1, fn in self.boxes:
            if x0 <= e.x <= x1 and fn:
                fn()
                return

    def draw(self):
        sk, S = self.skin, self.skin.S
        c = self
        c.delete("all")
        w, h = c.winfo_width(), c.winfo_height()
        self.boxes = []
        if sk.modern:  # only dialogs have one; the main window uses the OS's title bar
            c.create_rectangle(0, 0, w, h, fill=sk["title"], outline="")
            c.create_text(14 * S, h // 2, text=self.title, font=sk.ftitle, fill=sk["title_text"], anchor="w")
            if self.on_close:
                cx, cy, a = w - h // 2 - 4 * S, h // 2, max(4 * S, h // 7)
                c.create_line(cx - a, cy - a, cx + a, cy + a, fill=sk["dim"], width=2 * S, capstyle="round")
                c.create_line(cx - a, cy + a, cx + a, cy - a, fill=sk["dim"], width=2 * S, capstyle="round")
                self.boxes.append((w - h - 4 * S, w, self.on_close))
            return
        if sk.bevel:
            c.create_rectangle(0, 0, w, h, fill=sk["title"] if self.active else sk["title_off"], outline="")
            left = 0
            if self.on_close:
                bevel_box(c, 0, 0, h - S - 1, h - 1, sk)
                c.create_rectangle(h // 4, h // 2 - 2 * S, h - h // 4 - S, h // 2 + S, fill=sk["light"],
                                   outline=sk["dark"], width=S)
                c.create_rectangle(h - S, 0, h, h, fill=sk["dark"], outline="")
                self.boxes.append((0, h, self.on_close))
                left = h
            right = w
            for fn, up in ((self.on_zoom, True), (self.on_min, False)):
                if fn:
                    x1 = right
                    x0 = right - h
                    c.create_rectangle(x0, 0, x0 + S, h, fill=sk["dark"], outline="")
                    bevel_box(c, x0 + S, 0, x1 - 1, h - 1, sk)
                    cx, cy, a = (x0 + x1) // 2 + S // 2, h // 2, h // 5
                    pts = (cx - a, cy + a // 2, cx + a, cy + a // 2, cx, cy - a // 2 - S) if up else \
                        (cx - a, cy - a // 2, cx + a, cy - a // 2, cx, cy + a // 2 + S)
                    c.create_polygon(pts, fill=sk["text"], outline=sk["text"])
                    self.boxes.append((x0, x1, fn))
                    right = x0
            c.create_text((left + right) // 2, h // 2, text=self.title, font=sk.ftitle,
                          fill=sk["title_text"] if self.active else sk["title_off_text"])
        else:
            c.create_rectangle(0, 0, w, h, fill=sk["title"], outline="")
            if self.active:
                for y in range(3 * S, h - 3 * S, 2 * S):
                    c.create_line(S, y + S // 2, w - S, y + S // 2, fill=sk["dark"], width=S)
            b = h - 8 * S
            y0 = (h - b) // 2
            if self.on_close and self.active:
                x0 = 8 * S
                c.create_rectangle(x0 - 2 * S, y0 - S, x0 + b + 2 * S, y0 + b + S, fill=sk["title"], outline="")
                c.create_rectangle(x0, y0, x0 + b, y0 + b, fill=sk["title"], outline=sk["dark"], width=S)
                self.boxes.append((0, x0 + b + 4 * S, self.on_close))
            if self.on_zoom and self.active:
                x1 = w - 8 * S
                x0 = x1 - b
                c.create_rectangle(x0 - 2 * S, y0 - S, x1 + 2 * S, y0 + b + S, fill=sk["title"], outline="")
                c.create_rectangle(x0, y0, x1, y0 + b, fill=sk["title"], outline=sk["dark"], width=S)
                c.create_rectangle(x0, y0, x0 + b * 6 // 10, y0 + b * 6 // 10, outline=sk["dark"], width=S)
                self.boxes.append((x0 - 4 * S, w, self.on_zoom))
            tw = sk.ftitle.measure(self.title)
            c.create_rectangle(w // 2 - tw // 2 - 6 * S, 0, w // 2 + tw // 2 + 6 * S, h, fill=sk["title"], outline="")
            c.create_text(w // 2, h // 2, text=self.title, font=sk.ftitle,
                          fill=sk["title_text"] if self.active else sk["title_off_text"])


class Window(tk.Frame):
    """A framed window with a title bar and an optional status line. Put content in .body."""

    def __init__(self, parent, skin, title, on_close=None, on_zoom=None, on_min=None, dialog=False, status=True):
        self._parent = parent
        S = skin.S
        self.skin = skin
        if skin.modern:
            self.build_modern(title, on_close, dialog, status)
            return
        super().__init__(parent, bg=skin["dark"], padx=S, pady=S)
        inner = self
        if skin.bevel:
            # the thick gray sizing border, then a black line
            mid = tk.Frame(self, bg=skin["face"] if not dialog else skin["title"], padx=3 * S, pady=3 * S)
            mid.pack(fill="both", expand=True)
            inner = tk.Frame(mid, bg=skin["dark"], padx=S, pady=S)
            inner.pack(fill="both", expand=True)
        self.titlebar = TitleBar(inner, skin, title, on_close, on_zoom, on_min, dialog)
        self.titlebar.pack(side="top", fill="x")
        tk.Frame(inner, bg=skin["dark"], height=S).pack(side="top", fill="x")
        self.status_var = tk.StringVar()
        self.status_right = tk.StringVar()
        self.statusbar = None
        if status:
            if skin["status_top"]:
                bar = tk.Frame(inner, bg=skin["window"])
                bar.pack(side="top", fill="x")
                tk.Frame(inner, bg=skin["dark"], height=S).pack(side="top", fill="x")
                tk.Frame(inner, bg=skin["window"], height=S).pack(side="top", fill="x")
                tk.Frame(inner, bg=skin["dark"], height=S).pack(side="top", fill="x")
            else:
                bar = tk.Frame(inner, bg=skin["face"], padx=2 * S, pady=2 * S)
                bar.pack(side="bottom", fill="x")
            for var, side in ((self.status_var, "left"), (self.status_right, "right")):
                lb = tk.Label(bar, textvariable=var, font=skin.fsmall if not skin.bevel else skin.f,
                              bg=skin["window"] if not skin.bevel else skin["face"], fg=skin["text"], anchor="w",
                              padx=6 * S, pady=S, relief="sunken" if skin.bevel else "flat", bd=S if skin.bevel else 0)
                lb.pack(side=side, fill="x", expand=side == "left", padx=(0, 2 * S) if side == "left" else 0)
            self.statusbar = bar
        self.body = tk.Frame(inner, bg=skin["window"])
        self.body.pack(fill="both", expand=True)

    def build_modern(self, title, on_close, dialog, status):
        """No drawn frame: the OS's own title bar is above. Dialogs get a thin border and a title row."""
        sk, S = self.skin, self.skin.S
        tk.Frame.__init__(self, self._parent, bg=sk["edge"] if dialog else sk["window"],
                          padx=S if dialog else 0, pady=S if dialog else 0)
        self.titlebar = TitleBar(self, sk, title, on_close, None, None, dialog)
        if dialog:
            self.titlebar.pack(side="top", fill="x")
            tk.Frame(self, bg=sk["shadow"], height=S).pack(side="top", fill="x")
        self.status_var = tk.StringVar()
        self.status_right = tk.StringVar()
        self.statusbar = None
        if status:
            bar = tk.Frame(self, bg=sk["window"], padx=8 * S, pady=3 * S)
            bar.pack(side="bottom", fill="x")
            tk.Frame(self, bg=sk["shadow"], height=S).pack(side="bottom", fill="x")
            for var, side in ((self.status_var, "left"), (self.status_right, "right")):
                tk.Label(bar, textvariable=var, font=sk.fsmall, bg=sk["window"], fg=sk["dim"], anchor="w").pack(
                    side=side, fill="x", expand=side == "left")
            self.statusbar = bar
        self.body = tk.Frame(self, bg=sk["window"])
        self.body.pack(fill="both", expand=True)

    def set_title(self, t):
        self.titlebar.set_title(t)

    def status(self, left=None, right=None):
        if left is not None:
            self.status_var.set(left)
        if right is not None:
            self.status_right.set(right)

    def progress(self, frac):
        """A progress bar in the status bar (0.0-1.0); None hides it."""
        if self.statusbar is None:
            return
        sk, S = self.skin, self.skin.S
        bar = getattr(self, "_progress", None)
        if frac is None:
            if bar:
                bar.pack_forget()
            return
        w, h = 160 * S, 12 * S
        if bar is None:
            bar = self._progress = tk.Canvas(self.statusbar, width=w, height=h, highlightthickness=0, bd=0,
                                             bg=sk["trough"])
            # the fill must stand out 3:1 from its track (WCAG 1.4.11), in every look, light or dark
            fill = fit(sk["sel"], [sk["trough"]], 3.0)
            bar.create_rectangle(0, 0, 0, h, fill=fill, outline="", tags="fill")
            bar.create_rectangle(0, 0, w - 1, h - 1, outline=sk["edge"], tags="edge")
        if not bar.winfo_ismapped():
            bar.pack(side="right", padx=(0, 6 * S))
        bar.coords("fill", 0, 0, round(w * max(0.0, min(1.0, frac))), h)


class Dialog:
    """A modal window inside the main window. Build into .body, add .buttons(), then .run()."""

    def __init__(self, gui, title, size=None):
        self.gui, self.skin = gui, gui.skin
        self.result = None
        S = self.skin.S
        root = gui.root
        self.shadow = None
        if self.skin.kind == "pinstripe":
            self.shadow = tk.Frame(root, bg=self.skin["dark"])
        closable = self.skin.bevel or self.skin.modern
        self.win = Window(root, self.skin, title, on_close=(lambda: self.close(self.cancel_value)) if closable else None,
                          dialog=True, status=False)
        self.body = tk.Frame(self.win.body, bg=self.skin["window"], padx=14 * S, pady=12 * S)
        self.body.pack(fill="both", expand=True)
        self.size = size
        self.default = None
        self.cancel_value = None
        self.drag = None
        self.harvest = None    # fn() run just before an OK-style close, while the widgets still exist
        tb = self.win.titlebar
        tb.bind("<ButtonPress-1>", self.drag_start, add="+")
        tb.bind("<B1-Motion>", self.drag_move)

    def buttons(self, specs, default=0, cancel=None):
        """specs: [(label, value)]. Return picks specs[default]; Esc gives `cancel`."""
        S = self.skin.S
        row = tk.Frame(self.body, bg=self.skin["window"])
        row.pack(side="bottom", anchor="e", pady=(14 * S, 0))
        self.button_row = row
        out = []
        for i, (text, value) in enumerate(specs):
            b = Button(row, self.skin, text, command=lambda v=value: self.close(v), default=i == default, minwidth=76)
            b.pack(side="left", padx=(8 * S, 0))
            out.append(b)
        self.default = out[default] if out else None
        self.cancel_value = cancel
        return out

    def drag_start(self, e):
        self.drag = (e.x_root - self.win.winfo_x(), e.y_root - self.win.winfo_y())

    def drag_move(self, e):
        if self.drag:
            x, y = e.x_root - self.drag[0], e.y_root - self.drag[1]
            self.win.place_configure(x=x, y=y, relx=0, rely=0, anchor="nw")
            if self.shadow:
                self.shadow.place_configure(x=x + self.skin.S * 2, y=y + self.skin.S * 2, relx=0, rely=0, anchor="nw")

    def place(self):
        root = self.gui.root
        root.update_idletasks()
        rw, rh = root.winfo_width(), root.winfo_height()
        if self.size:
            w, h = int(rw * self.size[0]), int(rh * self.size[1])
        else:
            w, h = min(self.win.winfo_reqwidth(), rw - 20), min(self.win.winfo_reqheight(), rh - 20)
        x, y = max(0, (rw - w) // 2), max(0, (rh - h) // 3)
        if self.shadow:
            S2 = 2 * self.skin.S
            self.shadow.place(x=x + S2, y=y + S2, width=w, height=h)
            self.shadow.lift()
        self.win.place(x=x, y=y, width=w, height=h)
        self.win.lift()

    def run(self, focus=None):
        gui = self.gui
        tag = f"dlg{id(self)}"
        for w in descendants(self.win):
            w.bindtags(w.bindtags() + (tag,))
        root = gui.root
        root.bind_class(tag, "<Escape>", lambda e: self.close(self.cancel_value))
        root.bind_class(tag, "<Return>", self.on_return)
        root.bind_class(tag, "<KP_Enter>", self.on_return)
        self.place()
        gui.modals.append(self)
        try:
            self.win.wait_visibility()
            self.win.grab_set()
        except tk.TclError:
            pass
        if self.win.winfo_exists():
            (focus or self.default or self.win).focus_set()
            root.wait_window(self.win)
        gui.modals.remove(self)
        if gui.modals:
            try:
                gui.modals[-1].win.grab_set()
            except tk.TclError:
                pass
        return self.result

    def on_return(self, e):
        if isinstance(e.widget, (tk.Text, Button, Dropdown)):
            return
        if self.default is not None:
            self.default.invoke()

    def close(self, value):
        if not self.win.winfo_exists():
            return
        if value and self.harvest:
            self.harvest()
        self.result = value
        try:
            self.win.grab_release()
        except tk.TclError:
            pass
        if self.shadow:
            self.shadow.destroy()
        self.win.destroy()


def _message(gui, title, message, icon, specs, default=0, cancel=None):
    d = Dialog(gui, title)
    S = d.skin.S
    row = tk.Frame(d.body, bg=d.skin["window"])
    row.pack(fill="both", expand=True)
    tk.Label(row, image=d.skin.icon(icon, 2), bg=d.skin["window"]).pack(side="left", anchor="n", padx=(0, 14 * S))
    label(row, d.skin, message, wrap=420 * S).pack(side="left", fill="both", expand=True)
    d.buttons(specs, default, cancel)
    return d.run()


def alert(gui, title, message, icon="info"):
    _message(gui, title, message, icon, [("OK", True)], 0, True)


def confirm(gui, title, message, yes="OK", no="Cancel", icon="ask"):
    return bool(_message(gui, title, message, icon, [(yes, True), (no, False)], 0, False))


def ask(gui, title, message, value="", width=44):
    d = Dialog(gui, title)
    S = d.skin.S
    label(d.body, d.skin, message, wrap=460 * S).pack(anchor="w")
    var = tk.StringVar(value=value)
    e = entry(d.body, d.skin, var, width=width)
    e.pack(fill="x", pady=(8 * S, 0))
    e.select_range(0, "end")
    e.icursor("end")
    d.buttons([("OK", True), ("Cancel", None)])
    return var.get() if d.run(focus=e) else None


def choose(gui, title, message, options, current=None):
    """Pick one of options [(value, label)] from a list. Returns the value or None."""
    d = Dialog(gui, title)
    S = d.skin.S
    if message:
        label(d.body, d.skin, message, wrap=520 * S).pack(anchor="w", pady=(0, 6 * S))
    lv = ListView(d.body, d.skin, [("name", "", 520, True)], height=min(12, max(4, len(options))))
    lv.tv.configure(show=())
    lv.pack(fill="both", expand=True)
    lv.set_rows([(v, [l]) for v, l in options], keep=current)
    if current is not None:
        for iid, v in lv.rows.items():
            if v == current:
                lv.tv.selection_set(iid)
                lv.tv.see(iid)
    lv.on_open(lambda: d.close(lv.selected()))
    btns = d.buttons([("OK", "ok"), ("Cancel", None)])
    btns[0].command = lambda: d.close(lv.selected())
    r = d.run(focus=lv.tv)
    return r
