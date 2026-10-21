#!/usr/bin/env python3
"""Frame benchmark harness for Aestra.

Launches the real app with AESTRA_FRAME_STATS=2, drives it with a scripted
pointer workload, and parses the app's own per-frame instrumentation out of
runtime_log.txt.

Why the app's own numbers and not wall-clock: on a 2-core Folio the scheduler
noise between the app and the harness would swamp a millisecond-scale signal.
The app measures frame work inside its own loop, so the number is the thing we
actually want to compare between builds.

Usage:
  bench-frames.py --app <path/to/Aestra> [--project <file.aes>] [--seconds 20]
                  [--workload idle|sweep|timeline|cycle] [--out <json>]
                  [--window WxH]
"""

import argparse
import json
import os
import re
import signal
import statistics
import subprocess
import sys
import time
from pathlib import Path

FRAME_STATS = re.compile(
    r"\[FrameStats\] frames=(\d+) work avg=([\d.]+)ms p95=([\d.]+)ms max=([\d.]+)ms "
    r"over16\.7=(\d+) over33=(\d+)"
)
FRAME_WHY = re.compile(r"\[FrameWhy\](.*)")
DIRTY_WHO = re.compile(r"\[DirtyWho\](.*)")
RENDER_SELF = re.compile(r"\[RenderSelf\](.*)")
PHASE = re.compile(r"\[Phase\] ms/frame: tree=([\d.]+) submit=([\d.]+) swap=([\d.]+)")


def parse_kv(text):
    """' a=1 b=2.5' -> {'a': '1', 'b': '2.5'}"""
    out = {}
    for tok in text.split():
        if "=" not in tok:
            continue
        k, _, v = tok.partition("=")
        out[k] = v
    return out


def drive_sweep(x0, y0, x1, y1, hz=30):
    """Sweep the pointer across a band. Hover invalidation is the worst case
    the frame budget has to survive, so this is the default workload."""
    period = 1.0 / hz
    while True:
        for x, y in ((x0, y0), (x1, y1), (x1, y0), (x0, y1)):
            subprocess.run(["ydotool", "mousemove", "--", str(x), str(y)], check=False)
            time.sleep(period)


def drive_cycle(x0, y0, x1, y1, hz=30):
    """Same coverage as sweep, but a straight back-and-forth: it reads as one
    continuous gesture rather than a corner-to-corner drag."""
    period = 1.0 / hz
    while True:
        for x in range(x0, x1, 8):
            subprocess.run(["ydotool", "mousemove", "--", str(x), str(y0)], check=False)
            time.sleep(period)
        for x in range(x1, x0, -8):
            subprocess.run(["ydotool", "mousemove", "--", str(x), str(y0)], check=False)
            time.sleep(period)


def read_cpu_ticks(pid):
    """utime+stime of a process, in ticks (CLK_TCK=100 on Linux)."""
    try:
        with open(f"/proc/{pid}/stat") as f:
            content = f.read()
    except OSError:
        return None
    close = content.rfind(")")
    fields = content[close + 1:].split()
    if len(fields) < 13:
        return None
    return int(fields[11]) + int(fields[12])


def focus_aestra_window():
    """Raise and focus the app.

    An unfocused SDL window does not receive pointer events, so a sweep over
    it changes nothing and the run quietly measures the idle path while
    claiming to measure interaction.
    """
    try:
        subprocess.run(["hyprctl", "dispatch", "focuswindow", "class:aestra"],
                       capture_output=True, timeout=5)
    except Exception:
        pass


def find_aestra_window():
    """Geometry AND workspace of the running Aestra window under Hyprland.

    The sweep has to land ON the app: a pointer that moves over another
    window changes nothing, and the run then measures the idle path while
    claiming to measure interaction. The app opens on its own workspace, so
    the workspace has to be switched to as well.
    """
    try:
        out = subprocess.run(["hyprctl", "clients", "-j"], capture_output=True,
                             text=True, timeout=5).stdout
        clients = json.loads(out)
    except Exception:
        return None
    for c in clients:
        cls = (c.get("class", "") or "").lower()
        if cls == "aestra" or cls.startswith("aestra"):
            at = c.get("at", [0, 0])
            size = c.get("size", [0, 0])
            return (int(at[0]), int(at[1]), int(size[0]), int(size[1]),
                    int((c.get("workspace") or {}).get("id", 0)))
    return None


def switch_workspace(workspace_id):
    """Switch Hyprland's active workspace (the app opens on its own)."""
    try:
        subprocess.run(["hyprctl", "dispatch", f"hl.dsp.focus({{workspace={workspace_id}}})"],
                       capture_output=True, timeout=5)
    except Exception:
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True, help="path to the Aestra binary")
    ap.add_argument("--project", default=None)
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--settle", type=float, default=6.0)
    ap.add_argument("--workload", default="sweep", choices=["idle", "sweep", "cycle", "playback"])
    ap.add_argument("--muse-port", type=int, default=41952)
    ap.add_argument("--muse-bin", default=None)
    ap.add_argument("--out", default=None)
    ap.add_argument("--cwd", default=None, help="working dir for the app (log lands here)")
    ap.add_argument("--window", default="1600x900")
    ap.add_argument("--label", default="run")
    args = ap.parse_args()

    app = Path(args.app).resolve()
    cwd = Path(args.cwd).resolve() if args.cwd else app.parent
    log = cwd / "runtime_log.txt"
    # The frame probe logs to STDOUT (the console logger), not to the file
    # logger. Capture stdout — the file log only carries the startup lines.
    stdout_path = Path("/tmp/opencode") / f"frames-stdout-{args.label}.log"

    # Truncate the log so the run's lines are unambiguous.
    if log.exists():
        log.unlink()

    env = dict(os.environ)
    env["AESTRA_FRAME_STATS"] = "2"
    env["AESTRA_MUSE_PORT"] = env.get("AESTRA_MUSE_PORT", "41952")

    cmd = [str(app)]
    if args.project:
        cmd.append(str(Path(args.project).resolve()))

    print(f"[bench] launching: {' '.join(cmd)}  (cwd={cwd})", flush=True)
    stdout_file = open(stdout_path, "wb")
    proc = subprocess.Popen(cmd, cwd=str(cwd), env=env,
                            stdout=stdout_file, stderr=subprocess.STDOUT)
    driver = None
    home_workspace = None
    try:
        time.sleep(args.settle)
        if proc.poll() is not None:
            print(f"[bench] app exited early with {proc.returncode}", file=sys.stderr)
            stdout_file.close()
            return 3

        w, _, h = args.window.partition("x")
        w, h = int(w), int(h)

        # Find the app's own window so the pointer actually lands on it.
        rect = find_aestra_window()
        try:
            home_workspace = int(json.loads(subprocess.run(
                ["hyprctl", "activeworkspace", "-j"], capture_output=True,
                text=True, timeout=5).stdout)["id"])
        except Exception:
            pass
        if rect:
            wx, wy, ww, wh, wsw = rect
            print(f"[bench] Aestra window at {wx},{wy} size {ww}x{wh} workspace {wsw}", flush=True)
            if wsw and home_workspace and wsw != home_workspace:
                switch_workspace(wsw)
                time.sleep(1.5)
            x0 = wx + int(ww * 0.30)
            x1 = wx + int(ww * 0.92)
            y = wy + int(wh * 0.55)
        else:
            x0, x1, y = int(w * 0.12), int(w * 0.88), int(h * 0.55)
            print("[bench] Aestra window not found; using the requested geometry", flush=True)

        if args.workload == "sweep":
            driver = subprocess.Popen(
                [sys.executable, __file__, "--__drive", "sweep", str(x0), str(y), str(x1), str(y)],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        elif args.workload == "cycle":
            driver = subprocess.Popen(
                [sys.executable, __file__, "--__drive", "cycle", str(x0), str(y), str(x1), str(y)],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        cpu_start = read_cpu_ticks(proc.pid)
        wall_start = time.monotonic()

        health = None
        transport = None
        if args.workload == "playback":
            muse = args.muse_bin or str(cwd / "muse")
            play_out = subprocess.run([muse, "--port", str(args.muse_port), "play"],
                                     capture_output=True, text=True, timeout=30)
            print(f"[bench] muse play -> {play_out.stdout.strip()[:120]}", flush=True)
            time.sleep(2.0)  # let the graph settle before the measured window
            transport = subprocess.run([muse, "--port", str(args.muse_port), "get_transport"],
                                       capture_output=True, text=True, timeout=30).stdout
            print(f"[bench] transport -> {transport.strip()[:200]}", flush=True)

        time.sleep(args.seconds)

        if args.workload == "playback":
            muse = args.muse_bin or str(cwd / "muse")
            health = subprocess.run([muse, "--port", str(args.muse_port), "get_audio_health"],
                                    capture_output=True, text=True, timeout=30).stdout
            subprocess.run([muse, "--port", str(args.muse_port), "stop"],
                           capture_output=True, timeout=30)
        cpu_end = read_cpu_ticks(proc.pid)
        wall_end = time.monotonic()
    finally:
        if driver is not None:
            driver.terminate()
            try:
                driver.wait(timeout=3)
            except subprocess.TimeoutExpired:
                driver.kill()
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
        stdout_file.close()
        if home_workspace:
            switch_workspace(home_workspace)

    if not stdout_path.exists():
        print("[bench] no stdout capture produced", file=sys.stderr)
        return 4

    text = stdout_path.read_text(errors="replace")
    frames = []
    why = {}
    dirty = {}
    self_ms = {}
    phase = []
    for line in text.splitlines():
        m = FRAME_STATS.search(line)
        if m:
            frames.append({
                "frames": int(m.group(1)),
                "avg": float(m.group(2)),
                "p95": float(m.group(3)),
                "max": float(m.group(4)),
                "over16": int(m.group(5)),
                "over33": int(m.group(6)),
            })
            continue
        m = FRAME_WHY.search(line)
        if m:
            for k, v in parse_kv(m.group(1)).items():
                why[k] = why.get(k, 0) + int(float(v))
            continue
        m = DIRTY_WHO.search(line)
        if m:
            for k, v in parse_kv(m.group(1)).items():
                dirty[k] = dirty.get(k, 0) + int(float(v))
            continue
        m = RENDER_SELF.search(line)
        if m:
            for k, v in parse_kv(m.group(1)).items():
                self_ms[k] = self_ms.get(k, 0.0) + float(v)
            continue
        m = PHASE.search(line)
        if m:
            phase.append((float(m.group(1)), float(m.group(2)), float(m.group(3))))

    if not frames:
        # No presented frame means the app elided every frame — which is the
        # idle-elision gate doing its job, but only if the app was actually
        # alive and had a window. Say which, so a broken run cannot be read as
        # a perfect one.
        alive = proc.returncode
        print(f"[bench] no presented frames. app exit={alive}", file=sys.stderr)
        for line in text.splitlines()[-15:]:
            print("  " + line, file=sys.stderr)
        return 6

    total_frames = sum(f["frames"] for f in frames)
    # Weighted mean of the per-second averages.
    wavg = sum(f["avg"] * f["frames"] for f in frames) / max(1, total_frames)
    wp95 = max(f["p95"] for f in frames)
    wmax = max(f["max"] for f in frames)
    over16 = sum(f["over16"] for f in frames)
    over33 = sum(f["over33"] for f in frames)
    nwin = len(frames)
    tree = sum(p[0] for p in phase) / max(1, len(phase))
    submit = sum(p[1] for p in phase) / max(1, len(phase))
    swap = sum(p[2] for p in phase) / max(1, len(phase))

    result = {
        "label": args.label,
        "workload": args.workload,
        "project": args.project,
        "seconds_measured": args.seconds,
        "windows": nwin,
        "presented_frames": total_frames,
        "fps": round(total_frames / max(1e-9, nwin), 1),
        "process_cpu_percent": (round((cpu_end - cpu_start) / 100.0 / (wall_end - wall_start) * 100.0, 1)
                                if cpu_start is not None and cpu_end is not None else None),
        "process_cpu_seconds": (round((cpu_end - cpu_start) / 100.0, 2)
                                if cpu_start is not None and cpu_end is not None else None),
        "audio_health": health,
        "transport": transport,
        "work_avg_ms": round(wavg, 3),
        "work_p95_ms": round(wp95, 3),
        "work_max_ms": round(wmax, 3),
        "frames_over_16_7ms": over16,
        "frames_over_33ms": over33,
        "phase_ms_per_frame": {"tree": round(tree, 3), "submit": round(submit, 3), "swap": round(swap, 3)},
        "present_reasons": dict(sorted(why.items(), key=lambda kv: -kv[1])),
        "dirty_origins": dict(sorted(dirty.items(), key=lambda kv: -kv[1])),
        "render_self_ms_per_frame": {k: round(v / nwin, 3) for k, v in
                                     sorted(self_ms.items(), key=lambda kv: -kv[1])},
        "raw_windows": frames,
    }

    print(json.dumps(result, indent=2))
    if args.out:
        Path(args.out).write_text(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--__drive":
        mode, x0, y0, x1, y1 = sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
        (drive_cycle if mode == "cycle" else drive_sweep)(x0, y0, x1, y1)
        sys.exit(0)
    sys.exit(main())
