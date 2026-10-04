"""A month calendar for picking days: the Day Slideshow and a lesson's Date(s).

Drawn on a canvas so it matches every look and its dark mode. Keyboard: arrows move a day or a
week, Page Up / Page Down change the month, Home goes to today, Return or Space picks the day
(Shift with a range calendar picks through it). Mouse: click a day; Shift-click picks through it.
"""

import calendar
import datetime
import tkinter as tk

from ..store import fmt_range
from . import widgets as W

WEEK = calendar.Calendar(firstweekday=6)  # Sunday first, like a school calendar
DAY_NAMES = ["Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"]


def add_months(day, n):
    m = day.month - 1 + n
    return datetime.date(day.year + m // 12, m % 12 + 1, 1)


class Calendar(tk.Canvas):
    """on_pick(start, end) runs when a day (or a range) is picked. marks(day) -> True puts a dot
    under days that already have something planned."""

    def __init__(self, parent, skin, start=None, end=None, on_pick=None, marks=None, allow_range=False):
        S = skin.S
        self.skin, self.on_pick, self.marks, self.allow_range = skin, on_pick, marks or (lambda d: False), allow_range
        self.today = datetime.date.today()
        self.start, self.end = start, end or start
        self.cursor = start or self.today
        self.month = self.cursor.replace(day=1)
        self.cell = max(skin.f.measure("00") + 18 * S, skin.line + 14 * S)
        self.head = skin.line + 16 * S
        w, h = self.cell * 7 + 4 * S, self.head + skin.line + 6 * S + self.cell * 6 + 4 * S
        super().__init__(parent, width=w, height=h, bg=W.bg_of(parent), highlightthickness=0, bd=0, takefocus=1)
        self.hits = []
        for seq, fn in (("<Left>", lambda e: self.move(-1)), ("<Right>", lambda e: self.move(1)),
                        ("<Up>", lambda e: self.move(-7)), ("<Down>", lambda e: self.move(7)),
                        ("<Prior>", lambda e: self.page(-1)), ("<Next>", lambda e: self.page(1)),
                        ("<Home>", lambda e: self.go(self.today)),
                        ("<space>", lambda e: self.pick(self.cursor, False)),
                        ("<Return>", lambda e: self.pick(self.cursor, False)),
                        ("<Shift-Return>", lambda e: self.pick(self.cursor, True)),
                        ("<Shift-space>", lambda e: self.pick(self.cursor, True))):
            self.bind(seq, lambda e, fn=fn: (fn(e), "break")[1])
        self.bind("<ButtonPress-1>", lambda e: self.click(e, False))
        self.bind("<Shift-ButtonPress-1>", lambda e: self.click(e, True))
        self.bind("<FocusIn>", lambda e: self.draw())
        self.bind("<FocusOut>", lambda e: self.draw())
        self.draw()

    # -- moving around
    def go(self, day):
        self.cursor = day
        self.month = day.replace(day=1)
        self.draw()

    def move(self, n):
        self.go(self.cursor + datetime.timedelta(days=n))

    def page(self, n):
        first = add_months(self.cursor, n)
        last = calendar.monthrange(first.year, first.month)[1]
        self.go(first.replace(day=min(self.cursor.day, last)))

    def click(self, e, extend):
        self.focus_set()
        for x0, y0, x1, y1, what in self.hits:
            if x0 <= e.x < x1 and y0 <= e.y < y1:
                if isinstance(what, int):
                    self.page(what)
                else:
                    self.pick(what, extend)
                return

    def pick(self, day, extend):
        if extend and self.allow_range and self.start:
            self.end = day
        else:
            self.start = self.end = day
        self.go(day)
        if self.on_pick:
            self.on_pick(min(self.start, self.end), max(self.start, self.end))

    # -- drawing
    def draw(self):
        sk, S, c = self.skin, self.skin.S, self
        c.delete("all")
        self.hits = []
        w, cell = self.cell * 7, self.cell
        x0 = 2 * S
        # month and year, with arrows for the months before and after
        title = f"{calendar.month_name[self.month.month]} {self.month.year}"
        c.create_text(x0 + w / 2, self.head / 2, text=title, font=sk.fb, fill=sk["text"])
        for n, x in ((-1, x0 + cell / 2), (1, x0 + w - cell / 2)):
            a = max(4 * S, cell // 7)
            y = self.head / 2
            pts = (x + a / 2, y - a, x - a / 2, y, x + a / 2, y + a) if n < 0 else (x - a / 2, y - a, x + a / 2, y,
                                                                                   x - a / 2, y + a)
            c.create_polygon(*pts, fill=sk["text"], outline=sk["text"])
            self.hits.append((x - cell / 2, 0, x + cell / 2, self.head, n))
        y = self.head
        for i, name in enumerate(DAY_NAMES):
            c.create_text(x0 + cell * i + cell / 2, y + sk.line / 2, text=name, font=sk.f, fill=sk["dim"])
        y += sk.line + 6 * S
        lo, hi = (min(self.start, self.end), max(self.start, self.end)) if self.start else (None, None)
        soft_text = sk["seltext"] if sk["selsoft"] == sk["sel"] else sk["text"]
        focused = self.focus_get() is self
        for r, week in enumerate(WEEK.monthdatescalendar(self.month.year, self.month.month)):
            for i, day in enumerate(week):
                cx0, cy0 = x0 + cell * i, y + cell * r
                cx1, cy1 = cx0 + cell, cy0 + cell
                m = 2 * S
                fg = sk["text"] if day.month == self.month.month else sk["off"]
                if lo and lo <= day <= hi:
                    end = day in (lo, hi)
                    fill = sk["sel"] if end else sk["selsoft"]
                    if sk.modern:
                        W.rounded(c, cx0 + m, cy0 + m, cx1 - m, cy1 - m, 7 * S, fill=fill, outline="")
                    else:
                        c.create_rectangle(cx0 + m, cy0 + m, cx1 - m, cy1 - m, fill=fill, outline="")
                    fg = sk["seltext"] if end else soft_text
                if day == self.today:  # today: an outline, whatever else is going on
                    c.create_rectangle(cx0 + m, cy0 + m, cx1 - m - 1, cy1 - m - 1, outline=sk["edge"], width=S)
                font = sk.fb if day == self.today else sk.f
                c.create_text((cx0 + cx1) / 2, cy0 + cell * 0.44, text=str(day.day), font=font, fill=fg)
                if self.marks(day):
                    r_ = max(2, 2 * S)
                    cx, cy = (cx0 + cx1) / 2, cy1 - m - 5 * S
                    c.create_oval(cx - r_, cy - r_, cx + r_, cy + r_, fill=fg, outline=fg)
                if focused and day == self.cursor:
                    W.focus_ring(c, sk, cx0 + m + 2 * S, cy0 + m + 2 * S, cx1 - m - 2 * S, cy1 - m - 2 * S)
                self.hits.append((cx0, cy0, cx1, cy1, day))


def lesson_marks(gui):
    """marks() for a Calendar: days that already have a lesson."""
    from ..store import lesson_dates
    days = {d for l in gui.store.data["lessons"] for d in lesson_dates(l)}
    return lambda d: d in days


def pick_dates(gui, title, start=None, end=None):
    """A calendar dialog for a lesson's Date(s). Returns (start, end) or None."""
    sk, S = gui.skin, gui.skin.S
    d = W.Dialog(gui, title)
    W.label(d.body, sk, "Click a day. Teaching it over several days? Click the first day, then Shift-click the last. "
                        "Dots are days that already have a lesson.", wrap=380 * S).pack(anchor="w")
    picked = {"v": (start, end or start) if start else None}
    note = W.label(d.body, sk, "", bold=True)

    def on_pick(a, b):
        picked["v"] = (a, b)
        note.configure(text=fmt_range(a, b))
    cal = Calendar(d.body, sk, start, end, on_pick, lesson_marks(gui), allow_range=True)
    cal.pack(anchor="w", pady=(10 * S, 6 * S))
    note.pack(anchor="w")
    if start:
        note.configure(text=fmt_range(start, end))
    d.buttons([("OK", True), ("Cancel", None)])
    cal.bind("<Return>", lambda e: (cal.pick(cal.cursor, False), d.close(True), "break")[2])
    if not d.run(focus=cal):
        return None
    return picked["v"]
