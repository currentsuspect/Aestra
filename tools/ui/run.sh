#!/usr/bin/env bash
# Run Aestra on a private X display, for seeing a change on screen without touching the real desktop.
#
#   tools/ui/run.sh [--display :78] [--project file.aes] [path/to/bin/Aestra]
#
# - Its own Xvfb (started if not running), 1366x768, the size of the founder's laptop screen.
# - Its own HOME and XDG folders under ~/.cache/aestra-ui/<display>/, so presets, settings, autosaves and
#   logs never land in the real ones. They survive reboots; delete the folder to start clean.
# - No sound at all: /dev/snd and the audio sockets are hidden (bubblewrap), so Aestra opens its Dummy Output.
#   Never mute a test instance with pactl instead: PipeWire remembers mute per app name ("RtAudio"), and the
#   next real Aestra would start muted.
# - No session bus, so a file dialog is zenity on this display, never a portal dialog on the real screen.
#
# Stop it with tools/ui/stop.sh. Drive it with tools/ui/drive.py.
set -euo pipefail

display=":78"
project=""
bin=""
while [ $# -gt 0 ]; do
    case "$1" in
        --display) display="$2"; shift 2 ;;
        --project) project="$2"; shift 2 ;;
        -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
        *) bin="$1"; shift ;;
    esac
done
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
if [ -z "$bin" ]; then
    for candidate in "$repo"/build*/bin/Aestra; do
        [ -x "$candidate" ] && { bin="$candidate"; break; }
    done
fi
[ -x "$bin" ] || { echo "run.sh: no Aestra binary (pass its path)" >&2; exit 1; }
command -v Xvfb >/dev/null || { echo "run.sh: Xvfb is not installed" >&2; exit 1; }
command -v bwrap >/dev/null || { echo "run.sh: bubblewrap (bwrap) is needed to keep the test instance silent" >&2; exit 1; }

state="${AESTRA_UI_STATE:-$HOME/.cache/aestra-ui}/${display#:}"
mkdir -p "$state/home" "$state/data" "$state/config" "$state/state"

"$here/stop.sh" --display "$display" >/dev/null 2>&1 || true

# The display: start it detached, so it outlives this shell.
num="${display#:}"
if [ ! -S "/tmp/.X11-unix/X$num" ]; then
    setsid nohup Xvfb "$display" -screen 0 1366x768x24 -nolisten tcp >"$state/xvfb.log" 2>&1 </dev/null &
    for _ in $(seq 50); do [ -S "/tmp/.X11-unix/X$num" ] && break; sleep 0.1; done
fi

rm -f "$state/data/Aestra/crash_flag"

runtime="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
hide=(--tmpfs /dev/snd)
for sock in "$runtime/pipewire-0" "$runtime/pulse/native"; do
    [ -e "$sock" ] && hide+=(--ro-bind /dev/null "$sock")
done

args=()
[ -n "$project" ] && args+=("$(realpath "$project")")

cd "$state"
env -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS \
    HOME="$state/home" XDG_DATA_HOME="$state/data" XDG_CONFIG_HOME="$state/config" XDG_STATE_HOME="$state/state" \
    DISPLAY="$display" SDL_VIDEODRIVER=x11 XDG_SESSION_TYPE=x11 GDK_BACKEND=x11 \
    DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent" \
    PIPEWIRE_REMOTE=/nonexistent PULSE_SERVER=unix:/nonexistent JACK_NO_START_SERVER=1 \
    AESTRA_MUSE_PORT="${AESTRA_MUSE_PORT:-419${num: -2}}" \
    setsid nohup bwrap --dev-bind / / "${hide[@]}" --chdir "$state" "$bin" "${args[@]}" \
    >"$state/app.log" 2>&1 </dev/null &

# Wait for the window, then fill the screen with it (Xvfb has no window manager).
python3 "$here/drive.py" --display "$display" waitwindow 30 fit wait 3
echo "Aestra on $display (state: $state, log: $state/app.log)"
grep -m1 "Using audio device" "$state/app.log" || true
