#!/usr/bin/env python3
"""Drive the real MuseRepl binary over the session corpus and judge the answers.

This is the receipt. It runs the shipped executable — not a unit test, not a
reimplementation of the dispatch — so "end to end confirmation" means a
transcript you can read rather than an assertion somebody wrote next to the code.

Two jobs:

  * judge a run against the corpus's expectations (``--expect``), and
  * prove two builds behave the same (``--baseline``), which is how the
    MuseService family split was shown to change nothing.

Normalised before anything is compared, because both genuinely vary between runs
of the SAME binary and neither says anything about the code:

  * executionMs — wall clock
  * the render directory, which the reply echoes back verbatim
  * 32-hex object ids — clip and lane ids are minted per run, verified by
    running one binary twice
"""

import argparse
import json
import os
import re
import subprocess
import sys

VOLATILE = {"executionMs"}
UUID_RE = re.compile(r"\b[0-9a-f]{32}\b")


def scrub(value):
    if isinstance(value, str):
        return UUID_RE.sub("<uuid>", value)
    return value


def normalise(obj):
    if isinstance(obj, dict):
        return {k: normalise(v) for k, v in obj.items() if k not in VOLATILE}
    if isinstance(obj, list):
        return [normalise(v) for v in obj]
    if isinstance(obj, str) and ("/" in obj or "\\" in obj):
        return scrub(os.path.basename(obj))
    return scrub(obj)


def run_session(repl, session_path, timeout=300):
    payload = open(session_path, "rb").read()
    proc = subprocess.run([repl], input=payload, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=timeout)
    lines = [l for l in proc.stdout.decode().splitlines() if l.strip()]
    return lines, proc.stderr.decode(), proc.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repl", required=True, help="path to the MuseRepl binary")
    ap.add_argument("--session", required=True)
    ap.add_argument("--expect", default=None, help="corpus expectation table")
    ap.add_argument("--out", default=None, help="write normalised responses here")
    ap.add_argument("--baseline", default=None,
                    help="normalised responses from another build to diff against")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    lines, stderr, rc = run_session(args.repl, args.session)
    if not args.quiet:
        print(f"MuseRepl exit={rc}  responses={len(lines)}")
        if stderr.strip():
            print(f"stderr: {stderr.strip()[:400]}")

    responses = []
    for i, line in enumerate(lines):
        try:
            responses.append(normalise(json.loads(line)))
        except json.JSONDecodeError as e:
            print(f"FAIL line {i + 1} is not JSON: {e}\n  {line[:200]}")
            return 1

    # Every request must get exactly one response. A dropped, doubled or
    # misattributed reply is the failure mode a status-code check alone misses.
    # Order is not required to match — the corpus reorders blocks on purpose —
    # but the multiset of ids must.
    sent = [json.loads(l)["id"]
            for l in open(args.session, encoding="utf-8") if l.strip()]
    got = [r.get("id") for r in responses]
    if sorted(sent) != sorted(got):
        print("FAIL response coverage:")
        missing = sorted(set(sent) - set(got))
        extra = sorted(set(got) - set(sent))
        dupes = sorted({i for i in got if got.count(i) > 1})
        if missing:
            print(f"  no response for ids {missing}")
        if extra:
            print(f"  unexpected ids {extra}")
        if dupes:
            print(f"  ids answered more than once {dupes}")
        return 1

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            for r in responses:
                f.write(json.dumps(r, sort_keys=True) + "\n")

    failures = []
    by_id = {r.get("id"): r for r in responses}
    if args.expect:
        for row in open(args.expect, encoding="utf-8"):
            rid, exp, verb = row.rstrip("\n").split("\t")
            resp = by_id.get(int(rid))
            if resp is None:
                failures.append((rid, verb, exp, "no response"))
                continue
            status = resp.get("status")
            if exp == "ok" and status != "ok":
                failures.append((rid, verb, exp,
                                 f"status={status} {resp.get('message', '')[:90]}"))
            elif exp == "err" and status == "ok":
                failures.append((rid, verb, exp, "unexpectedly succeeded"))

    ok = sum(1 for r in responses if r.get("status") == "ok")
    print(f"status: ok={ok} not-ok={len(responses) - ok}")

    if failures:
        print(f"\n{len(failures)} expectation mismatch(es):")
        for rid, verb, exp, why in failures:
            print(f"  id={rid:<4} {verb:<24} want={exp:<3} {why}")

    # A baseline diff is the stronger statement, so it outranks expectations.
    if args.baseline:
        base = [json.loads(l) for l in open(args.baseline, encoding="utf-8") if l.strip()]
        if len(base) != len(responses):
            print(f"\nBASELINE DIFF: {len(base)} responses vs {len(responses)} now")
            return 1
        diffs = [(b, n) for b, n in zip(base, responses) if b != n]
        if diffs:
            print(f"\nBASELINE DIFF: {len(diffs)} response(s) differ")
            for b, n in diffs[:10]:
                print(f"  id {b.get('id')} {b.get('verb')}")
                print(f"    baseline: {json.dumps(b)[:220]}")
                print(f"    now     : {json.dumps(n)[:220]}")
            return 1
        print(f"baseline: IDENTICAL across {len(responses)} responses")

    if failures:
        return 1
    if args.expect:
        print("all expectations met")
    return 0


if __name__ == "__main__":
    sys.exit(main())
