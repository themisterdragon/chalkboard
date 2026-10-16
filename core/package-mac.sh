#!/bin/sh
# Make Chalkboard.app for macOS from the program cross.sh builds (dist/macos/chalkboard, one file for
# Apple silicon and Intel), and on a Mac, sign it (ad hoc: the whole app, so a download opens with
# right-click > Open instead of being called "damaged") and wrap it in a DMG.
#   sh core/package-mac.sh [program]      -> core/dist/macos/Chalkboard.app (+ Chalkboard-<v>-macOS.dmg on a Mac)
set -e
# the program's path as given (from wherever this was run), before moving into core/
[ -n "$1" ] && BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
cd "$(dirname "$0")"
BIN=${BIN:-dist/macos/chalkboard}
[ -f "$BIN" ] || { echo "No $BIN: build it first (sh core/cross.sh macos)."; exit 1; }
VERSION=$(sed -n 's/^#define CHALKBOARD_VERSION "\(.*\)"/\1/p' src/version.h)
[ -x chalkboard-core ] || sh build.sh >/dev/null
APP=dist/macos/Chalkboard.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/Chalkboard"
chmod 755 "$APP/Contents/MacOS/Chalkboard"
sed "s/@VERSION@/$VERSION/g" packaging/macos/Info.plist > "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"
./chalkboard-core --write-icon "$APP/Contents/Resources/Chalkboard.icns"
if [ "$(uname -s)" != Darwin ]; then
    echo "$APP (not signed: on a Mac, run this again to sign it and make the DMG)"
    exit 0
fi
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
ditto "$APP" "$stage/Chalkboard.app"
ln -s /Applications "$stage/Applications"
cp "packaging/macos/Read Me First.txt" "$stage/"
DMG="dist/Chalkboard-$VERSION-macOS.dmg"
hdiutil create -volname "Chalkboard $VERSION" -srcfolder "$stage" -fs HFS+ -format UDZO -ov "$DMG" >/dev/null
echo "$APP (signed ad hoc)"
echo "$DMG"
