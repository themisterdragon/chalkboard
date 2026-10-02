"""Example Chalkboard plugin: export lessons and assessments as Markdown (.md).

To use it, copy this file into the "plugins" folder inside Chalkboard's data folder (Settings
shows where the data file lives; make the folder if it isn't there) and restart Chalkboard.
"Markdown (.md)" then shows up as an export format. Delete the file to remove it.

A plugin gets the same document the PDF and Word exports draw: a title and a list of blocks,
each a dict with a "t" (type) such as "h1", "p", "bullet", "kv", or "q". See src/chalkboard/doc.py.
"""


def write_markdown(doc, path, item):
    out = []
    for b in doc["blocks"]:
        t = b["t"]
        if t == "title":
            out += [f"# {b['text']}", ""]
        elif t == "subtitle":
            out += [f"*{b['text']}*", ""]
        elif t == "h1":
            out += ["", f"## {b['text']}", ""]
        elif t == "bullet":
            out.append("  " * b.get("indent", 0) + f"- {b['text']}")
        elif t == "kv":
            out.append(f"- **{b['label']}** {b['text']}")
        elif t == "q":
            out += ["", f"{b['num']} {b['text']}" + (f" {b['points']}" if b.get("points") else "")]
        elif t == "choice":
            out.append(f"   {b['label']} {b['text']}")
        elif t == "check":
            out.append(f"- [ ] {b['text']}")
        elif t == "answer":
            out.append(f"   > Answer: {b['text']}")
        elif b.get("text"):  # paragraphs, passages, and anything newer: keep the words
            out += [b["text"], ""]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out).strip() + "\n")


def setup(chalkboard):
    chalkboard.add_export("MD", "Markdown (.md)", ".md", write_markdown)
