#!/usr/bin/env python3
"""Check Chalkboard's offline promise (SECURITY.md); the release build runs this.

1. No source file imports networking code.
2. Loading the app pulls in no networking module, not even through the standard library
   (xml.sax.saxutils, for one, imports urllib).
3. The guard Chalkboard turns on at startup (chalkboard/offline.py) refuses network connections,
   other programs, and web addresses, for plugins too, and a plugin that tries is switched off
   while the rest of Chalkboard carries on.
4. Making a lesson and a quiz with every question type and exporting them in every format opens no
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
PNG_TOOLS = {"pdftoppm", "mutool", "gs", "gswin64c", "gswin32c", "sips", "qlmanage", "powershell"}

bad = []
for f in sorted(src.rglob("*.py")):
    for node in ast.walk(ast.parse(f.read_text(encoding="utf-8"), str(f))):
        names = [a.name for a in node.names] if isinstance(node, ast.Import) else \
            [node.module or ""] if isinstance(node, ast.ImportFrom) and not node.level else []
        bad += [f"{f.relative_to(root)}:{node.lineno} imports {n}" for n in names if n.split(".")[0] in NETWORK]
if bad:
    sys.exit("network code found:\n  " + "\n  ".join(bad))
print(f"ok: no networking imports in {len(list(src.rglob('*.py')))} source files")

GUARD_TEST = r"""
import os, socket, subprocess, sys, urllib.request
sys.path.insert(0, sys.argv[1])
os.environ.update(XDG_DATA_HOME=sys.argv[2], APPDATA=sys.argv[2], HOME=sys.argv[2])
from chalkboard import offline, plugins, exporting
from chalkboard.store import Store
offline.enforce()
tries = {
    "connect": lambda: socket.create_connection(("192.0.2.1", 80), timeout=1),
    "dns lookup": lambda: socket.getaddrinfo("example.com", 443),
    "urllib": lambda: urllib.request.urlopen("http://example.com", timeout=1),
    "curl": lambda: subprocess.run(["curl", "http://example.com"]),
    "browser": lambda: subprocess.run(["xdg-open", "https://example.com"]),
    "shell": lambda: os.system("true"),
}
for name, fn in tries.items():
    try:
        fn()
        sys.exit("GUARD MISSED: " + name)
    except offline.OfflineError:
        pass
    except urllib.error.URLError as e:  # urllib wraps the refusal
        if not isinstance(e.reason, offline.OfflineError):
            raise
s = Store()
d = os.path.join(s.dir, "plugins")
os.makedirs(d, exist_ok=True)
open(os.path.join(d, "good.py"), "w").write(
    "def setup(cb):\n"
    "    cb.add_export('MD', 'Markdown', '.md', lambda doc, path, item: open(path, 'w').write('# ' + doc['title']))\n"
    "    import socket\n"
    "    cb.after_export(lambda paths: socket.create_connection(('192.0.2.1', 80), timeout=1))\n")
open(os.path.join(d, "broken.py"), "w").write("def setup(cb):\n    raise RuntimeError('oops')\n")
plugins.load(s.dir)
assert any("broken.py" in p for p in plugins.problems), plugins.problems
plugins.take_problems()
lesson = s.data["lessons"][0] if s.data["lessons"] else None
from chalkboard import store as st
lesson = st.new_lesson(s.settings); lesson["title"] = "Guard"
s.settings["export_dir"] = os.path.join(sys.argv[2], "out")
files = exporting.export(s, "lesson", lesson, "MD")
assert files and open(files[0]).read() == "# Guard", files
assert any("AFTER EXPORT" in p for p in plugins.problems), plugins.problems  # the network try was refused
print("ok")
"""
import subprocess  # noqa: E402
out = subprocess.run([sys.executable, "-c", GUARD_TEST, str(root / "src"), tempfile.mkdtemp()],
                     capture_output=True, text=True)
if out.returncode or out.stdout.strip() != "ok":
    sys.exit("offline guard test failed:\n" + out.stdout + out.stderr)
print("ok: the offline guard refuses the network, other programs, and web addresses (plugins too)")

tmp = tempfile.mkdtemp()
os.environ.update(XDG_DATA_HOME=tmp, APPDATA=tmp, HOME=tmp)
sys.path.insert(0, str(root / "src"))
ran = []
from chalkboard.offline import popen_program  # noqa: E402  (no networking imports)
from chalkboard.offline import OfflineError, _check_program  # noqa: E402

# Windows hands the guard one command line: a board-PNG tool passes, anything else is refused
_check_program(*popen_program((None, r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.EXE -NoProfile', 0, 0)))
_check_program(*popen_program((None, r'"C:\Program Files\gs\bin\gswin64c.exe" -q', 0, 0)))
for line in (r'C:\Windows\System32\curl.exe -O x', r'"C:\Program Files\Browser\browser.exe" https://example.com',
             r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.EXE iwr https://example.com'):
    try:
        _check_program(*popen_program((None, line, 0, 0)))
        sys.exit(f"the guard let a Windows command through: {line}")
    except OfflineError:
        pass
print("ok: the guard reads Windows command lines")


def hook(event, args):
    if event.startswith(("socket.", "urllib.", "http.", "ftplib.", "smtplib.", "webbrowser.")):
        os.write(2, f"OFFLINE PROMISE BROKEN: {event} {args!r}\n".encode())
        os._exit(1)
    if event in ("subprocess.Popen", "os.system", "os.posix_spawn", "os.exec", "os.startfile"):
        if event == "subprocess.Popen":
            exe = popen_program(args)[0]  # the guard's own reading (Windows passes one command line)
        else:
            exe = args[0] if event != "os.system" else args[0].split()[0]
        exe = os.path.basename(str(exe).replace("\\", "/"))
        exe = os.path.splitext(exe)[0].lower()
        if exe not in PNG_TOOLS:
            os.write(2, f"UNEXPECTED PROGRAM: {event} {args!r}\n".encode())
            os._exit(1)
        ran.append(exe)


sys.addaudithook(hook)

# this script's own imports don't count (on Python 3.12, pathlib loads urllib.parse)
already = set(sys.modules)
from chalkboard import store as st  # noqa: E402
from chalkboard.exporting import FORMAT_ORDER, ExportError, export  # noqa: E402
import chalkboard.app  # noqa: E402,F401  (the terminal app; imports curses, not a network module)

loaded = sorted(m for m in sys.modules if m.split(".")[0] in NETWORK and m not in already)
if loaded:
    sys.exit("networking modules got loaded: " + ", ".join(loaded))
print("ok: loading the app loads no networking module")

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
