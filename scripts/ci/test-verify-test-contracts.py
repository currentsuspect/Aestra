#!/usr/bin/env python3
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Mutation tests for verify-test-contracts.py.
#
# Every case starts from a synthetic census that PASSES, mutates exactly one
# thing, and requires that the verifier rejects it with a specific diagnostic.
#
# WHY THE DIAGNOSTICS ARE ASSERTED, NOT JUST THE EXIT CODE
#
# A verifier that crashes also exits non-zero. Asserting only the exit code
# would let a future refactor turn a precise rejection into a stack trace and
# still look covered — the test would pass while the operator lost the one line
# telling them what to fix. Each case therefore pins a substring of the real
# message.
#
# The baseline is deliberately small and synthetic rather than a copy of the
# real census. Pinning the real 243-case tree here would make this suite fail
# every time a test is legitimately added, which is the same freezing mistake
# the verifier itself refuses to make.

import json
import os
import subprocess
import sys
import tempfile

FAILURES = []
PASSES = 0


def census_json(tests):
    """tests: {name: [labels]} -> a CTest --show-only=json-v1 shaped document."""
    return {
        "kind": "ctestInfo",
        "version": {"major": 1, "minor": 0},
        "tests": [
            {"name": name, "properties": [{"name": "LABELS", "value": labels}]}
            for name, labels in tests.items()
        ],
    }


BASE_SMALL = {
    "AlphaTest": ["unit", "contract:core"],
    "BetaTest": ["audio", "contract:audio"],
}
BASE_MAXIMAL = {
    "AlphaTest": ["unit", "contract:core"],
    "BetaTest": ["audio", "contract:audio"],
    "GammaTest": ["ui", "contract:application"],
    "BenchOne": ["benchmark", "role:benchmark"],
}
BASE_ALLOWLIST = "BenchOne\n"


VERIFIER = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "verify-test-contracts.py"
)


def run_case(name, small, maximal, allowlist_text, expect_ok, expect_substring=None,
             corrupt_census=None, allowlist_bytes=None, raw_census=None):
    """Exercise the verifier through its real CLI.

    Deliberately not through the imported functions: invoking the script proves
    argument handling, file decoding, diagnostic rendering and exit status
    together. A verifier whose internals are correct but whose CLI mis-parses
    --census would pass a function-level suite and fail in CI.
    """
    global PASSES
    with tempfile.TemporaryDirectory() as tmp:
        allow = os.path.join(tmp, "allowlist.txt")
        if allowlist_bytes is not None:
            with open(allow, "wb") as handle:
                handle.write(allowlist_bytes)
        else:
            with open(allow, "w", encoding="utf-8") as handle:
                handle.write(allowlist_text)

        specs = []
        for label, tests in (("small", small), ("maximal", maximal)):
            path = os.path.join(tmp, f"{label}.json")
            with open(path, "w", encoding="utf-8") as handle:
                if corrupt_census == label:
                    handle.write("{ this is not json")
                elif raw_census and label in raw_census:
                    json.dump(raw_census[label], handle)
                else:
                    json.dump(census_json(tests), handle)
            specs.append(f"--census={label}={path}")

        proc = subprocess.run(
            [sys.executable, VERIFIER, f"--allowlist={allow}", "--maximal=maximal"] + specs,
            capture_output=True,
            text=True,
        )
        message = proc.stdout + proc.stderr
        ok = proc.returncode == 0

    if expect_ok:
        if ok:
            PASSES += 1
            return
        FAILURES.append(f"{name}: expected PASS, got failure(s):\n    {message}")
        return

    if ok:
        FAILURES.append(f"{name}: expected FAILURE, but the census was accepted")
        return
    if expect_substring and expect_substring not in message:
        FAILURES.append(
            f"{name}: rejected, but the diagnostic did not mention "
            f"{expect_substring!r}\n    got: {message}"
        )
        return
    PASSES += 1


def mutate(base, **changes):
    out = {k: list(v) for k, v in base.items()}
    out.update({k: v for k, v in changes.items() if v is not None})
    for k, v in changes.items():
        if v is None:
            out.pop(k, None)
    return out


# --- 0. baseline -----------------------------------------------------------

run_case("baseline valid census", BASE_SMALL, BASE_MAXIMAL, BASE_ALLOWLIST, expect_ok=True)

# --- 1. normal test with zero contracts ------------------------------------

run_case(
    "1 normal case with zero contracts",
    mutate(BASE_SMALL, AlphaTest=["unit"]),
    mutate(BASE_MAXIMAL, AlphaTest=["unit"]),
    BASE_ALLOWLIST,
    expect_ok=False,
    expect_substring="AlphaTest: has no contract label",
)

# --- 2. normal test with multiple contracts --------------------------------

run_case(
    "2 normal case with two contracts",
    mutate(BASE_SMALL, AlphaTest=["contract:core", "contract:audio"]),
    mutate(BASE_MAXIMAL, AlphaTest=["contract:core", "contract:audio"]),
    BASE_ALLOWLIST,
    expect_ok=False,
    expect_substring="expected exactly one",
)

# --- 3. unknown contract ---------------------------------------------------

run_case(
    "3 unknown contract label",
    mutate(BASE_SMALL, AlphaTest=["contract:teapot"]),
    mutate(BASE_MAXIMAL, AlphaTest=["contract:teapot"]),
    BASE_ALLOWLIST,
    expect_ok=False,
    expect_substring="unknown contract label",
)

# --- 4. unknown role -------------------------------------------------------

run_case(
    "4 unknown role label",
    mutate(BASE_SMALL, AlphaTest=["contract:core", "role:probe"]),
    mutate(BASE_MAXIMAL, AlphaTest=["contract:core", "role:probe"]),
    BASE_ALLOWLIST,
    expect_ok=False,
    expect_substring="unknown role label",
)

# --- 5. role:benchmark without allowlist membership ------------------------

run_case(
    "5 role:benchmark without allowlist membership",
    BASE_SMALL,
    mutate(BASE_MAXIMAL, GammaTest=["role:benchmark"]),
    BASE_ALLOWLIST,
    expect_ok=False,
    expect_substring="not in the benchmark allowlist",
)

# --- 6. allowlist membership without role:benchmark ------------------------

run_case(
    "6 allowlisted but no role:benchmark",
    BASE_SMALL,
    mutate(BASE_MAXIMAL, BenchOne=["benchmark"]),
    BASE_ALLOWLIST,
    expect_ok=False,
    expect_substring="does not carry role:benchmark",
)

# --- 7. benchmark carrying a contract --------------------------------------

run_case(
    "7 benchmark carrying a contract",
    BASE_SMALL,
    mutate(BASE_MAXIMAL, BenchOne=["role:benchmark", "contract:realtime"]),
    BASE_ALLOWLIST,
    expect_ok=False,
    expect_substring="must carry no contract",
)

# --- 8. stale allowlist entry ----------------------------------------------

run_case(
    "8 stale allowlist entry absent from the union",
    BASE_SMALL,
    BASE_MAXIMAL,
    "BenchOne\nGhostBench\n",
    expect_ok=False,
    expect_substring="allowlist entries absent from the measured union",
)

# --- 9. allowlist format ---------------------------------------------------

run_case(
    "9a duplicate allowlist entry",
    BASE_SMALL, BASE_MAXIMAL, "BenchOne\nBenchOne\n",
    expect_ok=False, expect_substring="duplicates",
)
run_case(
    "9b blank allowlist line",
    BASE_SMALL, BASE_MAXIMAL, "BenchOne\n\n",
    expect_ok=False, expect_substring="is blank",
)
run_case(
    "9c whitespace-padded allowlist entry",
    BASE_SMALL, BASE_MAXIMAL, "  BenchOne\n",
    expect_ok=False, expect_substring="leading or trailing whitespace",
)
run_case(
    "9d carriage return in allowlist",
    BASE_SMALL, BASE_MAXIMAL, "BenchOne\r\n",
    expect_ok=False, expect_substring="carriage return",
)
run_case(
    "9e allowlist without terminal newline",
    BASE_SMALL, BASE_MAXIMAL, "BenchOne",
    expect_ok=False, expect_substring="does not end with a newline",
)
run_case(
    "9f allowlist not valid UTF-8",
    BASE_SMALL, BASE_MAXIMAL, None,
    expect_ok=False, expect_substring="not valid UTF-8",
    allowlist_bytes=b"\xff\xfe\x00Bench\n",
)

# --- 10. classification drift between configurations -----------------------

run_case(
    "10 classification differs between configurations",
    mutate(BASE_SMALL, BetaTest=["audio", "contract:realtime"]),
    BASE_MAXIMAL,
    BASE_ALLOWLIST,
    expect_ok=False,
    expect_substring="policy identity differs between configurations",
)

# --- 11. maximal is not the union ------------------------------------------

run_case(
    "11 maximal configuration is not the union",
    {**BASE_SMALL, "OrphanTest": ["contract:core"]},
    BASE_MAXIMAL,
    BASE_ALLOWLIST,
    expect_ok=False,
    expect_substring="is not the union",
)

# --- 12. malformed census json ---------------------------------------------

run_case(
    "12 malformed CTest JSON",
    BASE_SMALL, BASE_MAXIMAL, BASE_ALLOWLIST,
    expect_ok=False, expect_substring="not valid JSON",
    corrupt_census="small",
)

# --- 10b. descriptive drift is ALLOWED -------------------------------------
#
# Pins the narrowing: only contract:* and role:* form the policy identity.
# A descriptive label may legitimately differ by lane, and the guard must not
# enforce a rule nobody agreed to.

run_case(
    "10b descriptive-only drift is accepted",
    mutate(BASE_SMALL, BetaTest=["audio", "headless", "contract:audio"]),
    BASE_MAXIMAL,
    BASE_ALLOWLIST,
    expect_ok=True,
)

# --- 13. duplicate test name in one census ---------------------------------
#
# Enumeration must diagnose duplicates BEFORE the list becomes a set. Collapsing
# them would let two registrations of one name with different contracts appear
# as a single valid case, with whichever came last winning.

run_case(
    "13 duplicate test name in a census",
    BASE_SMALL, BASE_MAXIMAL, BASE_ALLOWLIST,
    expect_ok=False, expect_substring="more than once",
    raw_census={"small": {"tests": [
        {"name": "AlphaTest", "properties": [{"name": "LABELS", "value": ["contract:core"]}]},
        {"name": "AlphaTest", "properties": [{"name": "LABELS", "value": ["contract:audio"]}]},
    ]}},
)

# --- 14. terminal-LF handling is exactly one element -----------------------

run_case(
    "14a exactly one terminal LF is accepted",
    BASE_SMALL, BASE_MAXIMAL, "BenchOne\n", expect_ok=True,
)
run_case(
    "14b a second trailing LF is rejected",
    BASE_SMALL, BASE_MAXIMAL, "BenchOne\n\n",
    expect_ok=False, expect_substring="line 2 is blank",
)
run_case(
    "14c an internal blank line is rejected",
    BASE_SMALL, BASE_MAXIMAL, "BenchOne\n\nOther\n",
    expect_ok=False, expect_substring="line 2 is blank",
)

# --- report ----------------------------------------------------------------

print(f"verify-test-contracts: {PASSES} assertion(s) passed")
if FAILURES:
    for failure in FAILURES:
        print(f"::error::{failure}")
    print(f"\n{len(FAILURES)} mutation test(s) FAILED.")
    sys.exit(1)
print("All mutation tests passed.")
