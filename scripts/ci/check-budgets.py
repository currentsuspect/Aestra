#!/usr/bin/env python3
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Budget gate, tier 1 (V8-G5 · FD-39): deterministic resource budgets that block a PR.
#
# FD-06 turned "Aestra runs on a 4 GB laptop" into committed budgets. FD-39 decided
# how to enforce them without making every build an optimisation exercise:
#
#   tier 1  every PR, BLOCKING   — only metrics that come out the same every run
#   tier 2  nightly, reported    — timing (callback WCET, xruns, start-up); CI runners
#                                  are too noisy for timing to block a PR
#   tier 3  release cut, BLOCKING — everything, on the Folio-calibrated reference
#
# This script is tier 1. It does not ask anyone to optimise. It fails only when a
# build gets WORSE than the stored baseline by more than the tolerance, or crosses
# an absolute cap. A feature that does not regress passes untouched.
#
# WHAT IT MEASURES
#
#   bundle_bytes          stripped Aestra binary + the AestraAssets/ tree shipped
#                         beside it. FD-35 promises a lean core installer (20–50 MB);
#                         the cap row holds that promise.
#   rss_kb.<scenario>     peak resident memory (ru_maxrss) of AestraHeadless running
#                         a fixed offline scenario — no audio device, no UI, no
#                         timing, so the number is a property of the code, not of
#                         the runner's load.
#
# THE RULES (each one a distinct diagnostic, pinned by test-check-budgets.py)
#
#   - a metric more than TOLERANCE_PCT above its baseline   -> FAIL  "regressed"
#   - a metric above its absolute cap                       -> FAIL  "over cap"
#   - a measured metric with no baseline row                -> FAIL  "no baseline row"
#   - a baseline row nothing measured (stale)               -> FAIL  "stale row"
#   - a malformed or duplicated row                         -> FAIL
#   - a measurement that could not be taken                 -> FAIL  (never a skip)
#   - a metric more than TOLERANCE_PCT BELOW its baseline   -> WARN  "improved":
#     lower the row in the same PR so the win is kept. A warning rather than a
#     failure, because a toolchain update on the runner can shrink the binary
#     without anyone touching the code, and that must not turn every PR red.
#
# MOVING A BUDGET
#
# A change that legitimately needs more (a new bundled asset, a real feature cost)
# raises the row in the same PR and says why in the description — the same
# reviewable exception the file-size ratchet uses (Tests/Guards/file_size_ratchet.cmake).
# FD-06: budget numbers may be revised through a cited decision; the gate may not
# be removed. `--update` rewrites the baseline from the current measurement.
#
# CONTRACT LIMITS (deliberate)
#
# Baselines are measured on the CI ui-app lane (ubuntu-latest, Release). A local
# build with another compiler will not match them exactly; the tolerance absorbs
# small drift, and the authority is the lane, not a developer machine.

import argparse
import json
import os
import resource
import shutil
import subprocess
import sys
import tempfile
import time

TOLERANCE_PCT = 10

# Every child process gets a deadline: a hung measurement must fail the gate with a
# message, never hang the CI step until the job-level timeout kills it silently.
STRIP_TIMEOUT_S = 120
SCENARIO_TIMEOUT_S = 300

# name -> AestraHeadless arguments (paths relative to the repository root).
SCENARIOS = {
    "tone": ["--scenario-file", "Tests/headless_scenarios.json", "--scenario", "synthetic_tone_440"],
    "stress": ["--scenario-file", "Tests/headless_scenarios.json", "--scenario", "stress_graph_32_tracks"],
}


class GateError(Exception):
    pass


# ----------------------------------------------------------------- baseline

def parse_baseline(text):
    """Return {metric: (baseline, cap_or_None)}. Raise GateError on any malformed or duplicate row."""
    rows = {}
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 3:
            raise GateError(f"baseline line {n}: malformed row {raw!r} (expected: <metric> <baseline> <cap|->)")
        metric, base, cap = parts
        if metric in rows:
            raise GateError(f"baseline line {n}: duplicate row for {metric}")
        try:
            base_v = int(base)
            cap_v = None if cap == "-" else int(cap)
        except ValueError:
            raise GateError(f"baseline line {n}: malformed row {raw!r} (values must be integers or -)")
        if base_v <= 0 or (cap_v is not None and cap_v <= 0):
            raise GateError(f"baseline line {n}: malformed row {raw!r} (values must be positive)")
        rows[metric] = (base_v, cap_v)
    return rows


def render_baseline(measured, old_rows, header):
    lines = [header.rstrip("\n")]
    for metric in sorted(measured):
        cap = old_rows.get(metric, (None, None))[1]
        lines.append(f"{metric} {measured[metric]} {cap if cap is not None else '-'}")
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------- measurement

def tree_bytes(path):
    total = 0
    for root, _dirs, files in os.walk(path):
        for f in files:
            p = os.path.join(root, f)
            if not os.path.islink(p):
                total += os.path.getsize(p)
    return total


def measure_bundle(bin_dir):
    exe = os.path.join(bin_dir, "Aestra")
    assets = os.path.join(bin_dir, "AestraAssets")
    if not os.path.isfile(exe):
        raise GateError(f"cannot measure bundle_bytes: {exe} does not exist (build the Aestra target first)")
    if not os.path.isdir(assets):
        raise GateError(f"cannot measure bundle_bytes: {assets} does not exist (the build copies it beside the binary)")
    strip = shutil.which("strip")
    if not strip:
        raise GateError("cannot measure bundle_bytes: `strip` is not on PATH — an unstripped size would include debug info and mean nothing")
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "Aestra.stripped")
        try:
            r = subprocess.run([strip, "--strip-unneeded", "-o", out, exe], capture_output=True, text=True,
                               timeout=STRIP_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            raise GateError(f"cannot measure bundle_bytes: strip did not finish within {STRIP_TIMEOUT_S}s")
        if r.returncode != 0:
            raise GateError(f"cannot measure bundle_bytes: strip failed: {r.stderr.strip()}")
        return os.path.getsize(out) + tree_bytes(assets)


def peak_rss_kb(cmd, cwd, timeout_s=SCENARIO_TIMEOUT_S):
    """Run cmd to completion and return its own peak RSS in KiB (Linux ru_maxrss units).

    stderr goes to a temporary file, not a pipe: a child that writes more than a pipe
    holds would block on the write while we block in wait4, and the gate would hang.
    The wait polls wait4(WNOHANG) against a deadline, so the per-process rusage that
    wait4 returns is kept, and a hung child is killed, reaped and reported.
    """
    with tempfile.TemporaryFile() as err_file:
        proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.DEVNULL, stderr=err_file)
        deadline = time.monotonic() + timeout_s
        while True:
            pid, status, usage = os.wait4(proc.pid, os.WNOHANG)
            if pid != 0:
                break
            if time.monotonic() > deadline:
                proc.kill()
                os.wait4(proc.pid, 0)
                proc.returncode = -9  # reaped here; stop Popen's destructor waiting again
                raise GateError(f"cannot measure {' '.join(cmd[1:])}: did not finish within {timeout_s}s (killed)")
            time.sleep(0.05)
        proc.returncode = os.waitstatus_to_exitcode(status)
        code = proc.returncode
        if code != 0:
            err_file.seek(0)
            err = err_file.read().decode(errors="replace")
            tail = "\n".join(err.strip().splitlines()[-5:])
            raise GateError(f"cannot measure {' '.join(cmd[1:])}: exited {code}\n{tail}")
    return int(usage.ru_maxrss if sys.platform.startswith("linux") else usage.ru_maxrss // 1024)


def measure(bin_dir, headless, repo_root):
    measured = {"bundle_bytes": measure_bundle(bin_dir)}
    if not os.path.isfile(headless):
        raise GateError(f"cannot measure rss_kb.*: {headless} does not exist (build the AestraHeadless target first)")
    for name, args in SCENARIOS.items():
        measured[f"rss_kb.{name}"] = peak_rss_kb([headless, *args], repo_root)
    return measured


# --------------------------------------------------------------------- judge

def judge(measured, rows, tolerance_pct=TOLERANCE_PCT):
    """Return (failures, warnings), each a list of one-line diagnostics."""
    failures, warnings = [], []
    for metric in sorted(set(measured) | set(rows)):
        if metric not in rows:
            failures.append(f"{metric}: measured {measured[metric]} but there is no baseline row — add `{metric} {measured[metric]} -`")
            continue
        if metric not in measured:
            failures.append(f"{metric}: stale row — nothing measures this metric any more; delete the row")
            continue
        value, (base, cap) = measured[metric], rows[metric]
        delta_pct = (value - base) * 100.0 / base
        if cap is not None and value > cap:
            failures.append(f"{metric}: {value} is over cap {cap}")
        if delta_pct > tolerance_pct:
            failures.append(
                f"{metric}: regressed {delta_pct:+.1f}% ({base} -> {value}), limit +{tolerance_pct}% — "
                f"fix the regression, or raise the row in this PR and say why (FD-06/FD-39)")
        elif delta_pct < -tolerance_pct:
            warnings.append(
                f"{metric}: improved {delta_pct:+.1f}% ({base} -> {value}) — lower the row to `{metric} {value} "
                f"{cap if cap is not None else '-'}` so the win is kept")
    return failures, warnings


# ---------------------------------------------------------------------- main

def main(argv=None):
    ap = argparse.ArgumentParser(description="FD-39 tier-1 budget gate (V8-G5)")
    ap.add_argument("--bin-dir", help="build output dir holding Aestra and AestraAssets/ (e.g. build-app/bin)")
    ap.add_argument("--headless", help="path to AestraHeadless (default: <bin-dir>/AestraHeadless)")
    ap.add_argument("--baseline", default="Tests/Guards/budget_baseline.txt")
    ap.add_argument("--repo-root", default=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    ap.add_argument("--measurements", help="JSON {metric: value} to judge instead of measuring (self-tests)")
    ap.add_argument("--update", action="store_true", help="rewrite the baseline from this measurement")
    a = ap.parse_args(argv)

    baseline_path = a.baseline if os.path.isabs(a.baseline) else os.path.join(a.repo_root, a.baseline)
    try:
        with open(baseline_path, encoding="utf-8") as fh:
            text = fh.read()
        rows = parse_baseline(text)
        if a.measurements:
            with open(a.measurements, encoding="utf-8") as fh:
                measured = {k: int(v) for k, v in json.load(fh).items()}
        else:
            if not a.bin_dir:
                raise GateError("--bin-dir is required unless --measurements is given")
            headless = a.headless or os.path.join(a.bin_dir, "AestraHeadless")
            measured = measure(a.bin_dir, headless, a.repo_root)
    except GateError as e:
        print(f"::error::budget gate: {e}")
        return 2
    except OSError as e:
        print(f"::error::budget gate: {e}")
        return 2

    for metric in sorted(measured):
        base = rows.get(metric, (None, None))[0]
        print(f"  {metric:<22} {measured[metric]:>12}   baseline {base if base is not None else '—'}")

    if a.update:
        header = "".join(l for l in text.splitlines(keepends=True) if l.startswith("#"))
        with open(baseline_path, "w", encoding="utf-8") as fh:
            fh.write(render_baseline(measured, rows, header))
        print(f"baseline rewritten: {os.path.relpath(baseline_path, a.repo_root)}")
        return 0

    failures, warnings = judge(measured, rows)
    for w in warnings:
        print(f"::warning::budget gate: {w}")
    for f in failures:
        print(f"::error::budget gate: {f}")
    if failures:
        print(f"budget gate: FAILED ({len(failures)} problem(s), tolerance {TOLERANCE_PCT}%)")
        return 1
    print(f"budget gate: ok ({len(measured)} metrics within {TOLERANCE_PCT}% of baseline)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
