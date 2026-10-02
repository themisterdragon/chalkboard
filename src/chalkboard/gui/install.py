"""Put the window version on this computer: the setup wizard's last step.

Windows (.exe): copy into %LOCALAPPDATA%\\Programs\\Chalkboard and add Start-menu / desktop shortcuts.
macOS (.app): when it's running from the downloaded disk image, copy it to Applications.
Linux (.pyz): copy to ~/.local/bin/chalkboard-gui with an app-menu entry.
Anything else (pip, running from source) is already installed as far as we're concerned.
"""

import os
import shutil
import subprocess
import sys

from ..logo import png

WIN_NAME = "Chalkboard Window.exe"


def _xdg_data():
    return os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")


def _mac_app():
    """The .app bundle we're running from, or None."""
    exe = os.path.abspath(sys.executable)
    parts = exe.split(os.sep)
    for i in range(len(parts) - 1, 0, -1):
        if parts[i].endswith(".app"):
            return os.sep.join(parts[:i + 1])
    return None


def _pyz():
    p = os.path.abspath(sys.argv[0]) if sys.argv and sys.argv[0] else ""
    return p if p.endswith(".pyz") and os.path.isfile(p) else None


def plan():
    """What installing would do here: {"kind", "source", "target", "text"}, or None if there's nothing to do."""
    frozen = getattr(sys, "frozen", False)
    if sys.platform == "win32" and frozen:
        base = os.environ.get("LOCALAPPDATA") or os.path.expanduser(r"~\AppData\Local")
        target = os.path.join(base, "Programs", "Chalkboard", WIN_NAME)
        if os.path.normcase(os.path.abspath(sys.executable)) == os.path.normcase(target):
            return None
        return {"kind": "windows", "source": sys.executable, "target": target,
                "text": "Chalkboard will be copied to your Programs folder and added to the Start menu."}
    if sys.platform == "darwin" and frozen:
        app = _mac_app()
        if not app or not app.startswith("/Volumes/"):
            return None  # already copied somewhere (Applications, Desktop, ...)
        apps = "/Applications" if os.access("/Applications", os.W_OK) else os.path.expanduser("~/Applications")
        return {"kind": "mac", "source": app, "target": os.path.join(apps, os.path.basename(app)),
                "text": f"Chalkboard Window will be copied to your {os.path.basename(apps)} folder and reopened "
                        "from there. Then you can eject the disk image."}
    pyz = _pyz()
    if pyz and sys.platform.startswith("linux"):
        target = os.path.join(os.path.expanduser("~/.local/bin"), "chalkboard-gui")
        if os.path.abspath(pyz) == target:
            return None
        return {"kind": "linux", "source": pyz, "target": target,
                "text": "Chalkboard will be installed as chalkboard-gui, with an entry in your app menu."}
    return None


def install(p, desktop_shortcut=True):
    """Do the plan. Returns a short note for the user. Raises OSError (or CalledProcessError) on failure."""
    kind, src, target = p["kind"], p["source"], p["target"]
    os.makedirs(os.path.dirname(target), exist_ok=True)
    if kind == "windows":
        shutil.copy2(src, target)
        start = os.path.join(os.environ.get("APPDATA") or os.path.expanduser(r"~\AppData\Roaming"),
                             "Microsoft", "Windows", "Start Menu", "Programs")
        links = [os.path.join(start, "Chalkboard.lnk")]
        if desktop_shortcut:
            links.append(os.path.join(os.path.expanduser("~"), "Desktop", "Chalkboard.lnk"))
        for link in links:
            os.makedirs(os.path.dirname(link), exist_ok=True)
            ps = ("$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:CB_LINK); "
                  "$s.TargetPath = $env:CB_TARGET; $s.WorkingDirectory = Split-Path $env:CB_TARGET; "
                  "$s.IconLocation = $env:CB_TARGET + ',0'; $s.Description = 'Chalkboard lesson planner'; $s.Save()")
            subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", ps], check=True,
                           env=dict(os.environ, CB_LINK=link, CB_TARGET=target),
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        return "Chalkboard is in your Start menu" + (" and on your desktop." if desktop_shortcut else ".")
    if kind == "mac":
        if os.path.exists(target):
            shutil.rmtree(target)
        subprocess.run(["ditto", src, target], check=True)
        subprocess.run(["xattr", "-dr", "com.apple.quarantine", target], check=False)
        return "Chalkboard Window is in your Applications folder."
    # linux
    shutil.copy2(src, target)
    os.chmod(target, 0o755)
    share = _xdg_data()
    icon = os.path.join(share, "icons", "chalkboard-gui.png")
    os.makedirs(os.path.dirname(icon), exist_ok=True)
    with open(icon, "wb") as f:
        f.write(png(size=256, margin=16))
    entry = os.path.join(share, "applications", "chalkboard-gui.desktop")
    os.makedirs(os.path.dirname(entry), exist_ok=True)
    with open(entry, "w", encoding="utf-8") as f:
        f.write("[Desktop Entry]\nVersion=1.0\nName=Chalkboard (Window)\nComment=Lesson planner\n"
                f"Exec={target}\nTerminal=false\nType=Application\nIcon={icon}\nStartupWMClass=Chalkboard\n"
                "Categories=Education;Office;\n")
    return "Chalkboard is in your app menu as “Chalkboard (Window)”."


def relaunch(p):
    """Open the installed copy (macOS: the copy in Applications)."""
    if p["kind"] == "mac":
        subprocess.Popen(["open", "-n", p["target"]])
        return True
    return False
