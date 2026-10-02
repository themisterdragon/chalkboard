"""Inline formatting in any text field: **bold**, *italic*, __underline__.

Markdown-style rules keep ordinary text safe: a mark only counts when the text inside
it doesn't start or end with a space, so "5 * 3 * 2", bullets ("* item"), and
fill-in-the-blank lines ("_____") print as typed. Marks never cross a line break.
"""

import re

PATTERN = re.compile(
    r"\*\*(?=\S)(?P<b>.+?)(?<=\S)\*\*"
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
