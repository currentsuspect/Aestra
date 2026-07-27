#!/usr/bin/env bash
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Tests for lane-runs.sh — the verdict → lane policy table.
#
# As with the classifier, the negative direction is the one that matters: this table
# decides whether REQUIRED status checks do real work, so a wrong "false" removes a
# gate. Unknown input must always come back "true".

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LANE_RUNS="${HERE}/lane-runs.sh"

failures=0
checks=0

# expect <expected> <verdict> <lane>
expect() {
    local expected="$1" verdict="$2" lane="$3"
    checks=$((checks + 1))
    local actual
    actual="$(bash "${LANE_RUNS}" "${verdict}" "${lane}")"
    if [[ "${actual}" == "${expected}" ]]; then
        printf 'PASS  %-22s %-14s -> %s\n' "${verdict}" "${lane}" "${actual}"
    else
        printf 'FAIL  %-22s %-14s -> %s (expected %s)\n' \
            "${verdict}" "${lane}" "${actual}" "${expected}"
        failures=$((failures + 1))
    fi
}

ALL_LANES=(linux-gcc windows-msvc macos-clang ui-app asan tsan lsan tidy)

# --- broad runs everything --------------------------------------------------
for lane in "${ALL_LANES[@]}"; do
    expect true broad "${lane}"
done

# --- skip-cxx runs no C++ lane, the UI/App lane included --------------------
for lane in "${ALL_LANES[@]}"; do
    expect false skip-cxx "${lane}"
done

# --- ui-app-only: only the lane that compiles the UI ------------------------
expect true  ui-app-only ui-app
expect false ui-app-only linux-gcc
expect false ui-app-only windows-msvc
expect false ui-app-only macos-clang
expect false ui-app-only asan
expect false ui-app-only tsan
expect false ui-app-only lsan
expect false ui-app-only tidy

# --- anything unrecognised must run -----------------------------------------
# A typo in a verdict or a lane id is allowed to cost compute. It is never allowed to
# remove a check, so every unknown combination answers true.
expect true "" ""
expect true "" linux-gcc
expect true broad ""
expect true skip-cxx ""
expect true ui-app-only ""
expect true "typo-verdict" linux-gcc
expect true "typo-verdict" ui-app
expect true skip-cxx "lane-that-does-not-exist"
expect true ui-app-only "lane-that-does-not-exist"
expect true "UI-APP-ONLY" linux-gcc  # case matters; a near-miss must not skip

# --- liveness ----------------------------------------------------------------
# Every assertion above except these would still pass if the script had degraded into
# printing "true" unconditionally, because "true" is the safe answer. These are the
# ones that fail when the feature stops working.
checks=$((checks + 1))
if [[ "$(bash "${LANE_RUNS}" ui-app-only windows-msvc)" == "false" &&
      "$(bash "${LANE_RUNS}" skip-cxx linux-gcc)" == "false" ]]; then
    printf 'PASS  %-37s -> false\n' "liveness: skips are reachable"
else
    printf 'FAIL  %-37s\n' "liveness: skips are reachable"
    failures=$((failures + 1))
fi

echo
if [[ "${failures}" -eq 0 ]]; then
    echo "ALL PASSED (${checks} checks)"
    exit 0
fi
echo "FAILURES: ${failures} of ${checks}"
exit 1
