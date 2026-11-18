#!/usr/bin/env python3
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
"""SPEC-028 R0: idle benchmark for a desktop app, fair between apps.

Launches an app on an X display (a private Xvfb, or the real session through XWayland), with network off and a
wiped state directory, waits for its window, lets it settle, then samples for 40 s: idle CPU, resident memory,
threads and, on a real session, what Hyprland/Xwayland spend drawing it.

    python3 scripts/perf/idle_bench.py --name aestra --display :1 --cwd . -- build-linux/bin/Aestra
    python3 scripts/perf/idle_bench.py --name other  --display :1 --cwd /path -- ./app

Rules this exists to enforce (each one cost a wrong number once):
  * State is wiped before every run. A SIGKILLed run leaves a crash flag and the next one opens a recovery dialog
    that animates, and an animation is not idle.
  * The same display, the same load, no network (an updater phoning home is not idle either).
  * Software GL (Xvfb, llvmpipe) shows draw cost as CPU in its own threads; a real GPU hides it in the compositor.
    Run both and say which a number came from.
  * Report the box load: a busy machine stretches every figure.

Needs python-xlib. Prints one JSON line.
"""
import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time

from Xlib import X, display


def pids_named(*names):
    out = []
    for d in os.listdir("/proc"):
        if d.isdigit():
            try:
                with open(f"/proc/{d}/comm") as f:
                    if f.read().strip() in names:
                        out.append(int(d))
            except OSError:
                pass
    return out


def ticks_of(pids):
    total = 0
    for pid in pids:
        try:
            with open(f"/proc/{pid}/stat") as f:
                s = f.read().rsplit(")", 1)[1].split()
            total += int(s[11]) + int(s[12])
        except (OSError, IndexError, ValueError):
            pass
    return total


def proc_stat(pid):
    try:
        with open(f"/proc/{pid}/stat") as f:
            s = f.read().rsplit(")", 1)[1].split()
        return int(s[11]) + int(s[12]), int(s[21]) * os.sysconf("SC_PAGE_SIZE") / 1048576, int(s[17])
    except (OSError, IndexError, ValueError):
        return None


def load1():
    with open("/proc/loadavg") as f:
        return float(f.read().split()[0])


def big_window_mapped(disp):
    root = disp.screen().root
    for child in root.query_tree().children:
        try:
            attrs, geom = child.get_attributes(), child.get_geometry()
            if attrs.map_state == X.IsViewable and geom.width > 300 and geom.height > 200:
                return True
        except Exception:
            pass
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True, help="label; also names the throwaway HOME")
    ap.add_argument("--display", required=True, help="X display, e.g. :1 (XWayland) or a private Xvfb")
    ap.add_argument("--cwd", default=".")
    ap.add_argument("--settle", type=float, default=20.0, help="seconds after the window appears before sampling")
    ap.add_argument("--sample", type=float, default=40.0, help="seconds of sampling")
    ap.add_argument("--with-network", action="store_true", help="skip the network namespace (default: off)")
    ap.add_argument("--env", action="append", default=[], metavar="K=V")
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == "--" else a.cmd
    if not cmd:
        ap.error("give the command after --")

    home = os.path.join(os.environ.get("TMPDIR", "/tmp"), f"idle-bench-{a.name}")
    shutil.rmtree(home, ignore_errors=True)
    for sub in ("", "/xdg", "/cfg", "/state"):
        os.makedirs(home + sub, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if k != "WAYLAND_DISPLAY"}
    env.update(DISPLAY=a.display, SDL_VIDEODRIVER="x11", XDG_SESSION_TYPE="x11", HOME=home,
               XDG_DATA_HOME=home + "/xdg", XDG_CONFIG_HOME=home + "/cfg", XDG_STATE_HOME=home + "/state")
    for kv in a.env:
        k, _, v = kv.partition("=")
        env[k] = v
    if not a.with_network:
        cmd = ["unshare", "-rn"] + cmd

    t0 = time.time()
    proc = subprocess.Popen(cmd, cwd=a.cwd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            start_new_session=True)
    disp = display.Display(a.display)
    first = None
    while time.time() - t0 < 90 and proc.poll() is None:
        if big_window_mapped(disp):
            first = time.time() - t0
            break
        time.sleep(0.05)

    result = {"name": a.name, "alive": proc.poll() is None, "first_window_s": None if first is None else round(first, 2)}
    if result["alive"]:
        comp = pids_named("Hyprland", "Xwayland")
        clk = os.sysconf("SC_CLK_TCK")
        time.sleep(a.settle)
        first_sample, t_a, c_a, l_a = proc_stat(proc.pid), time.time(), ticks_of(comp), load1()
        peak = first_sample[1] if first_sample else 0.0
        for _ in range(int(a.sample)):
            time.sleep(1)
            s = proc_stat(proc.pid)
            if s:
                peak = max(peak, s[1])
        last_sample, t_b, c_b, l_b = proc_stat(proc.pid), time.time(), ticks_of(comp), load1()
        if first_sample and last_sample:
            result.update(
                idle_cpu_pct=round((last_sample[0] - first_sample[0]) / clk / (t_b - t_a) * 100, 2),
                rss_mb=round(last_sample[1], 1), rss_peak_mb=round(peak, 1), threads=last_sample[2],
                compositor_cpu_pct=round((c_b - c_a) / clk / (t_b - t_a) * 100, 2), load_start=l_a, load_end=l_b)
    else:
        result["note"] = "exited before showing a window"
    print(json.dumps(result))
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(proc.pid, sig)
        except OSError:
            pass
        time.sleep(1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
