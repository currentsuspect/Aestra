#!/usr/bin/env python3
"""Audio benchmark harness for Aestra.

Runs the Folio perf collector against the canonical fixture in both of its
modes and prints a comparable summary:

  offline   the export-time subject: the fixture rendered on one thread.
            Deterministic, no device, so it is also the mode that can be
            run under callgrind for instruction-level attribution.

  playback  the live subject: a real device, so callback durations, deadline
            misses and xruns become non-zero. This is the number that says
            whether the engine keeps up on the target machine.

Raw JSONL goes to --outdir; the summary goes to stdout and --out. The raw
samples are the evidence; the summary is a convenience.

Usage:
  bench-audio.py --collect <path/to/FolioCollect> --fixture <dir>
                 [--outdir <dir>] [--seconds 20] [--runs 3] [--mode both]
"""

import argparse
import json
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path


def run_collector(collect, fixture, outdir, mode, seconds, seq, run_id):
    out = outdir / f"{mode}-{seq:02d}.jsonl"
    cmd = [str(collect), "--fixture", str(fixture), "--out", str(out),
           "--mode", mode, "--seconds", str(seconds), "--sequence", str(seq),
           "--run-id", run_id]
    t0 = time.monotonic()
    proc = subprocess.run(cmd, cwd=str(fixture), capture_output=True, text=True)
    wall = time.monotonic() - t0
    if proc.returncode != 0:
        return None, wall, cmd, proc.stderr
    return out, wall, cmd, ""


def read_jsonl(path):
    records = []
    for line in Path(path).read_text(errors="replace").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    return records


CLK_TCK = 100.0


def summarize(path):
    """Summarize one collector JSONL run.

    CPU percent is derived from the utime/stime tick deltas the collector
    records per sample, against the wall-clock delta between those samples.
    A percentage over 100 means more than one core was busy.
    """
    recs = read_jsonl(path)
    samples = [r for r in recs if r.get("record") == "sample"]
    runs = [r for r in recs if r.get("record") == "run_start"]
    ends = [r for r in recs if r.get("record") == "run_end"]
    summary = {"file": str(path), "records": len(recs)}

    if runs:
        r0 = runs[0]
        summary["mode"] = r0.get("mode")
        summary["official"] = r0.get("official")
    if ends:
        e = ends[-1]
        summary["valid"] = e.get("valid", True)
        summary["invalidationReason"] = e.get("invalidationReason", "")
        ws = e.get("telemetryWindowStart") or {}
        we = e.get("telemetryWindowEnd") or {}

        def d(k):
            return we.get(k, 0) - ws.get(k, 0)

        timed = d("timedCallbackCount")
        total_ns = d("totalCallbackNs")
        tel = {
            "callbackCount": timed,
            "callbackAvgMs": round(total_ns / timed / 1e6, 4) if timed else None,
            "callbackMaxMs": round(we.get("maxCallbackNs", 0) / 1e6, 4),
            "callbackBudgetMs": round(we.get("budgetNs", 0) / 1e6, 4),
            "deadlineMisses": d("deadlineMisses"),
            "xruns": d("xruns"),
            "underruns": d("underruns"),
            "blocksProcessed": d("blocksProcessed"),
            "srcActiveBlocks": d("srcActiveBlocks"),
            "rtMisuseViolations": d("rtMisuseViolations"),
            "rtAllocationViolations": d("rtAllocationViolations"),
            "rtLockViolations": d("rtLockViolations"),
            "rtLogViolations": d("rtLogViolations"),
        }
        budget = tel["callbackBudgetMs"]
        if budget and tel["callbackMaxMs"] is not None:
            tel["maxToBudgetRatio"] = round(tel["callbackMaxMs"] / budget, 3)
        if timed:
            tel["deadlineMissRate"] = round(tel["deadlineMisses"] / timed, 6)
        summary["telemetry"] = tel

    if len(samples) >= 2:
        cpu = []
        for a, b in zip(samples, samples[1:]):
            dt_ns = b.get("monotonicNs", 0) - a.get("monotonicNs", 0)
            dt_ticks = (b.get("utimeTicks", 0) - a.get("utimeTicks", 0)) + \
                       (b.get("stimeTicks", 0) - a.get("stimeTicks", 0))
            if dt_ns > 0:
                cpu.append(dt_ticks / CLK_TCK / (dt_ns / 1e9) * 100.0)
        rss = [s.get("rssKb") for s in samples if s.get("rssKb") is not None]
        peak = [s.get("peakRssKb") for s in samples if s.get("peakRssKb") is not None]
        if cpu:
            summary["cpu_percent"] = {
                "mean": round(statistics.mean(cpu), 2),
                "median": round(statistics.median(cpu), 2),
                "max": round(max(cpu), 2),
                "min": round(min(cpu), 2),
            }
        if rss:
            summary["rss_mb"] = {"mean": round(statistics.mean(rss) / 1024, 1),
                                  "max": round(max(rss) / 1024, 1)}
        if peak:
            summary["peak_rss_mb"] = round(max(peak) / 1024, 1)
        summary["sample_count"] = len(samples)
    return summary


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--collect", required=True)
    ap.add_argument("--fixture", required=True)
    ap.add_argument("--outdir", default="/tmp/opencode/audio-runs")
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--mode", default="both", choices=["offline", "playback", "both"])
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    collect = Path(args.collect).resolve()
    fixture = Path(args.fixture).resolve()
    outdir = Path(args.outdir).resolve()
    outdir.mkdir(parents=True, exist_ok=True)

    modes = ["offline", "playback"] if args.mode == "both" else [args.mode]
    results = []
    for mode in modes:
        for seq in range(1, args.runs + 1):
            run_id = f"{mode}-{seq}"
            print(f"[bench-audio] {mode} run {seq}/{args.runs} ...", flush=True)
            path, wall, cmd, err = run_collector(
                collect, fixture, outdir, mode, args.seconds, seq, run_id)
            if path is None:
                print(f"[bench-audio] {mode} run {seq} FAILED: {err.strip()[:400]}", file=sys.stderr)
                results.append({"mode": mode, "seq": seq, "ok": False, "error": err.strip()[:400]})
                continue
            s = summarize(path)
            s["mode"] = mode
            s["seq"] = seq
            s["wall_seconds"] = round(wall, 2)
            s["ok"] = True
            results.append(s)
            t = s.get("telemetry", {})
            print(f"[bench-audio] {mode} run {seq}: "
                  f"cpu={s.get('cpu_percent',{}).get('mean','?')}% "
                  f"peakRSS={s.get('peak_rss_mb','?')}MB "
                  f"cbAvg={t.get('callbackAvgMs','?')}ms "
                  f"cbMax={t.get('callbackMaxMs','?')}ms "
                  f"misses={t.get('deadlineMisses','?')} "
                  f"xruns={t.get('xruns','?')}", flush=True)

    payload = {"fixture": str(fixture), "seconds": args.seconds, "runs": args.runs, "results": results}
    print(json.dumps(payload, indent=2))
    if args.out:
        Path(args.out).write_text(json.dumps(payload, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
