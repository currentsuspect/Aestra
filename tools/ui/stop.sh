#!/usr/bin/env bash
# Stop the Aestra that tools/ui/run.sh started on a display (default :78). Matches by working folder, never by a
# name pattern, so it can only stop the test instance.
#   tools/ui/stop.sh [--display :78] [--xvfb]   (--xvfb also stops the display)
set -euo pipefail
display=":78"
xvfb=0
while [ $# -gt 0 ]; do
    case "$1" in
        --display) display="$2"; shift 2 ;;
        --xvfb) xvfb=1; shift ;;
        *) shift ;;
    esac
done
state="${AESTRA_UI_STATE:-$HOME/.cache/aestra-ui}/${display#:}"
for pid in $(pgrep -x Aestra || true); do
    [ "$(readlink "/proc/$pid/cwd" 2>/dev/null)" = "$state" ] && kill "$pid" && echo "stopped Aestra $pid"
done
if [ "$xvfb" = 1 ]; then
    for pid in $(pgrep -x Xvfb || true); do
        tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null | grep -q "^Xvfb $display " && kill "$pid" && echo "stopped Xvfb $display"
    done
fi
exit 0
