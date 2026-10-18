#!/bin/sh
# The offline promise (SECURITY.md), checked: no networking code in the source, and no built program
# that links to networking functions or libraries. Any finding fails the check.
#   sh core/tools/check_offline.sh [program ...]
set -e
bad=0
src=$(cd "$(dirname "$0")/.." && pwd)

# 1. The source: no sockets, name lookups, or web/network libraries on any system.
net='sys/socket\.h|netinet/|arpa/inet\.h|netdb\.h|winsock|ws2_32|wininet|winhttp|urlmon|URLDownload|getaddrinfo|gethostbyname|NSURL|CFNetwork|CFSocket|libcurl|curl_easy'
if grep -rniE "$net" "$src/src" "$src/tools" --include='*.cpp' --include='*.h' | grep -v 'check_offline'; then
    echo "check_offline: networking code in the source (above)"; bad=1
fi

# 2. Each program: what it asks the system to link in.
for p in "$@"; do
    [ -f "$p" ] || { echo "check_offline: no program at $p"; bad=1; continue; }
    case "$p" in
    *.exe)  # Windows: the DLL names and imported functions are plain text in the file
        if LC_ALL=C grep -aoiE 'ws2_32\.dll|wsock32\.dll|wininet\.dll|winhttp\.dll|urlmon\.dll|WSAStartup|getaddrinfo' "$p" | sort -u | grep .; then
            echo "check_offline: $p links to networking (above)"; bad=1
        fi ;;
    *)
        if [ "$(uname)" = Darwin ]; then syms=$(nm -u "$p" 2>/dev/null || true); libs=$(otool -L "$p")
        else syms=$(nm -D --undefined-only "$p" 2>/dev/null || true); libs=$(ldd "$p" 2>/dev/null || true); fi
        if [ -z "$syms" ]; then   # nm can't read it here: look for the names as plain text instead
            syms=$(LC_ALL=C grep -aoE '_?(socket|connect|getaddrinfo|gethostbyname|sendto|recvfrom)' "$p" | sort -u || true)
        fi
        if printf '%s\n' "$syms" | grep -E '\b_?(socket|connect|getaddrinfo|gethostbyname|sendto|recvfrom)(@|$)'; then
            echo "check_offline: $p imports networking functions (above)"; bad=1
        fi
        if printf '%s\n' "$libs" | grep -iE 'CFNetwork|libcurl|libssl|Network\.framework'; then
            echo "check_offline: $p links to a networking library (above)"; bad=1
        fi ;;
    esac
    echo "checked $p"
done

[ $bad = 0 ] && echo "check_offline: no networking found"
exit $bad
