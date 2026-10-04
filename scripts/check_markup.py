#!/usr/bin/env python3
"""Check that **bold**, *italic*, and __underline__ survive the window app's text boxes.

The window app shows formatting without the marks and writes them back when it saves
(markup.tidy / marked / flags_of). This makes sure that what a box shows is exactly what
it saves, that saving twice changes nothing, and that text from before keeps its marks.
"""

import pathlib
import random
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "src"))
from chalkboard import markup as m  # noqa: E402

KEEP = ["**Bold** and *italic* and __under__", "***both***", "**bold *both***", "5 * 3 * 2", "* bullet",
        "Name: ______", "**a** *b* __c__", "__**x**__ y", "un**believ**able", "- **one**\n* two",
        "Read *Hamlet* 3.1, then __answer__ **all** questions."]


def main():
    problems = []
    for s in KEEP:
        plain, bits = m.flags_of(s)
        if m.tidy(plain, bits) != (plain, bits) or m.marked(plain, bits) != s:
            problems.append(f"changes on save: {s!r} -> {m.marked(*m.tidy(plain, bits))!r}")
    rng = random.Random(1)
    words = ["the", "Hamlet", "a", "5", "x_y", "don't", "word,", "(note)", "*", "**", "__", "re-read", "café",
             "Q1.", "—", "snake_case", "5 * 3"]
    for _ in range(20000):  # any formatting at all: shown == saved, and saving again changes nothing
        text = "".join(rng.choice(words) + rng.choice([" ", " ", "\n", "", ", "]) for _ in range(rng.randint(1, 8)))
        bits, cur = [], 0
        for _ch in text:
            cur = rng.randint(0, 7) if rng.random() < 0.3 else cur
            bits.append(cur)
        shown = m.tidy(text, bits)
        if m.flags_of(m.marked(*shown)) != shown or m.tidy(*shown) != shown:
            problems.append(f"unsettled: {text!r} {bits}")
            break
    words = [w for w in words if not set(w) & set("*_")]
    for _ in range(20000):  # whole words formatted, the usual case: nothing is lost
        text, bits = "", []
        for w in (rng.choice(words) for _ in range(rng.randint(1, 8))):
            f = rng.choice([0, 0, 1, 2, 4, 3, 5, 6, 7])
            text, bits = text + w + " ", bits + [f] * len(w) + [rng.choice([0, f])]
        got = m.tidy(text, bits)[1]
        if any(c.strip() and f & ~g for c, f, g in zip(text, bits, got)):
            problems.append(f"lost formatting: {m.marked(text, bits)!r}")
            break
    if problems:
        sys.exit("MARKUP PROBLEMS:\n  " + "\n  ".join(problems))
    print("ok: formatting in text boxes saves exactly as shown")


if __name__ == "__main__":
    main()
