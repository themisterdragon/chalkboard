"""The school mascot as a chibi in the window app: it waits in a corner of the home screen and
cheers when it's clicked or an export finishes. It only moves when something happens, so it
costs nothing while it waits."""

import tkinter as tk

from .. import chibi
from ..mascots import name as mascot_name


def image(gui, key, scale, cheer=False):
    """The mascot as a Tk image (clear background), scale screen pixels per art pixel; None if none."""
    st = gui.settings
    rows = chibi.frame(key, cheer)
    if not rows:
        return None
    pal = chibi.colors(key, st.get("primary_color", ""), st.get("secondary_color", ""))
    cache = gui.__dict__.setdefault("_chibis", {})
    ck = (key, cheer, scale, pal.get("p"), pal.get("s"))
    if ck not in cache:
        img = tk.PhotoImage(master=gui.root, width=chibi.SIZE, height=chibi.SIZE)
        for y, r in enumerate(rows):
            x = 0
            while x < len(r):  # one put() per run of a color
                c = r[x]
                end = x
                while end < len(r) and r[end] == c:
                    end += 1
                if c != ".":
                    img.put(pal[c], to=(x, y, end, y + 1))
                x = end
        cache[ck] = img.zoom(scale, scale) if scale > 1 else img
    return cache[ck]


def cheer_text(key):
    n = mascot_name(key)
    return f"GO {n}!" if n else "Nice work!"


class Buddy(tk.Canvas):
    """A mascot that hops and cheers on click (or cheer()). Empty, and tiny, when there's no mascot."""

    def __init__(self, parent, gui, scale=None, bubble=True):
        self.gui, self.bubble = gui, bubble
        S = gui.skin.S
        self.scale = scale or 4 * S
        size = chibi.SIZE * self.scale
        self.key = gui.settings.get("mascot") or ""
        has = chibi.frame(self.key) is not None
        w = size + (150 * S if bubble else 0) if has else 1
        super().__init__(parent, width=w, height=size + 30 * S if has else 1, bg=parent["bg"],
                         highlightthickness=0, bd=0, cursor="hand2" if has else "")
        self.jobs = []
        if has:
            self.stand()
            self.bind("<Button-1>", lambda e: self.cheer())

    def stand(self):
        self.delete("all")
        img = image(self.gui, self.key, self.scale)
        self.create_image(int(self["width"]), int(self["height"]), image=img, anchor="se", tags="pet")

    def after_jobs_clear(self):
        for j in self.jobs:
            try:
                self.after_cancel(j)
            except tk.TclError:
                pass
        self.jobs = []

    def cheer(self):
        """Hop twice with arms up, say the cheer, then stand again."""
        if chibi.frame(self.key) is None or not self.winfo_exists():
            return
        self.after_jobs_clear()
        sk, S = self.gui.skin, self.gui.skin.S
        up = image(self.gui, self.key, self.scale, cheer=True)
        down = image(self.gui, self.key, self.scale)
        x, y = int(self["width"]), int(self["height"])
        hop = 10 * S

        def show(img, dy):
            self.delete("pet")
            self.create_image(x, y - dy, image=img, anchor="se", tags="pet")

        def say():
            if not self.bubble:
                return
            self.delete("talk")
            text = cheer_text(self.key)
            t = self.create_text(10 * S, 16 * S, text=text, anchor="nw", font=sk.fb, fill=sk["text"], tags="talk")
            x0, y0, x1, y1 = self.bbox(t)
            r = self.create_rectangle(x0 - 6 * S, y0 - 4 * S, x1 + 6 * S, y1 + 4 * S, fill=sk["field"],
                                      outline=sk["edge"], width=S, tags="talk")
            self.tag_lower(r, t)

        steps = [(0, lambda: (show(up, hop), say())), (140, lambda: show(up, 0)), (260, lambda: show(up, hop)),
                 (400, lambda: show(down, 0)), (2200, lambda: self.delete("talk"))]
        for ms, fn in steps:
            self.jobs.append(self.after(ms, fn))
