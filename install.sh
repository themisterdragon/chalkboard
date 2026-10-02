#!/bin/sh
# Install chalkboard.pyz as the `chalkboard` command (terminal) and chalkboard-gui.pyz as
# `chalkboard-gui` (window), on Linux/macOS. On Linux the window version also gets an app-menu entry.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src="$here/chalkboard.pyz"
[ -f "$src" ] || src="$here/dist/chalkboard.pyz"
[ -f "$src" ] || { echo "chalkboard.pyz not found next to install.sh or in dist/"; exit 1; }
gui="$(dirname "$src")/chalkboard-gui.pyz"
command -v python3 >/dev/null || { echo "python3 is required"; exit 1; }
dest="${PREFIX:-$HOME/.local}/bin"
mkdir -p "$dest"
cp "$src" "$dest/chalkboard"
chmod +x "$dest/chalkboard"
echo "installed $dest/chalkboard"
if [ -f "$gui" ]; then
    cp "$gui" "$dest/chalkboard-gui"
    chmod +x "$dest/chalkboard-gui"
    echo "installed $dest/chalkboard-gui"
    if [ "$(uname)" = Linux ]; then
        share="${XDG_DATA_HOME:-$HOME/.local/share}"
        mkdir -p "$share/applications" "$share/icons"
        python3 -c 'import sys, zipfile; sys.stdout.buffer.write(zipfile.ZipFile(sys.argv[1]).read("chalkboard/data/icon.png"))' \
            "$gui" > "$share/icons/chalkboard-gui.png"
        cat > "$share/applications/chalkboard-gui.desktop" <<EOF
[Desktop Entry]
Version=1.0
Name=Chalkboard (Window)
Comment=Lesson planner
Exec=$dest/chalkboard-gui
Terminal=false
Type=Application
Icon=$share/icons/chalkboard-gui.png
StartupWMClass=Chalkboard
Categories=Education;Office;
EOF
        echo "installed $share/applications/chalkboard-gui.desktop"
        python3 -c 'import tkinter' 2>/dev/null || \
            echo "note: chalkboard-gui needs Tk (Arch: sudo pacman -S tk; Debian/Ubuntu: sudo apt install python3-tk)"
    fi
fi
case ":$PATH:" in *":$dest:"*) ;; *) echo "note: add $dest to your PATH";; esac
