#!/usr/bin/env python3
"""Check Chalkboard's offline promise (SECURITY.md); the release build runs this.

1. No source file imports networking code.
2. Making a lesson and a quiz with every question type and exporting them in every format opens no
   network connection and runs no program except the local PNG tools. An audit hook stops the
   process the moment it tries.
"""
import ast
import os
import pathlib
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
src = root / "src" / "chalkboard"
NETWORK = {"socket", "ssl", "http", "urllib", "requests", "ftplib", "smtplib", "poplib", "imaplib", "telnetlib",
           "xmlrpc", "asyncio", "webbrowser", "email"}
PNG_TOOLS = {"pdftoppm", "mutool", "gs", "gswin64c", "gswin32c", "sips"}

bad = []
for f in sorted(src.rglob("*.py")):
    for node in ast.walk(ast.parse(f.read_text(encoding="utf-8"), str(f))):
        names = [a.name for a in node.names] if isinstance(node, ast.Import) else \
            [node.module or ""] if isinstance(node, ast.ImportFrom) and not node.level else []
        bad += [f"{f.relative_to(root)}:{node.lineno} imports {n}" for n in names if n.split(".")[0] in NETWORK]
if bad:
    sys.exit("network code found:\n  " + "\n  ".join(bad))
print(f"ok: no networking imports in {len(list(src.rglob('*.py')))} source files")

tmp = tempfile.mkdtemp()
os.environ.update(XDG_DATA_HOME=tmp, APPDATA=tmp, HOME=tmp)
sys.path.insert(0, str(root / "src"))
ran = []


def hook(event, args):
    if event.startswith(("socket.", "urllib.", "http.", "ftplib.", "smtplib.", "webbrowser.")):
        os.write(2, f"OFFLINE PROMISE BROKEN: {event} {args!r}\n".encode())
        os._exit(1)
    if event in ("subprocess.Popen", "os.system", "os.posix_spawn", "os.exec", "os.startfile"):
        exe = os.path.basename(str(args[0] if event != "os.system" else args[0].split()[0]))
        exe = os.path.splitext(exe)[0].lower()
        if exe not in PNG_TOOLS:
            os.write(2, f"UNEXPECTED PROGRAM: {event} {args!r}\n".encode())
            os._exit(1)
        ran.append(exe)


sys.addaudithook(hook)

from chalkboard import store as st  # noqa: E402
from chalkboard.exporting import FORMAT_ORDER, ExportError, export  # noqa: E402

s = st.Store()
s.settings["export_dir"] = os.path.join(tmp, "exports")
lesson = st.new_lesson(s.settings)
lesson.update(title="Offline Check", unit="Unit 1", targets="- I can test", bell_ringer=s.good_thing())
quiz = st.new_assessment(s.settings, "Quiz")
quiz["title"] = "Offline Quiz"
for t, _, _ in st.QUESTION_TYPES:
    q = st.new_question(t)
    q.update(prompt="A ___ question?", title="Part", text="Some text.")
    if t == "mc":
        q.update(choices=["a", "b"], answer=0)
    if t == "match":
        q["pairs"] = [["x", "y"], ["z", "w"]]
    quiz["questions"].append(q)
lesson["assessments"] = [quiz["id"]]
s.data["lessons"].append(lesson)
s.data["assessments"].append(quiz)
for kind in st.SHEET_KINDS:
    a = st.new_assessment(s.settings, kind)
    a["title"] = kind
    s.data["assessments"].append(a)
s.save()
files = []
for fmt in FORMAT_ORDER["lesson"]:
    try:
        files += export(s, "lesson", lesson, fmt)
    except ExportError as e:  # no PNG tool on this machine is fine; it's not a network problem
        print(f"  (lesson {fmt}: {e})")
for a in s.data["assessments"]:
    files += export(s, "assessment", a, "ALL")
print(f"ok: exported {len(files)} files with no network access"
      + (f"; ran only {', '.join(sorted(set(ran)))}" if ran else ""))
