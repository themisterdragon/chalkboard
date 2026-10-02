"""Terminal view: the terminal Chalkboard running inside the window.

A small stand-in for curses that draws on a Tk canvas: only what chalkboard.ui and
chalkboard.app use. TermHost runs the real terminal app (the same code as the `chalkboard`
command) over the window's data, so the two views can't drift apart, and it needs no
curses package (so it works on Windows too).
"""

import sys
from collections import deque

import tkinter as tk
from tkinter import font as tkfont

from .skin import MAC

MOD = "Command" if MAC else "Control"


class error(Exception):
    pass


class Leave(BaseException):
    """Unwinds the terminal app: why is 'switch' (to the window view) or 'quit'."""

    def __init__(self, why):
        super().__init__(why)
        self.why = why


# the curses names the terminal app uses (values match ncurses)
KEY_DOWN, KEY_UP, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_BACKSPACE = 258, 259, 260, 261, 262, 263
KEY_DC, KEY_NPAGE, KEY_PPAGE, KEY_ENTER, KEY_END, KEY_RESIZE = 330, 338, 339, 343, 360, 410
COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW, COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE = range(8)
A_UNDERLINE, A_REVERSE, A_DIM, A_BOLD, A_ITALIC = 0x20000, 0x40000, 0x100000, 0x200000, 0x80000000
COLORS = COLOR_PAIRS = 256

KEYS = {"Up": KEY_UP, "Down": KEY_DOWN, "Left": KEY_LEFT, "Right": KEY_RIGHT, "Prior": KEY_PPAGE,
        "Next": KEY_NPAGE, "Home": KEY_HOME, "End": KEY_END, "Delete": KEY_DC, "BackSpace": KEY_BACKSPACE,
        "Return": "\n", "KP_Enter": "\n", "Escape": "\x1b", "Tab": "\t", "ISO_Left_Tab": "\t"}
BLOCKS = "█▀▄"
_pairs = {0: (7, 0)}
_screen = None


def xterm(n):
    """An xterm-256 color number as #RRGGBB."""
    if n < 16:
        base = ["000000", "800000", "008000", "808000", "000080", "800080", "008080", "C0C0C0",
                "808080", "FF0000", "00FF00", "FFFF00", "0000FF", "FF00FF", "00FFFF", "FFFFFF"]
        return "#" + base[n]
    if n < 232:
        n -= 16
        v = (0, 95, 135, 175, 215, 255)
        return "#%02X%02X%02X" % (v[n // 36], v[n // 6 % 6], v[n % 6])
    g = 8 + (n - 232) * 10
    return "#%02X%02X%02X" % (g, g, g)


def blend(a, b, t=0.5):
    pa, pb = [int(a[i:i + 2], 16) for i in (1, 3, 5)], [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return "#%02X%02X%02X" % tuple(round(x + (y - x) * t) for x, y in zip(pa, pb))


def start_color():
    pass


def init_pair(n, fg, bg):
    _pairs[n] = (fg, bg)
    if _screen:
        _screen.repaint()


def color_pair(n):
    return n << 8


def curs_set(n):
    if _screen:
        _screen.cursor_on = bool(n)


def update_lines_cols():
    pass


def ungetch(k):
    if _screen:
        _screen.keys.appendleft(chr(k) if isinstance(k, int) and k < 256 else k)


def napms(ms):
    s = _screen
    if s is None:
        return
    done = tk.IntVar(s.c, 0)
    s.c.after(max(1, ms), lambda: done.set(1))
    s.c.wait_variable(done)
    s.check()


class Screen:
    """The curses screen: a grid of (character, attributes) drawn on a canvas, row by row."""

    def __init__(self, canvas, family, px):
        global _screen
        _screen = self
        self.c = canvas
        self.keys = deque()
        self.wake = tk.IntVar(canvas, 0)
        self.delay = -1            # ms to wait for a key; -1 waits forever, 0 never waits
        self.leaving = None
        self.live = False          # set once the app is running; before that a resize is no keypress
        self.cursor_on = False
        self.cy = self.cx = 0
        self.bk = 0
        self.rows = self.cols = 0
        self.cells, self.shown = [], []
        self.family = family
        self.set_font(px)
        self.sel = None            # ((row, col), (row, col)) while text is selected with the mouse
        canvas.bind("<Configure>", lambda e: self.reflow())
        canvas.bind("<Key>", self.key)
        canvas.bind("<Button-1>", self.press)
        canvas.bind("<B1-Motion>", self.drag)
        canvas.bind("<ButtonRelease-1>", lambda e: self.copy())
        canvas.bind("<Button-2>", lambda e: self.paste("PRIMARY"))  # middle click, like a Linux terminal
        self.reflow()

    # -- geometry
    def set_font(self, px):
        self.fonts = {}
        self.px = px
        f = self.font(0)
        self.cw = max(1, f.measure("0"))
        self.ch = max(1, f.metrics("linespace"))
        self.reflow(force=True)

    def font(self, attr):
        key = (bool(attr & A_BOLD), bool(attr & A_ITALIC), bool(attr & A_UNDERLINE))
        if key not in self.fonts:
            self.fonts[key] = tkfont.Font(self.c, family=self.family, size=-self.px,
                                          weight="bold" if key[0] else "normal",
                                          slant="italic" if key[1] else "roman", underline=key[2])
        return self.fonts[key]

    def reflow(self, force=False):
        w, h = max(self.c.winfo_width(), 2), max(self.c.winfo_height(), 2)
        pad = max(2, self.cw // 2)  # a terminal's small inner margin; the grid starts top left
        rows, cols = max(1, (h - pad) // self.ch), max(1, (w - pad) // self.cw)
        self.ox = self.oy = pad
        if (rows, cols) == (self.rows, self.cols) and not force:
            return
        blank = (" ", self.bk)
        self.cells = [[(self.cells[y][x] if y < self.rows and x < self.cols else blank) for x in range(cols)]
                      for y in range(rows)]
        self.rows, self.cols = rows, cols
        self.repaint()
        if self.live:
            self.keys.append(KEY_RESIZE)
            self.wake.set(1)

    def repaint(self):
        self.c.delete("all")
        self.shown = [None] * self.rows
        self.c.configure(bg=self.colors(self.bk)[1])

    # -- the curses window calls
    def getmaxyx(self):
        return self.rows, self.cols

    def keypad(self, flag):
        pass

    def bkgd(self, ch, attr=0):
        self.bk = attr
        self.repaint()

    def erase(self):
        blank = (" ", self.bk)
        self.cells = [[blank] * self.cols for _ in range(self.rows)]

    def addstr(self, y, x, text, attr=0):
        if not (0 <= y < self.rows and 0 <= x < self.cols):
            raise error("addstr out of range")
        row = self.cells[y]
        for i, c in enumerate(text[: self.cols - x]):
            row[x + i] = (c, attr)

    def move(self, y, x):
        self.cy, self.cx = y, x

    def nodelay(self, flag):
        self.delay = 0 if flag else -1

    def timeout(self, ms):
        self.delay = ms

    def refresh(self):
        for y in range(self.rows):
            row = tuple(self.cells[y])
            if row != self.shown[y]:
                self.draw_row(y, row)
                self.shown[y] = row
        self.c.tag_raise("sel")
        self.c.delete("cursor")
        if self.cursor_on and 0 <= self.cy < self.rows and 0 <= self.cx < self.cols:
            x, y = self.ox + self.cx * self.cw, self.oy + self.cy * self.ch
            fg = self.colors(self.cells[self.cy][self.cx][1])[0]
            self.c.create_rectangle(x, y + self.ch - max(2, self.ch // 7), x + self.cw, y + self.ch,
                                    fill=fg, outline="", tags="cursor")
        self.c.update_idletasks()

    def get_wch(self):
        k = self.next_key()
        if k is None:
            raise error("no input")
        return k

    def getch(self):
        k = self.next_key()
        if k is None:
            return -1
        return ord(k) if isinstance(k, str) else k

    # -- drawing
    def colors(self, attr):
        fg, bg = _pairs.get((attr >> 8) & 0xFF, (7, 0))
        fg, bg = xterm(fg if fg >= 0 else 7), xterm(bg if bg >= 0 else 0)
        if attr & A_REVERSE:
            fg, bg = bg, fg
        if attr & A_DIM:
            fg = blend(fg, bg)
        return fg, bg

    def draw_row(self, y, row):
        c, tag = self.c, f"r{y}"
        c.delete(tag)
        top = self.oy + y * self.ch
        base_bg = self.colors(self.bk)[1]
        x = 0
        while x < len(row):
            attr = row[x][1]
            end = x
            while end < len(row) and row[end][1] == attr:
                end += 1
            fg, bg = self.colors(attr)
            left = self.ox + x * self.cw
            if bg != base_bg:
                c.create_rectangle(left, top, self.ox + end * self.cw, top + self.ch, fill=bg, outline="", tags=tag)
            text = "".join(ch for ch, _ in row[x:end])
            self.draw_text(left, top, text, fg, bg, attr, tag)
            x = end

    def draw_text(self, left, top, text, fg, bg, attr, tag):
        """Plain characters as text; block characters as exact rectangles so pixel art has no gaps."""
        c, cw, ch = self.c, self.cw, self.ch
        i = 0
        while i < len(text):
            k = text[i]
            x = left + i * cw
            if k in BLOCKS:
                j = i
                while j < len(text) and text[j] == k:
                    j += 1
                x2 = left + j * cw
                if k == "█":
                    c.create_rectangle(x, top, x2, top + ch, fill=fg, outline="", tags=tag)
                else:
                    half = top + ch // 2
                    c.create_rectangle(x, top, x2, top + ch, fill=bg, outline="", tags=tag)
                    y1, y2 = (top, half) if k == "▀" else (half, top + ch)
                    c.create_rectangle(x, y1, x2, y2, fill=fg, outline="", tags=tag)
                i = j
                continue
            j = i
            while j < len(text) and text[j] not in BLOCKS and text[j].isascii():
                j += 1
            if j == i:  # one non-ASCII character, placed on its own cell
                j = i + 1
            piece = text[i:j]
            if piece.strip():
                c.create_text(x, top, text=piece, anchor="nw", font=self.font(attr), fill=fg, tags=tag)
            elif attr & A_UNDERLINE:
                c.create_line(x, top + ch - 2, left + j * cw, top + ch - 2, fill=fg, tags=tag)
            i = j

    # -- copy and paste
    def cell(self, e):
        return (min(self.rows - 1, max(0, (e.y - self.oy) // self.ch)),
                min(self.cols - 1, max(0, (e.x - self.ox) // self.cw)))

    def press(self, e):
        self.c.focus_set()
        self.sel = (self.cell(e), self.cell(e))
        self.show_sel()

    def drag(self, e):
        if self.sel:
            self.sel = (self.sel[0], self.cell(e))
            self.show_sel()

    def selected(self):
        """The selected text, line by line like a terminal (trailing spaces dropped)."""
        if not self.sel or self.sel[0] == self.sel[1]:
            return ""
        (r1, c1), (r2, c2) = sorted(self.sel)
        out = []
        for r in range(r1, r2 + 1):
            a = c1 if r == r1 else 0
            b = c2 + 1 if r == r2 else self.cols
            out.append("".join(ch for ch, _ in self.cells[r][a:b]).rstrip())
        return "\n".join(out).replace("█", "").strip("\n")

    def show_sel(self):
        self.c.delete("sel")
        if not self.sel or self.sel[0] == self.sel[1]:
            return
        (r1, c1), (r2, c2) = sorted(self.sel)
        fg = self.colors(self.bk)[0]
        for r in range(r1, r2 + 1):
            a = c1 if r == r1 else 0
            b = c2 + 1 if r == r2 else self.cols
            self.c.create_rectangle(self.ox + a * self.cw, self.oy + r * self.ch, self.ox + b * self.cw,
                                    self.oy + (r + 1) * self.ch, outline="", fill=fg, stipple="gray50", tags="sel")

    def copy(self):
        text = self.selected()
        if text:
            self.c.clipboard_clear()
            self.c.clipboard_append(text)
        return text

    def paste(self, which="CLIPBOARD"):
        try:
            text = self.c.selection_get(selection=which) if which == "PRIMARY" else self.c.clipboard_get()
        except tk.TclError:
            return
        self.clear_sel()
        for ch in text.replace("\r\n", "\n").replace("\r", "\n").replace("\t", "    "):
            if ch == "\n" or ch.isprintable():
                self.keys.append(ch)
        self.wake.set(1)

    def clear_sel(self):
        if self.sel:
            self.sel = None
            self.c.delete("sel")

    # -- input
    def key(self, e):
        ks = e.keysym
        ctrl = bool(e.state & 0x4)
        if ctrl and ks.lower() == "v" or (MAC and e.state & 0x8 and ks.lower() == "v"):
            self.paste()
            return "break"
        if (ctrl or MAC and e.state & 0x8) and ks.lower() == "c" and self.selected():
            self.copy()  # with nothing selected, Ctrl+C goes to the app as usual
            return "break"
        self.clear_sel()
        if ks in KEYS:
            self.keys.append(KEYS[ks])
        elif ctrl and len(ks) == 1 and ks.isalpha():
            self.keys.append(chr(ord(ks.lower()) - 96))
        elif e.char and e.char.isprintable() and not (MAC and e.state & 0x8):
            self.keys.append(e.char)
        else:
            return None
        self.wake.set(1)
        return "break"

    def check(self):
        if self.leaving:
            raise Leave(self.leaving)

    def next_key(self):
        self.check()
        if not self.keys and self.delay != 0:
            timer = self.c.after(self.delay, lambda: self.wake.set(1)) if self.delay > 0 else None
            self.c.wait_variable(self.wake)
            if timer:
                self.c.after_cancel(timer)
            self.check()
        return self.keys.popleft() if self.keys else None


def load_app():
    """Import the terminal app with this module standing in for curses (only for this window)."""
    if "chalkboard.app" in sys.modules:
        return sys.modules["chalkboard.app"]
    saved = sys.modules.get("curses")
    sys.modules["curses"] = sys.modules[__name__]
    try:
        from .. import app
    finally:
        if saved is None:
            sys.modules.pop("curses", None)
        else:
            sys.modules["curses"] = saved
    return app


class Args:
    def __init__(self, no_boot, leave):
        self.no_boot = no_boot
        self.leave = leave


class TermHost:
    """Runs the terminal app in the main window until it switches back or quits."""

    def __init__(self, gui):
        self.gui = gui
        self.screen = None

    def start(self, boot):
        gui = self.gui
        self.canvas = tk.Canvas(gui.root, highlightthickness=0, bd=0, bg="#000000", takefocus=1)
        self.canvas.place(x=0, y=0, relwidth=1, relheight=1)
        tk.Misc.lift(self.canvas)  # (Canvas.lift raises canvas items, not the widget)
        self.build_menu()
        gui.root.update_idletasks()
        self.screen = Screen(self.canvas, self.family(), self.px())
        self.canvas.focus_set()
        self.canvas.bind("<Button-3>", lambda e: self.popup.tk_popup(e.x_root, e.y_root))
        if MAC:
            self.canvas.bind("<Button-2>", lambda e: self.popup.tk_popup(e.x_root, e.y_root))  # right click on a Mac
        for seq, fn in ((f"<{MOD}-Shift-W>", lambda: self.request("switch")),
                        (f"<{MOD}-Shift-w>", lambda: self.request("switch")),
                        (f"<{MOD}-q>", lambda: self.request("quit")),
                        (f"<{MOD}-equal>", lambda: self.zoom(1)), (f"<{MOD}-plus>", lambda: self.zoom(1)),
                        (f"<{MOD}-minus>", lambda: self.zoom(-1))):
            self.canvas.bind(seq, lambda e, fn=fn: (fn(), "break")[1])
        gui.root.after_idle(lambda: self.run(boot))

    def family(self):
        """The computer's own terminal font: Terminal's on a Mac, Windows Terminal's on Windows,
        and the system monospace font (fontconfig's choice) on Linux."""
        root = self.gui.root
        families = set(tkfont.families(root))
        if MAC:
            wanted = ("SF Mono", "Menlo", "Monaco")
        elif sys.platform == "win32":
            wanted = ("Cascadia Mono", "Consolas", "Lucida Console")
        else:
            wanted = (system_monospace(),)
        for f in wanted:
            if f and f in families:
                return f
        return tkfont.nametofont("TkFixedFont", root).actual("family")

    def px(self):
        return self.gui.text_px() * self.gui.skin.S + 3

    def zoom(self, d):
        from .desktop import TEXT_SIZES
        i = TEXT_SIZES.index(self.gui.text_px()) + d
        if 0 <= i < len(TEXT_SIZES):
            self.gui.settings["gui_text"] = TEXT_SIZES[i]
            self.screen.set_font(self.px())

    def build_menu(self):
        gui = self.gui
        m = self.menu = tk.Menu(gui.root)
        sub = tk.Menu(m, tearoff=False)
        m.add_cascade(label="File", menu=sub, underline=0)
        sub.add_command(label="Quit", accelerator=f"{gui_mod_label()}+Q", command=lambda: self.request("quit"))
        sub = tk.Menu(m, tearoff=False)
        m.add_cascade(label="Edit", menu=sub, underline=0)
        sub.add_command(label="Copy", accelerator=f"{gui_mod_label()}+C", command=lambda: self.screen and self.screen.copy())
        sub.add_command(label="Paste", accelerator=f"{gui_mod_label()}+V", command=lambda: self.screen and self.screen.paste())
        self.popup = sub
        sub = tk.Menu(m, tearoff=False)
        m.add_cascade(label="View", menu=sub, underline=0)
        sub.add_command(label="Switch to Window View", accelerator=f"{gui_mod_label()}+Shift+W",
                        command=lambda: self.request("switch"))
        sub.add_separator()
        sub.add_command(label="Bigger Text", accelerator=f"{gui_mod_label()}+=", command=lambda: self.zoom(1))
        sub.add_command(label="Smaller Text", accelerator=f"{gui_mod_label()}+-", command=lambda: self.zoom(-1))
        sub.add_command(label="Fill the Screen / Restore", command=gui.toggle_zoom)
        gui.root.config(menu=m)

    def leave(self, why):
        """For the terminal app's own WINDOW VIEW item."""
        raise Leave(why)

    def request(self, why):
        """From the menu bar, a shortcut, or the window's close box: unwind at the next key wait."""
        if self.screen and self.screen.live:
            self.screen.leaving = why
            self.screen.wake.set(1)
        else:  # not running yet (or it never started): just close
            self.close()
            self.gui.left_terminal(why)

    def close(self):
        global _screen
        _screen = None
        self.screen = None
        gui = self.gui
        gui.root.config(menu=gui.menubar)
        for w in (getattr(self, "canvas", None), getattr(self, "menu", None)):
            if w is not None:
                w.destroy()

    def run(self, boot):
        gui = self.gui
        if self.screen is None:  # closed before it got going
            return
        why, crash = "quit", None
        try:
            app = load_app()
            a = app.App(self.screen, Args(not boot, self.leave), store=gui.store)
            a.ui.animate = True
            self.screen.live = True
            a.run()
        except Leave as e:
            why = e.why
        except Exception as e:  # noqa: BLE001 - keep the window alive and say what happened
            why, crash = "switch", e
        try:
            gui.store.save()
        except OSError:
            pass
        self.close()
        gui.left_terminal(why, crash)


def system_monospace():
    """Linux: the font fontconfig picks for "monospace" (what terminals use by default)."""
    import shutil
    import subprocess
    if not shutil.which("fc-match"):
        return None
    try:
        out = subprocess.run(["fc-match", "-f", "%{family[0]}", "monospace"], capture_output=True, text=True,
                             timeout=3).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None
    return out or None


def gui_mod_label():
    return "Cmd" if MAC else "Ctrl"
