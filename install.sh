#!/bin/sh
# Install chalkboard.pyz as the `chalkboard` command (terminal) and chalkboard-gui.pyz as
# `chalkboard-gui` (window), on Linux/macOS. On Linux the window version also gets the "Chalkboard" app-menu entry.
set -e
here=$(cd "$(dirname "$0")" && pwd)
src="$here/chalkboard.pyz"
[ -f "$src" ] || src="$here/dist/chalkboard.pyz"
[ -f "$src" ] || { echo "chalkboard.pyz not found next to install.sh or in dist/"; exit 1; }
gui="$(dirname "$src")/chalkboard-gui.pyz"

# Prerequisites: Python 3 and Tk (for the window version), and a PDF tool for board slide PNGs.
# Anything missing is offered through the system's package manager (it asks for your password).
need_py=""; need_tk=""; need_pdf=""
command -v python3 >/dev/null || need_py=1
[ -n "$need_py" ] || python3 -c 'import tkinter' 2>/dev/null || need_tk=1
command -v pdftoppm >/dev/null || command -v mutool >/dev/null || command -v gs >/dev/null || need_pdf=1
[ "$(uname)" = Darwin ] && need_pdf=""  # Macs make PNGs with the built-in sips
if [ -n "$need_py$need_tk$need_pdf" ]; then
    if [ "$(uname)" = Darwin ]; then
        [ -n "$need_py" ] && { echo "Chalkboard needs Python 3: run  xcode-select --install  (or get it from python.org), then run this again."; exit 1; }
        [ -n "$need_tk" ] && echo "note: the window version needs Tk; Python from python.org includes it."
    else
        if command -v pacman >/dev/null; then pm="pacman -S --needed --noconfirm"; py=python; tk=tk; pdf=poppler
        elif command -v apt-get >/dev/null; then pm="apt-get install -y"; py=python3; tk=python3-tk; pdf=poppler-utils
        elif command -v dnf >/dev/null; then pm="dnf install -y"; py=python3; tk=python3-tkinter; pdf=poppler-utils
        elif command -v zypper >/dev/null; then pm="zypper --non-interactive install"; py=python3; tk=python3-tk; pdf=poppler-tools
        else pm=""
        fi
        pkgs="${need_py:+$py }${need_tk:+$tk }${need_pdf:+$pdf}"
        echo "Chalkboard needs a few things this computer doesn't have yet:"
        [ -n "$need_py" ] && echo "  - Python 3 (Chalkboard is written in it)"
        [ -n "$need_tk" ] && echo "  - Tk (draws the window version)"
        [ -n "$need_pdf" ] && echo "  - Poppler (turns board slides into PNG pictures)"
        if [ -z "$pm" ]; then
            echo "Install them with your package manager, then run this again."
            [ -n "$need_py" ] && exit 1
        elif [ -t 0 ]; then
            printf "Install them now? It will ask for your password. [Y/n] "
            read -r ans
            case "$ans" in
                n*|N*) [ -n "$need_py" ] && exit 1 ;;
                *) [ "$pm" = "apt-get install -y" ] && sudo apt-get update -qq
                   # shellcheck disable=SC2086
                   sudo $pm $pkgs || echo "note: that didn't finish; Chalkboard still installs, but see above." ;;
            esac
        else
            echo "Install them with:  sudo $pm $pkgs"
            [ -n "$need_py" ] && exit 1
        fi
    fi
fi
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
Name=Chalkboard
Comment=Lesson planner
Exec=$dest/chalkboard-gui
Terminal=false
Type=Application
Icon=$share/icons/chalkboard-gui.png
StartupWMClass=Chalkboard
Categories=Education;Office;
EOF
        echo "installed $share/applications/chalkboard-gui.desktop"
    fi
fi
case ":$PATH:" in *":$dest:"*) ;; *) echo "note: add $dest to your PATH";; esac
