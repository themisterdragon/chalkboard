#!/bin/sh
# Build dist/Chalkboard-Terminal-<version>.dmg on macOS: Chalkboard Terminal.app, an Applications shortcut to
# drag it onto, and Read Me First.txt. Needs hdiutil (macOS only); run build_mac.py first.
set -e
root=$(cd "$(dirname "$0")/.." && pwd)
version=$(sed -n 's/^__version__ = "\(.*\)"/\1/p' "$root/src/chalkboard/__init__.py")
zip="$root/dist/Chalkboard-Terminal-macOS.zip"
[ -f "$zip" ] || { echo "run scripts/build_mac.py first"; exit 1; }
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
ditto -x -k "$zip" "$stage"
ln -s /Applications "$stage/Applications"
out="$root/dist/Chalkboard-Terminal-$version.dmg"
hdiutil create -volname "Chalkboard Terminal $version" -srcfolder "$stage" -fs HFS+ -format UDZO -ov "$out" >/dev/null
echo "built $out"
