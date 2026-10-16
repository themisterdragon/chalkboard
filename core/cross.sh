#!/bin/sh
# Build Chalkboard for every computer from one Linux (or macOS) machine, with Zig as the compiler.
#   sh core/cross.sh [windows] [linux] [linux-arm] [macos]     (no names: all of them)
#   ZIG=/path/to/zig sh core/cross.sh windows
# Programs go to core/dist/<target>/. Nothing is linked but the operating system's own libraries:
# GTK (Linux) is opened when the program starts, so the same file runs with or without a desktop.
set -e
cd "$(dirname "$0")"
ZIG=${ZIG:-$(command -v zig || echo "$HOME/.cache/tally-build/zig/zig")}
[ -x "$ZIG" ] || { echo "Zig not found (set ZIG=/path/to/zig)."; exit 1; }
VERSION=$(sed -n 's/^#define CHALKBOARD_VERSION "\(.*\)"/\1/p' src/version.h)
FLAGS=${CXXFLAGS:--O2}
JOBS=${JOBS:-$( (nproc || sysctl -n hw.ncpu) 2>/dev/null || echo 4)}
TARGETS=${*:-windows linux linux-arm macos}

# the data folder goes inside the program (a host build of the packer)
mkdir -p build
[ build/embed -nt tools/embed.cpp ] || ${CXX:-c++} -std=c++17 -O1 -o build/embed tools/embed.cpp
build/embed ../src/chalkboard/data build/resources.tmp
if cmp -s build/resources.tmp build/resources.cpp; then rm build/resources.tmp; else mv build/resources.tmp build/resources.cpp; fi
# a host copy of Chalkboard draws the icons
[ -x chalkboard-core ] || sh build.sh >/dev/null

# compile every file for one target (in parallel, kept in build/<name>/ between runs), then link
build_one() {   # name zig-target out-file extra-link-flags...
    name=$1 target=$2 out=$3
    shift 3
    obj=build/$name
    mkdir -p "$obj" "$(dirname "$out")"
    newest_h=$(ls -t src/*.h | head -n 1)
    todo=""
    for f in src/*.cpp build/resources.cpp; do
        o="$obj/$(basename "$f" .cpp).o"
        if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ "$newest_h" -nt "$o" ]; then todo="$todo $f"; fi
    done
    echo "$name: compiling$( [ -z "$todo" ] && echo ' (nothing changed)')"
    [ -z "$todo" ] || printf '%s\n' $todo | xargs -P "$JOBS" -n 1 sh -c '
        f=$1; o="'"$obj"'/$(basename "$f" .cpp).o"
        "'"$ZIG"'" c++ -target '"$target"' -std=c++17 '"$FLAGS"' -Isrc -c -o "$o" "$f" || { rm -f "$o"; exit 255; }' sh
    objs=""
    for f in src/*.cpp build/resources.cpp; do objs="$objs $obj/$(basename "$f" .cpp).o"; done
    "$ZIG" c++ -target "$target" $FLAGS -s -o "$out" $objs "$@"
    rm -f "${out%.exe}.pdb"
    echo "$name: $out"
}

for t in $TARGETS; do
    case $t in
    windows)
        # the icon, version, and manifest, compiled into the program
        ./chalkboard-core --write-icon packaging/windows/chalkboard.ico
        v1=$(echo "$VERSION" | sed 's/[^0-9.].*//' | cut -d. -f1) v2=$(echo "$VERSION" | sed 's/[^0-9.].*//' | cut -d. -f2) v3=$(echo "$VERSION" | sed 's/[^0-9.].*//' | cut -d. -f3)
        mkdir -p build/windows
        "$ZIG" rc /:target x86_64-windows-gnu /d CB_V1=$v1 /d CB_V2=$v2 /d CB_V3=${v3:-0} /d "CB_VERSION=\"$VERSION\"" \
            /fo build/windows/chalkboard.res packaging/windows/chalkboard.rc
        build_one windows x86_64-windows-gnu dist/windows/Chalkboard.exe build/windows/chalkboard.res \
            -Wl,--subsystem,windows -static -lole32 -lshell32 -luuid -lgdi32 -luser32 -ladvapi32 -lbcrypt
        ;;
    linux)
        # glibc 2.28 (2018) and later: Debian 10, Ubuntu 18.10, Fedora 29, ChromeOS Linux, Arch...
        build_one linux x86_64-linux-gnu.2.28 dist/linux-x86_64/chalkboard -ldl -lpthread
        ;;
    linux-arm)
        build_one linux-arm aarch64-linux-gnu.2.28 dist/linux-arm64/chalkboard -ldl -lpthread
        ;;
    macos)
        # macOS 11 and later: one program for Apple silicon and Intel (AppKit is opened when it starts)
        build_one macos-arm aarch64-macos.11.0 build/macos/chalkboard-arm64
        build_one macos-intel x86_64-macos.11.0 build/macos/chalkboard-x86_64
        mkdir -p dist/macos
        python3 -I tools/lipo.py dist/macos/chalkboard build/macos/chalkboard-arm64 build/macos/chalkboard-x86_64
        chmod 755 dist/macos/chalkboard
        echo "macos: dist/macos/chalkboard (Apple silicon + Intel)"
        ;;
    *)
        echo "unknown target $t (windows, linux, linux-arm, macos)"
        exit 2
        ;;
    esac
done
