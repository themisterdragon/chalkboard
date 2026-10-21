#!/bin/sh
# Fuzz the core's readers with clang's libFuzzer (bug hunting; needs clang).
#   sh core/fuzz/run.sh [seconds per target] [seed folder]
set -e
cd "$(dirname "$0")/.."
SECS=${1:-60}
SEEDS=${2:-}
# GCC 16's std::stable_sort frees its scratch buffer in a way clang's sanitizer misreads as a
# new/free mismatch (GCC's own sanitizer is fine with it); that one check is off.
export ASAN_OPTIONS=alloc_dealloc_mismatch=0:detect_leaks=1
mkdir -p build/fuzz
[ -f build/resources.cpp ] || sh build.sh >/dev/null
SRC=$(ls src/*.cpp | grep -v src/main.cpp | grep -v src/check.cpp)
for t in ${TARGETS:-1 2 3 4 5 6 7 8 9 10 11 12}; do
  echo "== target $t"
  clang++ -std=c++17 -g -O1 -DTARGET=$t -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -Isrc \
    -o build/fuzz/f$t fuzz/fuzz.cpp $SRC build/resources.cpp
  mkdir -p build/fuzz/corpus$t
  [ -n "$SEEDS" ] && [ -d "$SEEDS/$t" ] && cp -n "$SEEDS/$t"/* build/fuzz/corpus$t/ 2>/dev/null || true
  build/fuzz/f$t -max_total_time="$SECS" -max_len=200000 -timeout=20 -rss_limit_mb=4096 -artifact_prefix=build/fuzz/crash$t- \
    build/fuzz/corpus$t 2>&1 | grep -E "^#.*DONE|ERROR|SUMMARY|crash|Test unit written" | tail -3
done
