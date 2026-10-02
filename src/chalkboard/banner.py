"""The block-letter CHALKBOARD banner (no UI toolkit needed)."""

APP = "Chalkboard"

FONT = {
    "A": [".#.", "#.#", "###", "#.#", "#.#"],
    "B": ["##.", "#.#", "##.", "#.#", "##."],
    "C": ["###", "#..", "#..", "#..", "###"],
    "D": ["##.", "#.#", "#.#", "#.#", "##."],
    "H": ["#.#", "#.#", "###", "#.#", "#.#"],
    "K": ["#.#", "#.#", "##.", "#.#", "#.#"],
    "L": ["#..", "#..", "#..", "#..", "###"],
    "O": ["###", "#.#", "#.#", "#.#", "###"],
    "R": ["##.", "#.#", "##.", "#.#", "#.#"],
    " ": ["..", "..", "..", "..", ".."],
}


def big(text, px="█"):
    rows = [""] * 5
    blank = " " * len(px)
    for ch in text:
        glyph = FONT.get(ch.upper(), FONT[" "])
        for i in range(5):
            rows[i] += "".join(px if c == "#" else blank for c in glyph[i]) + blank
    return [r.rstrip() for r in rows]
