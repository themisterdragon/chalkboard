#!/bin/sh
# Build dist/Chalkboard-Window-<version>.dmg on macOS: a standalone "Chalkboard Window.app" (the window
# version, with Python and Tk inside, for Intel and Apple silicon), an Applications shortcut, and a read-me.
# Needs a universal2 Python with PyInstaller (python.org's installer is universal2):
#   PYTHON=/Library/Frameworks/Python.framework/Versions/3.12/bin/python3 sh scripts/build_mac_window.sh
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
py=${PYTHON:-python3}
version=$(sed -n 's/^__version__ = "\(.*\)"/\1/p' "$root/src/chalkboard/__init__.py")
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# --add-data, not --collect-data: chalkboard isn't pip-installed here, so --collect-data found nothing
# and the app shipped without its SEL prompts
printf 'from chalkboard.gui import main\n\nmain()\n' > "$work/entry.py"
"$py" -m PyInstaller --noconfirm --windowed --name "Chalkboard Window" --target-arch universal2 \
    --icon "$root/macos/Chalkboard.icns" --osx-bundle-identifier local.chalkboard.window \
    --paths "$root/src" --add-data "$root/src/chalkboard/data:chalkboard/data" --distpath "$work/dist" --workpath "$work/build" \
    --specpath "$work" "$work/entry.py"
app="$work/dist/Chalkboard Window.app"
plutil -replace CFBundleShortVersionString -string "$version" "$app/Contents/Info.plist"
plutil -replace CFBundleVersion -string "$version" "$app/Contents/Info.plist"
plutil -replace LSApplicationCategoryType -string public.app-category.education "$app/Contents/Info.plist"
codesign --force --deep --sign - "$app"   # ad-hoc: keeps the edited Info.plist valid
stage="$work/stage"
mkdir -p "$stage"
ditto "$app" "$stage/Chalkboard Window.app"
ln -s /Applications "$stage/Applications"
cp "$root/macos/Read Me First (Window).txt" "$stage/Read Me First.txt"
mkdir -p "$root/dist"
out="$root/dist/Chalkboard-Window-$version.dmg"
hdiutil create -volname "Chalkboard Window $version" -srcfolder "$stage" -fs HFS+ -format UDZO -ov "$out" >/dev/null
echo "built $out"
