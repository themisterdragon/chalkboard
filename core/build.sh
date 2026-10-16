#!/bin/sh
# Build Chalkboard's C++ core with nothing but a C++17 compiler.
#   sh core/build.sh            -> core/chalkboard-core
#   CXXFLAGS="-g -fsanitize=address,undefined" sh core/build.sh   (bug hunting)
#   sh core/build.sh ui-demo    -> core/build/ui-demo (the window toolkit on its own)
# Each source file is compiled on its own (several at once) and kept in build/obj-*, so a rebuild
# only redoes the files that changed (or all of them when a header changes).
set -e
cd "$(dirname "$0")"
CXX=${CXX:-$(command -v c++ || command -v g++ || command -v clang++)}
[ -n "$CXX" ] || { echo "No C++ compiler found (install g++ or clang++)."; exit 1; }
mkdir -p build
# the data folder (standards, prompts, mascots) goes inside the program
[ build/embed -nt tools/embed.cpp ] || $CXX -std=c++17 -O1 -o build/embed tools/embed.cpp
build/embed ../src/chalkboard/data build/resources.tmp
if cmp -s build/resources.tmp build/resources.cpp; then rm build/resources.tmp; else mv build/resources.tmp build/resources.cpp; fi
FLAGS=${CXXFLAGS:--O2}
# a folder of objects for each set of flags, so a sanitizer build doesn't undo a normal one
OBJ=build/obj-$(printf '%s %s' "$CXX" "$FLAGS" | cksum | cut -d' ' -f1)
mkdir -p "$OBJ"
newest_h=$(ls -t src/*.h | head -n 1)
JOBS=${JOBS:-$( (nproc || sysctl -n hw.ncpu) 2>/dev/null || echo 4)}
echo "Building with $CXX $FLAGS ($JOBS at a time) ..."
TODO=""
for f in src/*.cpp build/resources.cpp; do
    o="$OBJ/$(basename "$f" .cpp).o"
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ "$newest_h" -nt "$o" ]; then TODO="$TODO $f"; fi
done
[ -z "$TODO" ] || printf '%s\n' $TODO | xargs -P "$JOBS" -I{} sh -c '
    f={}; o="'"$OBJ"'/$(basename "$f" .cpp).o"
    echo "  $f"
    '"$CXX"' -std=c++17 '"$FLAGS"' -Wall -Wextra -Isrc -c -o "$o" "$f" || { rm -f "$o"; exit 255; }'
# only the objects of files that are still here
OBJS=""
for f in src/*.cpp build/resources.cpp; do OBJS="$OBJS $OBJ/$(basename "$f" .cpp).o"; done
if [ "$1" = "ui-demo" ]; then
    $CXX -std=c++17 $FLAGS -Wall -Wextra -Isrc -o build/ui-demo tools/ui_demo.cpp $(echo $OBJS | tr ' ' '\n' | grep -v '/main.o$')
    echo "Done: core/build/ui-demo"
    exit 0
fi
$CXX $FLAGS -o chalkboard-core $OBJS
echo "Done: core/chalkboard-core"
