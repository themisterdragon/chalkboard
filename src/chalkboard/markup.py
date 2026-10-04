"""Inline formatting in any text field: **bold**, *italic*, __underline__.

Markdown-style rules keep ordinary text safe: a mark only counts when the text inside
it doesn't start or end with a space, so "5 * 3 * 2", bullets ("* item"), and
fill-in-the-blank lines ("_____") print as typed. Marks never cross a line break.
"""

import re

PATTERN = re.compile(
    r"\*\*(?=\S)(?P<b>.+?)(?<=\S)\*\*(?!\*)"
    r"|(?<![\w_])__(?=[^\s_])(?P<u>.+?)(?<=[^\s_])__(?![\w_])"
    r"|(?<![\w*])\*(?=[^\s*])(?P<i>.+?)(?<=[^\s*])\*(?![\w*])")


def runs(text, bold=False, italic=False, under=False):
    """text -> [(piece, bold, italic, underline)], marks removed. Marks can nest."""
    out, at = [], 0
    for m in PATTERN.finditer(text or ""):
        if m.start() > at:
            out.append((text[at:m.start()], bold, italic, under))
        kind = m.lastgroup
        out += runs(m.group(kind), bold or kind == "b", italic or kind == "i", under or kind == "u")
        at = m.end()
    if at < len(text or ""):
        out.append((text[at:], bold, italic, under))
    return out


def plain(text):
    """The text without its marks (for plain-text exports and previews)."""
    return "".join(p for p, *_ in runs(text))


def spans(text):
    """For an editor: [(start, end, kind)] where kind is 'mark' for the ** __ * themselves,
    or a set of 'b'/'i'/'u' for the formatted text between them."""
    out = []

    def walk(s, offset, flags):
        for m in PATTERN.finditer(s):
            kind = m.lastgroup
            a, b = m.start(kind), m.end(kind)
            out.append((offset + m.start(), offset + a, "mark"))
            out.append((offset + b, offset + m.end(), "mark"))
            inner = flags | {kind}
            out.append((offset + a, offset + b, inner))
            walk(m.group(kind), offset + a, inner)
    walk(text or "", 0, frozenset())
    return out


MARKS = {"b": "**", "i": "*", "u": "__"}


def toggle(buf, pos, kind):
    """Ctrl-B/T/U in an editor: wrap the word at the cursor (or unwrap it if it already has
    that mark), or drop an empty pair at the cursor to type into. Returns (buf, pos)."""
    mark = MARKS[kind]
    a = pos
    while a > 0 and not buf[a - 1].isspace():
        a -= 1
    b = pos
    while b < len(buf) and not buf[b].isspace():
        b += 1
    word = buf[a:b]
    if not word:
        return buf[:pos] + mark + mark + buf[pos:], pos + len(mark)
    core = word.strip(".,;:!?\"')(")  # wrap "word" in "word," and keep the comma outside
    lead = word.index(core) if core else 0
    ca, cb = a + lead, a + lead + len(core)
    m = re.fullmatch(PATTERN, core)
    if m and m.lastgroup == kind:
        inner = m.group(kind)
        return buf[:ca] + inner + buf[cb:], max(ca, min(pos - len(mark), ca + len(inner)))
    return buf[:ca] + mark + core + mark + buf[cb:], cb + 2 * len(mark)


B, I, U = 1, 2, 4
MARK_OF = {B: "**", I: "*", U: "__"}
WORD = re.compile(r"\w")
_ORDERS = [(U, B, I), (B, U, I), (U, I, B), (I, U, B), (B, I, U), (I, B, U)]


def flags_of(text):
    """Marked text -> (plain text, [B/I/U bits per character])."""
    pieces, fl = [], []
    for piece, b, i, u in runs(text):
        pieces.append(piece)
        fl += [B * b | I * i | U * u] * len(piece)
    return "".join(pieces), fl


def _nest(line, fl, order):
    out, stack = [], []
    for k, ch in enumerate(line):
        while stack and any(not fl[k] & s for s in stack):
            out.append(MARK_OF[stack.pop()])
        for bit in order:
            if fl[k] & bit and bit not in stack:
                stack.append(bit)
                out.append(MARK_OF[bit])
        out.append(ch)
    out += [MARK_OF[s] for s in reversed(stack)]
    return "".join(out)


def _each(line, fl, order):
    out, k = [], 0
    while k < len(line):
        j = k
        while j < len(line) and fl[j] == fl[k]:
            j += 1
        bits = [b for b in order if fl[k] & b]
        out += [MARK_OF[b] for b in bits] + [line[k:j]] + [MARK_OF[b] for b in reversed(bits)]
        k = j
    return "".join(out)


def _aligned(line, fl, s):
    """How far reading s back strays from (line, fl): bits that differ, or None if it changes more
    of the text than marks typed into it (* and _ that become formatting)."""
    plain_, got = flags_of(s)
    miss, k = 0, 0
    for ch, g in zip(plain_, got):
        while k < len(line) and line[k] != ch and line[k] in "*_":
            k += 1
        if k >= len(line) or line[k] != ch:
            return None
        miss += g != fl[k]
        k += 1
    if any(c not in "*_" for c in line[k:]):
        return None
    return miss + (len(line) - len(plain_))


def _line(line, fl):
    """One line's marks: the first way of writing them that reads back exactly, else the closest."""
    if not any(fl):
        return line
    best, least = line, _aligned(line, fl, line)
    clean = list(fl)  # last resort: leave any word with a typed * or _ in it (and the spaces around it) plain
    for m in re.finditer(r"\s*\S*[*_]\S*\s*", line):
        clean[m.start():m.end()] = [0] * (m.end() - m.start())
    bare = [0 if ch.isspace() else f for ch, f in zip(line, fl)]  # or with the spaces between words plain
    tries = []
    for bits in (fl, bare, clean, [0 if ch.isspace() else f for ch, f in zip(line, clean)]):
        if bits not in tries:
            tries.append(bits)
    for bits in tries:
        for make in (_nest, _each):
            for order in _ORDERS:
                s = make(line, bits, order)
                miss = _aligned(line, fl, s)
                if miss == 0:
                    return s
                if miss is not None and (least is None or miss < least):
                    best, least = s, miss
    return best


def marked(text, fl):
    """The reverse of flags_of: plain text and per-character bits -> text with marks."""
    out, at = [], 0
    for line in text.split("\n"):
        out.append(_line(line, fl[at:at + len(line)]))
        at += len(line) + 1
    return "\n".join(out)


def tidy(text, fl):
    for _ in range(4):
        got = _tidy(text, fl)
        if got == (text, fl):
            break
        text, fl = got
    return text, fl


def _tidy(text, fl):
    """(text, bits) as an editor should show them, so what it shows is what gets saved: italic and
    underline cover whole words, no mark starts or ends on a space, anything the marks can't say is
    dropped, and marks typed into the text (*like this*) become formatting. Returns (text, bits);
    the text only ever loses characters (the marks it turned into formatting)."""
    fl = [0 if ch == "\n" else f for ch, f in zip(text, fl)]
    n = len(text)

    def spans(bit):
        out, k = [], 0
        while k < n:
            if fl[k] & bit:
                j = k
                while j < n and fl[j] & bit:
                    j += 1
                out.append((k, j))
                k = j
            else:
                k += 1
        return out

    for bit in (B, I):  # a space between two bold (or italic) words is bold too: "**two words**"
        for a, b in spans(bit):
            k = b
            while k < n and text[k].isspace() and text[k] != "\n":
                k += 1
            if k > b and k < n and fl[k] & bit:
                for x in range(b, k):
                    fl[x] |= bit
    for bit in (B, I, U):  # no mark starts or ends on a space
        for a, b in spans(bit):
            k = a
            while k < b and text[k].isspace():
                fl[k] &= ~bit
                k += 1
            k = b - 1
            while k >= a and text[k].isspace():
                fl[k] &= ~bit
                k -= 1
    for bit in (I, U):  # italic and underline cover whole words
        for a, b in spans(bit):
            while a > 0 and WORD.match(text[a - 1]):
                a -= 1
            while b < n and WORD.match(text[b]):
                b += 1
            for k in range(a, b):
                fl[k] |= bit
    for _ in range(8):  # settle: reading back what was written changes nothing
        got = flags_of(marked(text, fl))
        if got == (text, fl):
            break
        text, fl = got
    return text, fl


URL = re.compile(r"(?:https?://|www\.)[^\s<>\"]+", re.I)


def links(text):
    """text -> [(piece, url or None)]: web addresses written out become links. Trailing punctuation
    ("see https://x.org.") stays outside, and a bare www. address gets https:// in front."""
    out, at = [], 0
    for m in URL.finditer(text or ""):
        url = m.group(0).rstrip(".,;:!?'\")]")
        if url.count("(") > url.count(")") and m.group(0)[len(url):].startswith(")"):
            url += ")"  # keep the ) of a Wikipedia-style link
        if m.start() > at:
            out.append((text[at:m.start()], None))
        out.append((url, url if url.lower().startswith("http") else "https://" + url))
        at = m.start() + len(url)
    if at < len(text or ""):
        out.append((text[at:], None))
    return out
