#!/bin/sh
# Run the window app through a script with no screen, on a COPY of a data file, with HOME pointed at
# a scratch folder too, so exports, backups, and settings can never touch real files.
#   sh core/tests/run_gui.sh [script] [data.json]     (default script: tests/walkthrough.txt)
# The copy opens in the Modern look, light, 14-point text (LOOK=glossy THEME=dark ... for others).
# With no data file Chalkboard starts empty and opens Setup first.
# Uses build/chalkboard-asan when it's there (CXXFLAGS="-g -O1 -fsanitize=address,undefined" sh core/build.sh,
# then copy chalkboard-core to build/chalkboard-asan): any memory error or leak is reported and fails the run.
# Pictures go to the scratch folder's out/ (it says where).
set -e
cd "$(dirname "$0")/.."
SCRIPT=$(cd "$(dirname "${1:-tests/walkthrough.txt}")" && pwd)/$(basename "${1:-tests/walkthrough.txt}")
DATA=${2:-}
BIN=./chalkboard-core
[ -x build/chalkboard-asan ] && BIN=build/chalkboard-asan
T=$(mktemp -d)
mkdir -p "$T/data" "$T/home/Documents" "$T/out"
if [ -n "$DATA" ]; then
    # the copy forgets any export or backup folder it remembers (they'd be real folders)
    python3 -I - "$DATA" "$T/data/data.json" <<'PY'
import json, os, sys
d = json.load(open(sys.argv[1], encoding="utf-8"))
st = d.setdefault("settings", {})
for k, v in list(st.items()):
    if isinstance(v, str) and (v.startswith("/") or (len(v) > 2 and v[1] == ":")):
        st[k] = ""
# the same window every time (the walk-through's clicks are for it): LOOK and THEME pick another
st.update({"gui_skin": os.environ.get("LOOK", "modern"), "gui_theme": os.environ.get("THEME", "light"), "gui_v3_look": True,
           "gui_text": 14, "gui_scale": 1, "gui_maximized": False, "gui_mode": "window", "gui_accent": ""})
json.dump(d, open(sys.argv[2], "w", encoding="utf-8"))
PY
fi
# (CHALKBOARD_TIMING=1 and CHALKBOARD_CHECK_SKIP=1 pass through: timings, and checking skipped frames)
env -i PATH=/usr/bin:/bin HOME="$T/home" CHALKBOARD_DATA="$T/data" ${CHALKBOARD_TIMING:+CHALKBOARD_TIMING=1} ${CHALKBOARD_CHECK_SKIP:+CHALKBOARD_CHECK_SKIP=1} \
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:log_path="$T/asan" UBSAN_OPTIONS=print_stacktrace=1:log_path="$T/ubsan" \
    "$BIN" --no-boot --script "$SCRIPT" "$T/out"
echo "pictures: $T/out ($(ls "$T/out" | wc -l))"
if ls "$T"/asan* "$T"/ubsan* >/dev/null 2>&1; then
    cat "$T"/asan* "$T"/ubsan* 2>/dev/null | head -60
    echo "MEMORY PROBLEMS (above)"
    exit 1
fi
echo "no memory problems found ($BIN)"
