"""Retro curses toolkit: green-screen chrome, numbered lists, line and text editors."""

import sys
from datetime import datetime

try:
    import curses
except ImportError:  # Windows without the windows-curses package
    sys.exit("chalkboard needs curses. On Windows run: pip install windows-curses")

from .banner import APP, big  # noqa: F401  (re-exported for app.py)
from .markup import spans, toggle
from .mascots import hop, name as mascot_name, sprite

THEMES = {
    "green": ((83, 28, 157), curses.COLOR_GREEN),
    "amber": ((214, 130, 222), curses.COLOR_YELLOW),
    "white": ((252, 243, 231), curses.COLOR_WHITE),
}

ENTER = ("\n", "\r", curses.KEY_ENTER)
BACK = ("\x1b", curses.KEY_BACKSPACE, "\x7f", "\b", curses.KEY_LEFT)
BKSP = (curses.KEY_BACKSPACE, "\x7f", "\b")


def ch(k):
    return k if isinstance(k, str) else ""


def truncate(s, n):
    s = (s or "").replace("\n", " ").replace("\t", " ")
    if n <= 0:
        return ""
    return s if len(s) <= n else s[: max(0, n - 3)] + "..."


def wrap_rows(buf, width):
    """Soft-wrap buf into visual rows of (start, end) indexes."""
    rows = []
    start = 0
    n = len(buf)
    while True:
        nl = buf.find("\n", start)
        end = nl if nl != -1 else n
        s = start
        if s == end:
            rows.append((s, s))
        while s < end:
            if end - s <= width:
                rows.append((s, end))
                break
            cut = buf.rfind(" ", s, s + width)
            if cut <= s:
                rows.append((s, s + width))
                s += width
            else:
                rows.append((s, cut + 1))
                s = cut + 1
        if nl == -1:
            break
        start = nl + 1
    return rows


class UI:
    def __init__(self, stdscr, settings):
        self.s = stdscr
        self.settings = settings
        self.buf = []
        self.msg = None
        # The line-by-line reveal refreshes once per row; the Windows console is slow at that
        # (and its 8 ms sleeps last ~16 ms), so every screen change would stutter there.
        self.animate = sys.platform != "win32"
        self.cursor(False)
        stdscr.keypad(True)
        curses.start_color()
        self.apply_theme()

    def cursor(self, on):
        try:
            curses.curs_set(1 if on else 0)
        except curses.error:
            pass

    def apply_theme(self):
        (fg, dim, hi), basic = THEMES.get(self.settings.get("theme"), THEMES["green"])
        if curses.COLORS >= 256:
            bg, extra_dim, extra_hi = 16, 0, 0
        else:
            fg = dim = hi = basic
            bg, extra_dim, extra_hi = curses.COLOR_BLACK, curses.A_DIM, curses.A_BOLD
        curses.init_pair(1, fg, bg)
        curses.init_pair(2, bg, fg)
        curses.init_pair(3, dim, bg)
        curses.init_pair(4, hi, bg)
        self.N = curses.color_pair(1)
        self.INV = curses.color_pair(2)
        self.DIM = curses.color_pair(3) | extra_dim
        self.HI = curses.color_pair(4) | curses.A_BOLD | extra_hi
        self.s.bkgd(" ", self.N)
        # Pixel art: one color pair per (top, bottom) tone, drawn with half blocks.
        tones = {".": bg, "d": dim, "n": fg, "h": hi}
        self.PIX = {}
        for i, (top, bot) in enumerate((a, b) for a in tones for b in tones):
            if curses.COLOR_PAIRS > 10 + i:
                curses.init_pair(10 + i, tones[top], tones[bot])
                self.PIX[top, bot] = curses.color_pair(10 + i)
            else:  # tiny palette: a plain silhouette
                self.PIX[top, bot] = self.N if top != "." else self.INV

    def dims(self):
        """(rows, content width, left edge). Everything hugs the left edge like a plain terminal
        app; the width stops at 110 so lines stay readable on a wide screen."""
        rows, cols = self.s.getmaxyx()
        return rows, min(cols, 110), 0

    def tx(self, s):
        return s.upper() if self.settings.get("uppercase", True) else s

    # -- buffered drawing
    def begin(self):
        self.buf = []

    def put(self, y, x, text, attr=None, raw=False):
        self.buf.append((y, x, text if raw else self.tx(text), self.N if attr is None else attr))

    def line(self, y, text, attr=None, raw=False):
        """A line of text at the left margin."""
        self.put(y, 1, text, attr, raw)

    def draw(self, y, x, text, attr):
        rows, cols = self.s.getmaxyx()
        if y < 0 or y >= rows or x >= cols or x < 0:
            return
        try:
            self.s.addstr(y, x, text[: cols - x], attr)
        except curses.error:
            pass

    def show(self, animate=False, cursor=None):
        """Draw the buffered screen with one refresh (cursor=(y, x) places the text cursor first)."""
        animate = animate and self.animate
        rows, cols = self.s.getmaxyx()
        self.s.erase()
        if rows < 16 or cols < 60:
            self.draw(0, 0, "TERMINAL TOO SMALL (NEED 60X16)", self.HI)
            self.s.refresh()
            return
        items = sorted(self.buf, key=lambda b: b[0]) if animate else self.buf
        if animate:
            self.s.nodelay(True)
        last = None
        for y, x, text, attr in items:
            if animate and last is not None and y != last:
                self.s.refresh()
                curses.napms(8)
                k = self.s.getch()
                if k != -1:
                    curses.ungetch(k)
                    animate = False
            last = y
            self.draw(y, x, text, attr)
        self.s.nodelay(False)
        if cursor:
            try:
                self.s.move(*cursor)
            except curses.error:
                pass
        self.s.refresh()

    def sprite_runs(self, rows):
        """Pixel-art rows -> [(row, col, text, attr)], two pixels per cell (upper half block)."""
        out = []
        for r in range(0, len(rows) - 1, 2):
            top, bot = rows[r], rows[r + 1]
            for c, (a, b) in enumerate(zip(top, bot)):
                if a == "." and b == ".":
                    continue
                if a == b:
                    out.append((r // 2, c, "█", self.PIX[a, "."]))
                else:
                    out.append((r // 2, c, "▀", self.PIX[a, b]))
        return out

    def put_sprite(self, y, x, rows):
        for dy, dx, text, attr in self.sprite_runs(rows):
            self.put(y + dy, x + dx, text, attr, raw=True)

    def draw_sprite(self, y, x, rows, clear=True):
        """Draw a sprite right away (clear=True blanks its box first, for animation)."""
        if clear:
            for dy in range(len(rows) // 2):
                self.draw(y + dy, x, " " * len(rows[0]), self.N)
        for dy, dx, text, attr in self.sprite_runs(rows):
            self.draw(y + dy, x + dx, text, attr)

    def header(self, title, raw=False):
        """The title bar, across the whole screen."""
        rows, cols = self.s.getmaxyx()
        self.put(0, 0, " " * cols, self.INV)
        self.put(0, 1, APP, self.INV)
        title = truncate(title, cols - 34)
        self.put(0, (cols - len(title)) // 2, title, self.INV, raw)
        clock = datetime.now().strftime("%a %b %d %H:%M")
        self.put(0, cols - len(clock) - 1, clock, self.INV)

    def footer(self, hints):
        rows, w, x0 = self.dims()
        if self.msg:
            self.put(rows - 2, x0, truncate(self.msg, w), self.HI, raw=True)
            self.msg = None
        else:
            self.put(rows - 2, x0, truncate(hints, w), self.DIM)
        self.put(rows - 1, x0, "]", self.N)

    def badge(self, y, x, label):
        self.put(y, x, f" {label} ", self.INV)

    # -- input
    def key(self):
        rows, w, x0 = self.dims()
        self.s.timeout(450)
        on = True
        while True:
            self.draw(rows - 1, x0 + 1, "█" if on else " ", self.N)
            self.s.refresh()
            try:
                k = self.s.get_wch()
            except curses.error:
                on = not on
                continue
            if k == curses.KEY_RESIZE:
                curses.update_lines_cols()
            return k

    def echo(self, c):
        rows, w, x0 = self.dims()
        self.draw(rows - 1, x0 + 1, self.tx(c) + "█", self.N)
        self.s.refresh()
        curses.napms(80)

    def prompt(self, label, initial="", replace=False, raw=False):
        """One-line editor at the ] prompt. Returns text, or None on Esc.

        replace=True: the first typed character replaces the pre-filled value.
        raw=True: show the label as given, without applying the all-caps setting.
        """
        buf = list(str(initial or ""))
        pos = len(buf)
        fresh = replace and bool(buf)
        self.s.timeout(-1)
        self.cursor(True)
        try:
            while True:
                rows, w, x0 = self.dims()
                head = "]" + (label if raw else self.tx(label)) + "? "
                room = max(10, w - len(head) - 1)
                off = max(0, pos - room + 1)
                text = "".join(buf)[off:off + room]
                self.draw(rows - 1, x0, " " * w, self.N)
                self.draw(rows - 1, x0, head, self.HI)
                self.draw(rows - 1, x0 + len(head), text, self.N)
                self.s.move(rows - 1, min(x0 + len(head) + pos - off, x0 + w - 1))
                self.s.refresh()
                k = self.s.get_wch()
                if fresh and isinstance(k, str) and k.isprintable():
                    buf, pos = [], 0
                fresh = False
                if k in ENTER:
                    return "".join(buf).strip()
                if k == "\x1b":
                    return None
                if k in BKSP:
                    if pos:
                        pos -= 1
                        buf.pop(pos)
                elif k == curses.KEY_DC:
                    if pos < len(buf):
                        buf.pop(pos)
                elif k == curses.KEY_LEFT:
                    pos = max(0, pos - 1)
                elif k == curses.KEY_RIGHT:
                    pos = min(len(buf), pos + 1)
                elif k in (curses.KEY_HOME, "\x01"):
                    pos = 0
                elif k in (curses.KEY_END, "\x05"):
                    pos = len(buf)
                elif k == "\x15":
                    buf, pos = [], 0
                elif isinstance(k, str) and k.isprintable():
                    buf.insert(pos, k)
                    pos += 1
        finally:
            self.cursor(False)

    def confirm(self, question):
        """Yes/no question. Return (or Y) means yes; N or Esc means no."""
        ans = self.prompt(self.tx(question) + " (Y/n)", raw=True)
        return ans is not None and (ans == "" or ans.lower().startswith("y"))

    def edit_text(self, title, text, help_text="", fill=None, fill_hint="", fill_prefix=""):
        """Full-screen word-wrapping editor. Esc saves, Ctrl-X cancels.

        fill(buf) -> str, if given, is bound to Ctrl-G: it replaces an empty buffer or
        one that is just an earlier fill (one line starting with fill_prefix);
        otherwise it is inserted at the cursor."""
        buf = text or ""
        pos = len(buf)
        top = 0
        original = buf
        filled = buf if fill and buf.startswith(fill_prefix) and "\n" not in buf.strip() else None
        goal_col = None
        self.s.timeout(-1)
        self.cursor(True)
        try:
            while True:
                rows, w, x0 = self.dims()
                width = w - 4
                vis = wrap_rows(buf, width)
                r = 0
                for i, (a, _) in enumerate(vis):
                    if a <= pos:
                        r = i
                col = pos - vis[r][0]
                body = rows - 5
                if r < top:
                    top = r
                elif r >= top + body:
                    top = r - body + 1

                self.begin()
                self.header("EDIT: " + title)
                self.put(1, x0 + 1, help_text or "TYPE FREELY.  START A LINE WITH '- ' FOR A BULLET.", self.DIM)
                self.put(2, x0, "-" * w, self.DIM)
                attrs = self.mark_attrs(buf)
                for i, (a, b) in enumerate(vis[top:top + body]):
                    line = buf[a:b].rstrip("\n")
                    j = 0
                    while j < len(line):  # one put per run of the same look
                        k = j
                        while k < len(line) and attrs[a + k] == attrs[a + j]:
                            k += 1
                        self.put(3 + i, x0 + 2 + j, line[j:k], attrs[a + j], raw=True)
                        j = k
                words = len(buf.split())
                self.put(rows - 2, x0, f"ESC SAVE+EXIT   CTRL-X CANCEL   {fill_hint + '   ' if fill else ''}"
                                       f"^B BOLD  ^T ITALIC  ^U UNDERLINE   LINE {r + 1}/{len(vis)}   {words} WORDS",
                         self.DIM)
                self.show(cursor=(3 + r - top, x0 + 2 + col))

                k = self.s.get_wch()
                if k == "\x1b":
                    return buf
                if k == "\x18":
                    if buf == original or self.confirm("DISCARD CHANGES"):
                        return None
                    continue
                if k in ("\x02", "\x14", "\x15"):  # Ctrl-B / Ctrl-T / Ctrl-U (Ctrl-I is Tab in a terminal)
                    buf, pos = toggle(buf, pos, {"\x02": "b", "\x14": "i", "\x15": "u"}[k])
                    continue
                if k == "\x07" and fill:
                    if not buf.strip() or buf == filled:
                        buf = filled = fill(buf)
                        pos = len(buf)
                    else:
                        add = fill(buf)
                        buf = buf[:pos] + add + buf[pos:]
                        pos += len(add)
                    continue
                if k not in (curses.KEY_UP, curses.KEY_DOWN, curses.KEY_PPAGE, curses.KEY_NPAGE):
                    goal_col = None
                if k in ENTER:
                    buf = buf[:pos] + "\n" + buf[pos:]
                    pos += 1
                elif k in BKSP:
                    if pos:
                        buf = buf[:pos - 1] + buf[pos:]
                        pos -= 1
                elif k == curses.KEY_DC:
                    buf = buf[:pos] + buf[pos + 1:]
                elif k == curses.KEY_LEFT:
                    pos = max(0, pos - 1)
                elif k == curses.KEY_RIGHT:
                    pos = min(len(buf), pos + 1)
                elif k in (curses.KEY_UP, curses.KEY_DOWN, curses.KEY_PPAGE, curses.KEY_NPAGE):
                    step = {curses.KEY_UP: -1, curses.KEY_DOWN: 1,
                            curses.KEY_PPAGE: -(body - 1), curses.KEY_NPAGE: body - 1}[k]
                    goal_col = col if goal_col is None else goal_col
                    nr = max(0, min(len(vis) - 1, r + step))
                    if nr == r:
                        pos = 0 if step < 0 else len(buf)
                    else:
                        a, b = vis[nr]
                        limit = b - a
                        if nr + 1 < len(vis) and vis[nr + 1][0] == b and b > a:
                            limit -= 1  # soft-wrapped row: don't land past the wrap point
                        pos = a + min(goal_col, limit)
                elif k in (curses.KEY_HOME, "\x01"):
                    pos = vis[r][0]
                elif k in (curses.KEY_END, "\x05"):
                    a, b = vis[r]
                    pos = b - 1 if (r + 1 < len(vis) and vis[r + 1][0] == b and b > a) else b
                elif k == "\t":
                    buf = buf[:pos] + "  " + buf[pos:]
                    pos += 2
                elif isinstance(k, str) and (k.isprintable()):
                    buf = buf[:pos] + k + buf[pos:]
                    pos += len(k)
        finally:
            self.cursor(False)

    def mark_attrs(self, buf):
        """A curses attribute for every character of buf: formatted text shows bold, italic, or
        underlined; the ** __ * marks themselves are dimmed."""
        looks = {"b": curses.A_BOLD, "i": getattr(curses, "A_ITALIC", 0), "u": curses.A_UNDERLINE}
        attrs = [self.N] * len(buf)
        for a, b, kind in spans(buf):
            attr = self.DIM if kind == "mark" else self.N
            if kind != "mark":
                for flag in kind:
                    attr |= looks[flag]
            for i in range(a, b):
                attrs[i] = attr
        return attrs

    def view_lines(self, title, lines, hints="ARROWS/SPACE SCROLL   ESC BACK", raw_title=False):
        """Scrollable read-only view of [(text, attr)] lines (text shown as-is)."""
        top, first = 0, True
        while True:
            rows, w, x0 = self.dims()
            body = rows - 4
            maxtop = max(0, len(lines) - body)
            top = max(0, min(top, maxtop))
            self.begin()
            self.header(title, raw_title)
            for i, (text, attr) in enumerate(lines[top:top + body]):
                self.put(2 + i, x0 + 2, text, attr or self.N, raw=True)
            pct = 100 if maxtop == 0 else round(top / maxtop * 100)
            self.footer(f"{pct:>3}%  {hints}")
            self.show(animate=first)
            first = False
            k = self.key()
            c = ch(k).lower()
            if k in BACK or c == "q":
                return
            if k == curses.KEY_DOWN or c == "j":
                top += 1
            elif k == curses.KEY_UP or c == "k":
                top -= 1
            elif c == " " or k == curses.KEY_NPAGE:
                top += body - 2
            elif c == "b" or k == curses.KEY_PPAGE:
                top -= body - 2
            elif k == curses.KEY_HOME:
                top = 0
            elif k == curses.KEY_END:
                top = maxtop

    def list_screen(self, title, items_fn, row_fn, on_open=None, on_key=None, hints="",
                    info_fn=None, empty="NOTHING HERE YET.", start=0):
        """Numbered, paged list. Number keys or arrows+Return open an item.

        row_fn(item, width) -> (text, attr|None)
        on_open(item, index) / on_key(key, item|None, index|None) may return
        "back" to leave the list, or an int to move the selection there.
        """
        page = sel = 0
        pending = start
        first = True
        while True:
            items = items_fn()
            rows, w, x0 = self.dims()
            per = max(1, min(10, rows - 6))
            if pending is not None:
                page, sel = divmod(max(0, pending), per)
                pending = None
            pages = max(1, -(-len(items) // per))
            page = max(0, min(page, pages - 1))
            chunk = items[page * per:(page + 1) * per]
            sel = max(0, min(sel, len(chunk) - 1))
            cur = page * per + sel if chunk else None

            self.begin()
            self.header(title)
            info = info_fn() if info_fn else f"{len(items)} ITEMS"
            pg = f"PAGE {page + 1}/{pages}" if pages > 1 else ""
            self.put(1, x0 + 1, truncate(info, w - len(pg) - 3), self.DIM, raw=True)
            if pg:
                self.put(1, x0 + w - len(pg) - 1, pg, self.DIM)
            for i, it in enumerate(chunk):
                y = 3 + i
                self.badge(y, x0 + 1, str((i + 1) % 10))
                text, attr = row_fn(it, w - 7)
                text = truncate(text, w - 7) if len(text) > w - 7 else text
                if i == sel:
                    self.put(y, x0 + 5, " " + text.ljust(w - 7), self.INV, raw=True)
                else:
                    self.put(y, x0 + 6, text, attr or self.N, raw=True)
            if not items:
                self.put(3, x0 + 2, empty, self.N)
            self.footer(hints)
            self.show(animate=first)
            first = False

            k = self.key()
            c = ch(k)
            result = None
            if c.isdigit() and len(c) == 1 and on_open:
                i = (int(c) - 1) % 10
                if i < len(chunk):
                    sel = i
                    self.echo(c)
                    result = on_open(chunk[i], page * per + i)
                    first = True
            elif k in ENTER and chunk and on_open:
                result = on_open(chunk[sel], cur)
                first = True
            elif k == curses.KEY_DOWN:
                if sel < len(chunk) - 1:
                    sel += 1
                elif page < pages - 1:
                    page, sel = page + 1, 0
            elif k == curses.KEY_UP:
                if sel > 0:
                    sel -= 1
                elif page > 0:
                    page, sel = page - 1, per - 1
            elif k in (curses.KEY_NPAGE, curses.KEY_RIGHT) or c == ">":
                page, sel = page + 1, 0
            elif k == curses.KEY_PPAGE or c == "<":
                page, sel = page - 1, 0
            elif k in BACK:
                return
            elif on_key:
                result = on_key(k, chunk[sel] if chunk else None, cur)
            if result == "back":
                return
            if isinstance(result, int) and not isinstance(result, bool):
                pending = result

    def choose(self, title, options, current=None, hints="PRESS A NUMBER   ESC CANCEL"):
        """Pick one of [label, ...]; returns its index or None."""
        picked = []

        def on_open(item, i):
            picked.append(i)
            return "back"
        self.list_screen(title, lambda: list(enumerate(options)),
                         lambda o, w: (("* " if o[0] == current else "  ") + o[1], None),
                         on_open=on_open, hints=hints, info_fn=lambda: "",
                         start=current or 0)
        return picked[0] if picked else None


class ExportBar:
    """progress(done, total, file) for exporting(): the mascot (or the logo) runs along a bar.

    It only redraws a few rows per step, so it costs next to nothing on any machine.
    """

    def __init__(self, ui, mascot):
        self.ui, self.mascot, self.art = ui, mascot, sprite(mascot)
        self.x = None
        self.frame = 0

    def __call__(self, done, total, label):
        ui = self.ui
        rows, cols = ui.s.getmaxyx()
        if rows < 16 or cols < 60:
            return
        rows, w, x0 = ui.dims()
        if self.x is None:  # first call: the screen frame
            ui.begin()
            ui.header("EXPORTING")
            ui.show()
        y = 2
        left, span = x0 + 1, w - 2 - 16
        goal = left + span * done // max(1, total)
        if self.x is None:
            self.x = left
        # glide to the new spot in a few hops (skipped where the console is slow)
        steps = min(6, goal - self.x) if ui.animate else 0
        for i in range(1, steps + 1):
            self.draw(y, self.x + (goal - self.x) * i // steps, left, span, done, total)
            curses.napms(12)
        self.x = goal
        self.draw(y, goal, left, span, done, total)
        if done < total:
            text = f"{done + 1} OF {total}: {label}"
        else:
            text = f"DONE! GO {mascot_name(self.mascot)}!" if mascot_name(self.mascot) else "DONE!"
        ui.draw(y + 11, x0, " " * w, ui.N)
        ui.draw(y + 11, left, truncate(text, w - 2), ui.HI if done == total else ui.N)
        ui.s.refresh()
        if done == total:
            curses.napms(350)  # a beat to see it finish

    def draw(self, y, x, left, span, done, total):
        ui = self.ui
        self.frame += 1
        for dy in range(8):
            ui.draw(y + dy, left, " " * (span + 16), ui.N)
        ui.draw_sprite(y, x, hop(self.art) if self.frame % 2 else self.art, clear=False)
        # the bar under its feet: filled up to the mascot, dim after it
        filled = (span + 16) * (x - left) // max(1, span)
        ui.draw(y + 8, left, "▀" * filled, ui.PIX["n", "."])
        ui.draw(y + 8, left + filled, "▀" * (span + 16 - filled), ui.PIX["d", "."])
        pct = f"{100 * done // max(1, total):>3}%"
        ui.draw(y + 9, left + span + 16 - len(pct), pct, ui.DIM)
        ui.s.refresh()
