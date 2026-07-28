#!/usr/bin/env bash
# Verify window restore-geometry survival under a real stacking WM (#655).
#
# Hyprland tiles Aestra and reports _NET_WM_STATE_MAXIMIZED for a tiled window,
# so maximize/restore semantics cannot be tested on the host session. This runs a
# nested X server with openbox inside it. Nothing about the host display or
# compositor configuration is touched.
#
# Requires: xorg-server-xephyr, openbox, wmctrl  (xprop optional but recommended)
#
# Usage:  ./verify-window-restore.sh /path/to/Aestra
#
# NOTE: -screen is deliberately FIXED (no -resizeable). A tiling host compositor
# will size the Xephyr *window* to its own layout, and with -resizeable the
# nested screen shrinks to match — which silently invalidates the test by making
# the requested window larger than the screen.

set -uo pipefail

APP="${1:-./build-linux/bin/Aestra}"
DISP=":9"
SCREEN="1000x700"
SEED_W=760
SEED_H=540
STATE="$HOME/.local/share/Aestra/ui_state.json"
BACKUP="$(mktemp)"

cleanup() {
    [ -n "${APP_PID:-}" ] && kill -KILL "$APP_PID" 2>/dev/null
    [ -n "${OB_PID:-}" ] && kill -KILL "$OB_PID" 2>/dev/null
    [ -n "${XE_PID:-}" ] && kill -KILL "$XE_PID" 2>/dev/null
    rm -f "$HOME/.local/share/Aestra/crash_flag"
    if [ -s "$BACKUP" ]; then
        cp "$BACKUP" "$STATE"
        echo "[cleanup] ui_state.json restored"
    fi
    rm -f "$BACKUP"
}
trap cleanup EXIT

[ -x "$APP" ] || { echo "no executable at $APP"; exit 2; }
cp "$STATE" "$BACKUP" 2>/dev/null || true

echo "== starting nested session on $DISP ($SCREEN) =="
Xephyr "$DISP" -screen "$SCREEN" -ac >/dev/null 2>&1 &
XE_PID=$!
for _ in $(seq 1 20); do [ -S "/tmp/.X11-unix/X${DISP#:}" ] && break; sleep 1; done
DISPLAY="$DISP" openbox --replace >/dev/null 2>&1 &
OB_PID=$!
sleep 3
DISPLAY="$DISP" wmctrl -m >/dev/null 2>&1 || { echo "FAIL: no WM in nested session"; exit 1; }

echo "== seeding ${SEED_W}x${SEED_H}, maximized=false =="
python3 - "$STATE" "$SEED_W" "$SEED_H" <<'PY'
import json, sys
p, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
try:    d = json.load(open(p))
except Exception: d = {}
d["window"] = {"x": 40, "y": 30, "width": w, "height": h, "maximized": False}
json.dump(d, open(p, "w"), indent=2)
PY

DISPLAY="$DISP" SDL_VIDEODRIVER=x11 "$APP" >/tmp/aestra-nested.log 2>&1 &
APP_PID=$!
for _ in $(seq 1 60); do DISPLAY="$DISP" wmctrl -l 2>/dev/null | grep -qi aestra && break; sleep 1; done
sleep 4

geom() { DISPLAY="$DISP" wmctrl -lG | grep -i aestra | awk '{print $5"x"$6}'; }
state() { local id; id=$(DISPLAY="$DISP" wmctrl -l | grep -i aestra | awk '{print $1}')
          DISPLAY="$DISP" xprop -id "$id" _NET_WM_STATE 2>/dev/null | grep -o MAXIMIZED_VERT || echo "not-maximized"; }

echo
echo "-- CHECK 1: a window seeded non-maximized must NOT be born maximized --"
echo "   geometry: $(geom)   wm state: $(state)"
[ "$(geom)" = "${SEED_W}x${SEED_H}" ] && echo "   PASS" || echo "   FAIL (expected ${SEED_W}x${SEED_H})"

echo
echo "-- CHECK 2: maximize, quit, and the RESTORE size must survive --"
DISPLAY="$DISP" wmctrl -r "Aestra v1.0" -b add,maximized_vert,maximized_horz
sleep 3
echo "   maximized to: $(geom)"
DISPLAY="$DISP" wmctrl -c "Aestra v1.0"
echo "   (dismiss the unsaved-changes dialog if it appears)"
for _ in $(seq 1 30); do kill -0 "$APP_PID" 2>/dev/null || break; sleep 1; done

python3 - "$STATE" "$SEED_W" "$SEED_H" <<'PY'
import json, sys
p, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
d = json.load(open(p))["window"]
print(f"   persisted: {d['width']}x{d['height']} maximized={d['maximized']}")
ok = d["width"] == w and d["height"] == h and d["maximized"] is True
print("   PASS" if ok else f"   FAIL (expected {w}x{h} maximized=True)")
PY
