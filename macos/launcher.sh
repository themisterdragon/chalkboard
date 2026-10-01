#!/bin/sh
# Chalkboard.app/Contents/MacOS/Chalkboard
# Chalkboard is a full-screen terminal program, so the app just opens a
# Terminal window running the bundled chalkboard.pyz.

contents=$(cd "$(dirname "$0")/.." && pwd)
pyz="$contents/Resources/chalkboard.pyz"

alert() {
    osascript -e 'on run argv' -e 'display dialog (item 1 of argv) with title "Chalkboard" buttons {"OK"} default button 1 with icon caution' -e 'end run' "$1" >/dev/null 2>&1
}

py=""
for p in /opt/homebrew/bin/python3 /usr/local/bin/python3 \
         /Library/Frameworks/Python.framework/Versions/Current/bin/python3 /usr/bin/python3; do
    [ -x "$p" ] || continue
    # /usr/bin/python3 is only a stub until Apple's Command Line Tools are installed
    if [ "$p" = /usr/bin/python3 ] && ! xcode-select -p >/dev/null 2>&1; then
        continue
    fi
    py=$p
    break
done

if [ -z "$py" ]; then
    alert "Chalkboard needs Python 3, which comes free with Apple's Command Line Developer Tools.

Click Install in the next window. When it finishes, open Chalkboard again."
    xcode-select --install >/dev/null 2>&1
    exit 0
fi

if ! "$py" -c 'import sys, curses; sys.exit(sys.version_info < (3, 8))' >/dev/null 2>&1; then
    alert "Chalkboard needs Python 3.8 or newer with curses, but $py doesn't have it.

Install Python from python.org or Homebrew, then open Chalkboard again."
    exit 1
fi

# single-quote for the shell command Terminal will run
q() { printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"; }

# set the window title, size it to 110x34 so the big banner fits, then run
cmd="printf '\\033]0;Chalkboard\\007\\033[8;34;110t'; clear; exec $(q "$py") $(q "$pyz")"

osascript - "$cmd" <<'EOF' >/dev/null
on run argv
    set wasRunning to application "Terminal" is running
    tell application "Terminal"
        if wasRunning then
            do script (item 1 of argv)
        else
            -- launching Terminal opens a window; use it instead of making a second one
            activate
            repeat 50 times
                if (count of windows) > 0 then exit repeat
                delay 0.1
            end repeat
            do script (item 1 of argv) in window 1
        end if
        activate
    end tell
end run
EOF
