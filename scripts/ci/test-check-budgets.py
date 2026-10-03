#!/usr/bin/env python3
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Mutation tests for check-budgets.py (V8-G5 · FD-39 tier 1).
#
# A gate that cannot fail reports clean forever. Every case below starts from a
# synthetic baseline, injects measurements through --measurements (so no build is
# needed), and asserts the exit code AND a substring of the diagnostic — a crash
# also exits non-zero, and an exit-code-only test would let a precise rejection
# decay into a stack trace and still look covered (same rule as
# test-verify-test-contracts.py).
#
# MUST PASS: the exact baseline; +10% exactly; a cap-free row; an improvement
#            (passes, but warns "improved").
# MUST FAIL: +10.1% regression; over the cap while inside tolerance; a measured
#            metric with no row; a stale row; a malformed row; a duplicate row;
#            a non-positive value.

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
GATE = os.path.join(HERE, "check-budgets.py")

BASELINE = """# synthetic baseline for the self-test
bundle_bytes 1000000 1500000
rss_kb.tone 20000 -
"""

FAILURES = []
PASSES = 0


def run(baseline_text, measured):
    with tempfile.TemporaryDirectory() as tmp:
        b = os.path.join(tmp, "baseline.txt")
        m = os.path.join(tmp, "measured.json")
        with open(b, "w", encoding="utf-8") as fh:
            fh.write(baseline_text)
        with open(m, "w", encoding="utf-8") as fh:
            json.dump(measured, fh)
        r = subprocess.run([sys.executable, GATE, "--baseline", b, "--measurements", m],
                           capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr


def expect(label, baseline_text, measured, want_code, needle=None, absent=None):
    global PASSES
    code, out = run(baseline_text, measured)
    ok = code == want_code and (needle is None or needle in out) and (absent is None or absent not in out)
    if ok:
        PASSES += 1
        print(f"  ok    {label}")
    else:
        FAILURES.append(label)
        print(f"  FAIL  {label}: exit {code} (want {want_code}), needle {needle!r}\n{out}")


base = {"bundle_bytes": 1000000, "rss_kb.tone": 20000}

print("must pass")
expect("exact baseline", BASELINE, base, 0, "budget gate: ok", absent="::warning::")
expect("+10% exactly is inside tolerance", BASELINE, {**base, "rss_kb.tone": 22000}, 0, "budget gate: ok")
expect("cap-free row may grow within tolerance", BASELINE, {**base, "rss_kb.tone": 21999}, 0, "budget gate: ok")
expect("improvement passes but asks for the row to be lowered", BASELINE,
       {**base, "bundle_bytes": 800000}, 0, "improved", absent="::error::")

print("must fail")
expect("regression past tolerance", BASELINE, {**base, "rss_kb.tone": 22021}, 1, "regressed")
expect("over cap even inside tolerance",
       "bundle_bytes 1450000 1500000\nrss_kb.tone 20000 -\n",
       {**base, "bundle_bytes": 1500001}, 1, "over cap")
expect("measured metric without a row", BASELINE, {**base, "rss_kb.stress": 30000}, 1, "no baseline row")
expect("stale row", BASELINE, {"bundle_bytes": 1000000}, 1, "stale row")
expect("malformed row (two fields)", BASELINE + "rss_kb.stress 30000\n", base, 2, "malformed row")
expect("malformed row (not an integer)", BASELINE + "rss_kb.stress lots -\n", base, 2, "malformed row")
expect("non-positive value", BASELINE + "rss_kb.stress 0 -\n", base, 2, "must be positive")
expect("duplicate row", BASELINE + "rss_kb.tone 20000 -\n", base, 2, "duplicate row")

print(f"\n{PASSES} passed, {len(FAILURES)} failed")
sys.exit(1 if FAILURES else 0)
