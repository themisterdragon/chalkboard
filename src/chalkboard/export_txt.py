"""Plain-text output: easy to paste into Google Classroom, an LMS, or an email."""

import textwrap

from .markup import plain
from .organizers import text_lines

WIDTH = 78


def unmark(b):
    """A copy of block b with **bold** / *italic* / __underline__ marks removed."""
    b = dict(b)
    for k in ("text", "label", "title"):
        if isinstance(b.get(k), str):
            b[k] = plain(b[k])
    for k in ("left", "right"):
        if isinstance(b.get(k), list):
            b[k] = [plain(x) for x in b[k]]
    if b.get("t") == "days":
        b["days"] = [(d, plain(p)) for d, p in b["days"]]
    return b


def render_lines(doc, width=WIDTH):
    out = []
    w = lambda text, ind=0, sub=None: textwrap.wrap(text, width, initial_indent=" " * ind,
                                                    subsequent_indent=" " * (ind if sub is None else sub)) or [""]
    for b in doc["blocks"]:
        b = unmark(b)
        t = b["t"]
        if t == "title":
            if b.get("align") == "left":
                out += [b["text"], "=" * min(width, len(b["text"]))]
            else:
                out += [b["text"].center(width).rstrip(), ("=" * min(width, len(b["text"]) + 4)).center(width).rstrip()]
        elif t == "subtitle":
            out += [b["text"] if b.get("align") == "left" else b["text"].center(width).rstrip(), ""]
        elif t == "fields":
            out += ["   ".join(f"{k}: {'_' * (24 if k == 'Name' else 10)}" for k in b["items"]), ""]
        elif t == "h1":
            out += ["", b["text"].upper()] + (["-" * min(width, len(b["text"]))] if b.get("rule", True) else [])
        elif t == "p":
            out += w(b["text"]) + [""]
        elif t == "bullet":
            ind = 2 + 2 * b.get("indent", 0)
            out += w("- " + b["text"], ind, ind + 2)
        elif t == "kv":
            out += w(f"{b['label']} {b['text']}".rstrip(), 0, 4) + [""]
        elif t == "q":
            text = b["text"] + (f"  {b['points']}" if b.get("points") else "")
            out += [""] + w(f"{b['num']:<4}{text}", 0, 4)
        elif t == "choice":
            mark = "  <== " if b["correct"] else ""
            out += w(f"{b['label']} {b['text']}{mark}", 6, 9)
        elif t == "choice_inline":
            out.append("      " + "      ".join(
                (f"[{it}]" if b["correct"] == i else it) for i, it in enumerate(b["items"])))
        elif t == "lines":
            out += ["    " + "_" * (width - 4) for _ in range(b["n"])]
        elif t in ("blank", "box"):
            out += [""] * min(b["n"], 6)
        elif t == "answer":
            out += w("ANSWER: " + b["text"], 4, 12)
        elif t == "match":
            n = max(len(b["left"]), len(b["right"]))
            for i in range(n):
                left = b["left"][i] if i < len(b["left"]) else ""
                right = b["right"][i] if i < len(b["right"]) else ""
                blank = f"_{b['key'][i]}_" if (b["key"] and left) else ("___" if left else "   ")
                rl = f"{'ABCDEFGHIJ'[i]}. {right}" if right else ""
                out.append(f"    {blank} {left[:30]:<32}{rl}".rstrip())
        elif t == "organizer":
            out += [""] + text_lines(b, width)
        elif t == "passage":
            out.append("")
            if b.get("title"):
                out.append(b["title"].center(width).rstrip())
            for p in (b.get("text") or "").split("\n"):
                if p.strip():
                    out += w(p.strip(), 8, 4) + [""]
        elif t == "rule":
            out.append("-" * width)
        elif t == "grid":
            widths = [max(6, int((width - len(b["cols"]) - 1) * share)) for _, share in b["cols"]]
            sep = "+" + "+".join("-" * cw for cw in widths) + "+"
            out += ["", sep, "|" + "|".join(label.center(cw) for (label, _), cw in zip(b["cols"], widths)) + "|", sep]
            for _ in range(b["rows"]):
                out += ["|" + "|".join(" " * cw for cw in widths) + "|"] * 2 + [sep]
        elif t == "days":
            for label, prompt in b["days"]:
                out += ["", f"{label.upper():<{width - 20}}Date: ______________"]
                if prompt.strip():
                    out += w(prompt.strip(), 2, 2)
                out += ["  " + "_" * (width - 2) for _ in range(3)]
        elif t == "pagebreak":
            out += ["", "=" * width, "(NEXT PAGE)".center(width).rstrip(), ""]
    if doc.get("footer"):
        out += ["", "-" * width, doc["footer"]]
    return out


def render_txt(doc, path, **_):
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(render_lines(doc)) + "\n")
