#!/usr/bin/env python3
"""Exercise verify_render against the real MuseRepl, case by case.

Each case is a complete session. The point is not that the verb returns
something — it is that it returns the RIGHT verdict for the right reason, and
that a failure is reported as a failure rather than as a successful render with
unhappy numbers attached.
"""

import json
import os
import subprocess
import sys

REPL = os.path.expanduser("~/Dev/Aestra-muse/build/ci/bin/MuseRepl")
OUT = "/tmp/opencode/muse-verify"
SAMPLE = f"{OUT}/sample.wav"


def setup():
    os.makedirs(OUT, exist_ok=True)
    sys.path.insert(0, os.path.expanduser("~/Dev/Aestra-muse/scripts/muse"))
    from muse_e2e_session import write_sample_wav
    write_sample_wav(SAMPLE)


def with_clip(extra):
    return [
        {"verb": "add_track", "args": {"name": "T"}},
        {"verb": "add_lane", "args": {"name": "L"}},
        {"verb": "add_clip", "args": {"track": 0, "file": SAMPLE, "bar": 1}},
    ] + extra


CASES = {
    # A normal, healthy render of a session that definitely has audio.
    "pass_loud": (
        with_clip([{"verb": "verify_render", "args": {"file": f"{OUT}/a.wav"}}]),
        lambda r: r["status"] == "ok" and r["result"]["verdict"] == "pass"
        and r["result"]["peakDb"] > -89,
    ),
    # Soloed session: currently renders silent. The verdict must SAY so rather
    # than hand back peakDb and leave the agent to notice.
    "soloed_is_silent_and_fails": (
        with_clip([
            {"verb": "solo_track", "args": {"track": 0, "state": True}},
            {"verb": "verify_render", "args": {"file": f"{OUT}/b.wav"}},
        ]),
        lambda r: r["status"] == "ok" and r["result"]["verdict"] == "fail"
        and any(c["name"] == "not_silent" and not c["pass"] for c in r["result"]["checks"]),
    ),
    # A caller who wants a quiet render on purpose turns the check off, and the
    # silence stops being a failure.
    "silence_allowed_when_asked": (
        with_clip([
            {"verb": "solo_track", "args": {"track": 0, "state": True}},
            {"verb": "verify_render",
             "args": {"file": f"{OUT}/c.wav", "expect_not_silent": False}},
        ]),
        lambda r: r["status"] == "ok" and r["result"]["verdict"] == "pass"
        and not any(c["name"] == "not_silent" for c in r["result"]["checks"]),
    ),
    # An impossible peak ceiling must fail on that check specifically.
    "peak_ceiling_enforced": (
        with_clip([{"verb": "verify_render",
                    "args": {"file": f"{OUT}/d.wav", "expect_peak_max_db": -80}}]),
        lambda r: r["status"] == "ok" and r["result"]["verdict"] == "fail"
        and any(c["name"] == "peak_in_range" and not c["pass"] for c in r["result"]["checks"]),
    ),
    # Byte-for-byte reproducibility: same session twice must digest the same.
    "deterministic_across_renders": (
        with_clip([{"verb": "verify_render", "args": {"file": f"{OUT}/e1.wav"}}]),
        lambda r: r["status"] == "ok",
    ),
    # ...and a deliberately different ceiling must NOT change the audio, so the
    # second render still matches the first.
    "digest_matches_previous": (
        with_clip([
            {"verb": "verify_render", "args": {"file": f"{OUT}/e2.wav",
                                               "against": f"{OUT}/e1.wav"}},
        ]),
        lambda r: r["status"] == "ok"
        and any(c["name"] == "matches_previous_render" and c["pass"]
                for c in r["result"]["checks"]),
    ),
    # Comparing against a file that is not there is an error, not a pass.
    "missing_comparison_is_an_error": (
        with_clip([{"verb": "verify_render",
                    "args": {"file": f"{OUT}/f.wav", "against": f"{OUT}/nope.wav"}}]),
        lambda r: r["status"] != "ok",
    ),
    # Nothing to render is an error, not a verdict of "fail": the check never ran.
    "empty_timeline_is_an_error": (
        [{"verb": "verify_render", "args": {"file": f"{OUT}/g.wav"}}],
        lambda r: r["status"] == "execution_error",
    ),
    # Unknown args are refused rather than ignored, so a typo cannot silently
    # drop a check the caller believed they had asked for.
    "unknown_arg_refused": (
        with_clip([{"verb": "verify_render",
                    "args": {"file": f"{OUT}/h.wav", "expect_loudness": True}}]),
        lambda r: r["status"] == "validation_error",
    ),
    "missing_file_refused": (
        with_clip([{"verb": "verify_render", "args": {}}]),
        lambda r: r["status"] == "validation_error",
    ),
}


def run(reqs):
    payload = "\n".join(json.dumps(r) for r in reqs).encode()
    p = subprocess.run([REPL], input=payload, stdout=subprocess.PIPE, timeout=240)
    last = None
    for line in p.stdout.decode().splitlines():
        if not line.strip():
            continue
        d = json.loads(line)
        if d.get("verb") == "verify_render":
            last = d
    return last


def main():
    setup()
    failures = 0
    for name, (reqs, predicate) in CASES.items():
        r = run(reqs)
        if r is None:
            print(f"FAIL {name}: no verify_render response")
            failures += 1
            continue
        try:
            ok = predicate(r)
        except Exception as e:
            ok = False
            r = {"status": "predicate raised", "message": str(e)}
        verdict = r.get("result", {}).get("verdict") if r.get("status") == "ok" else r.get("status")
        print(f"{'pass' if ok else 'FAIL'} {name:<34} status={r.get('status'):<18} verdict={verdict}")
        if not ok:
            failures += 1
            print(f"     got: {json.dumps(r)[:400]}")
    print(f"\n{len(CASES) - failures}/{len(CASES)} verify_render cases behaved as specified")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
