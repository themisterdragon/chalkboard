#!/bin/sh
# Install the standalone chalkboard.pyz as the `chalkboard` command (Linux/macOS).
set -e
here=$(cd "$(dirname "$0")" && pwd)
src="$here/chalkboard.pyz"
[ -f "$src" ] || src="$here/dist/chalkboard.pyz"
[ -f "$src" ] || { echo "chalkboard.pyz not found next to install.sh or in dist/"; exit 1; }
command -v python3 >/dev/null || { echo "python3 is required"; exit 1; }
dest="${PREFIX:-$HOME/.local}/bin"
mkdir -p "$dest"
cp "$src" "$dest/chalkboard"
chmod +x "$dest/chalkboard"
echo "installed $dest/chalkboard"
case ":$PATH:" in *":$dest:"*) ;; *) echo "note: add $dest to your PATH";; esac
