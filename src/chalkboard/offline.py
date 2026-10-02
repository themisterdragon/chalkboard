"""Chalkboard works offline, and holds itself to it while it runs.

enforce() installs a Python audit hook (it can't be removed once in place) that refuses, for the
whole program and any plugin it loads:
- every network connection and name lookup, and
- starting any program except the few local tools Chalkboard uses (below), or handing one of
  them a web address.

A plugin that wants files in the cloud saves them into a folder Google Drive, OneDrive, or Dropbox
already syncs. (Python code can still reach the OS directly through ctypes; this guard covers
everything else. Only add plugins you trust.) scripts/check_offline.py tests the guard.
"""

import os
import re
import sys

TOOLS = {
    "pdftoppm", "mutool", "gs", "gswin64c", "gswin32c", "sips", "qlmanage", "powershell",  # board PNGs
    "fc-match",                                     # the terminal view's font (Linux)
    "gsettings", "defaults",                        # following the computer's dark mode and accent color
    "ditto", "xattr",                               # copying the Mac app into Applications
    "open", "xdg-open",                             # opening an exported file or folder
}
NET = {"socket.connect", "socket.bind", "socket.sendto", "socket.sendmsg", "socket.getaddrinfo",
       "socket.gethostbyname", "socket.gethostbyaddr", "socket.getnameinfo"}
WEB = re.compile(r"(://|^www\.|^mailto:)", re.I)
_on = False


class OfflineError(PermissionError):
    pass


def _check_program(path, args=()):
    exe = os.path.splitext(os.path.basename(str(path).replace("\\", "/")))[0].lower()
    if exe not in TOOLS:
        raise OfflineError(f"Chalkboard works offline and doesn't start other programs ({exe})")
    for a in args or ():
        if isinstance(a, (str, bytes)) and WEB.search(a if isinstance(a, str) else a.decode("utf-8", "replace")):
            raise OfflineError("Chalkboard works offline and doesn't open web addresses")


def popen_program(args):
    """(program, its arguments) from a subprocess.Popen audit event. On Windows the arguments arrive
    as one command line, like '"C:\\...\\powershell.EXE" -NoProfile ...'."""
    exe, argv = args[0], args[1]
    if isinstance(argv, bytes):
        argv = argv.decode("utf-8", "replace")
    if isinstance(argv, str):
        line = argv.strip()
        first, rest = ((line[1:].split('"', 1) + [""])[:2] if line.startswith('"')
                       else (line.split(None, 1) + [""])[:2])
        argv = [first, rest]
    else:
        argv = list(argv or [])
    return exe or (argv[0] if argv else ""), argv[1:]


def _hook(event, args):
    if event in NET:
        raise OfflineError("Chalkboard works offline: it doesn't connect to the internet")
    if event == "subprocess.Popen":
        _check_program(*popen_program(args))
    elif event in ("os.exec", "os.posix_spawn", "os.spawn"):
        _check_program(args[0], args[1] if len(args) > 1 else ())
    elif event == "os.system":
        raise OfflineError("Chalkboard works offline and doesn't run shell commands")
    elif event == "os.startfile":
        if WEB.search(str(args[0])):
            raise OfflineError("Chalkboard works offline and doesn't open web addresses")


def enforce():
    """Turn the guard on (once per process; it stays on)."""
    global _on
    if not _on:
        sys.addaudithook(_hook)
        _on = True
