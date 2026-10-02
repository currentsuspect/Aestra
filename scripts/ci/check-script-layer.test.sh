#!/usr/bin/env bash
# © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Self-test for check-script-layer.sh — proves the guard rejects each way the C8
# invariant can break, and accepts each legitimate shape.
#
# A guard that has only ever passed is indistinguishable from no guard. Every
# fixture below starts from a baseline that MUST pass and applies exactly ONE
# mutation, so each result isolates a single behaviour.
#
# Usage: check-script-layer.test.sh [guard-script]

set -uo pipefail

GUARD="${1:-$(dirname "$0")/check-script-layer.sh}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

failures=0

# ---------------------------------------------------------------------------
# write_tree <workflow-body> [ps1-body]
#
# The baseline is deliberately the shape C8 blesses: a .ps1 with a Python twin
# that delegates to it, a .ps1 with no twin (an external-tool wrapper), and a
# workflow that touches neither.
# ---------------------------------------------------------------------------
write_tree() {
    local wf_body="${1:-}"
    local ps1_body="${2:-}"
    rm -rf "$WORK/tree"
    mkdir -p "$WORK/tree/scripts/ci" "$WORK/tree/.github/workflows" "$WORK/tree/.githooks"
    cp "$GUARD" "$WORK/tree/scripts/ci/"

    printf 'def main():\n    return 0\n' > "$WORK/tree/scripts/thing.py"

    if [ -z "$ps1_body" ]; then
        # the passing default: a delegating wrapper, much smaller than its twin
        cat > "$WORK/tree/scripts/thing.ps1" <<'EOF'
# Thin wrapper — canonical implementation is scripts/thing.py
& python3 (Join-Path $PSScriptRoot "thing.py") @args
EOF
    else
        printf '%s\n' "$ps1_body" > "$WORK/tree/scripts/thing.ps1"
    fi

    # a .ps1 with NO Python twin: wraps an external tool, which is legitimate
    printf 'doxygen Doxyfile\n' > "$WORK/tree/scripts/external.ps1"

    printf '%s\n' "$wf_body" > "$WORK/tree/.github/workflows/ci.yml"
    printf '#!/bin/sh\nexit 0\n' > "$WORK/tree/.githooks/pre-push"
}

run_fixture() {
    local name="$1" expect="$2"
    bash "$GUARD" "$WORK/tree" >/dev/null 2>&1
    local rc=$?
    if [ "$expect" = "FAIL" ] && [ "$rc" -eq 0 ]; then
        printf 'fixture %-58s guard PASSED but must FAIL\n' "'$name'"
        failures=$((failures + 1))
    elif [ "$expect" = "PASS" ] && [ "$rc" -ne 0 ]; then
        printf 'fixture %-58s guard FAILED but must PASS\n' "'$name'"
        failures=$((failures + 1))
    else
        printf 'fixture %-58s ok (%s)\n' "'$name'" "$expect"
    fi
}

# --- the baseline must pass, or every FAIL below proves nothing -------------
write_tree
run_fixture "baseline: delegating wrapper, no CI .ps1 call" PASS

# --- invariant (1): a workflow that invokes PowerShell --------------------
write_tree 'jobs:
  build:
    steps:
      - run: powershell -File scripts/thing.ps1'
run_fixture "workflow runs powershell -File on a .ps1" FAIL

write_tree 'jobs:
  build:
    steps:
      - run: pwsh ./scripts/thing.ps1'
run_fixture "workflow runs pwsh on a .ps1" FAIL

write_tree 'jobs:
  build:
    steps:
      - run: ./scripts/thing.ps1'
run_fixture "workflow runs a .ps1 directly" FAIL

# A .ps1 named in a COMMENT or a skip-selector is not an invocation. Flagging
# those would train people to ignore the guard.
write_tree '# NOTE: powershell -File scripts/thing.ps1 is the old way
jobs:
  build:
    steps:
      - run: python3 scripts/thing.py'
run_fixture "a .ps1 named only in a comment is not an invocation" PASS

# --- invariant (2): a .ps1 that stopped delegating ------------------------
write_tree '' 'Write-Host "the same check, implemented again"'
run_fixture "twin exists but the .ps1 never invokes it" FAIL

write_tree '' '# see scripts/thing.py for the real logic
Write-Host "reimplemented"'
run_fixture "a .ps1 naming its twin only in a comment is not delegating" FAIL

# --- legitimate shapes must NOT be flagged --------------------------------
# No Python twin at all: wrapping an external tool is not duplication. The twin
# must actually be REMOVED to express this — the first version of this fixture
# left thing.py in place, so thing.ps1 did have a twin and correctly failed. The
# guard was right and the fixture was wrong, which is the useful order to discover
# that in.
rm -f "$WORK/tree/scripts/thing.py"
printf 'doxygen Doxyfile\n' > "$WORK/tree/scripts/external-tool.ps1"
run_fixture "a .ps1 with no Python twin (external-tool wrapper)" PASS

# And the baseline must still pass afterwards, so the previous fixture is proven
# not to have mutated shared state.
write_tree
run_fixture "baseline still passes after the no-twin fixture" PASS

# --- the git-hook branch, which the workflow fixtures do not reach ---------
# The hook loop shares its pattern with the workflow loop but is separate code,
# and a branch no fixture reaches is a branch no fixture proves.
rm -f "$WORK/tree/.githooks/pre-push"
printf '#!/bin/sh\npowershell -File scripts/thing.ps1\n' > "$WORK/tree/.githooks/pre-push"
run_fixture "a git hook invokes a .ps1" FAIL

printf '#!/bin/sh\npython3 scripts/thing.py\n' > "$WORK/tree/.githooks/pre-push"
run_fixture "a git hook that calls Python is fine" PASS

rm -f "$WORK/tree/.githooks/pre-push"
printf 'x\n' > "$WORK/tree/.githooks/pre-push.ps1"
run_fixture "a PowerShell git hook" FAIL
rm -f "$WORK/tree/.githooks/pre-push.ps1"

# --- a guard that cannot see its input must fail, not pass ----------------
rm -rf "$WORK/tree"
mkdir -p "$WORK/tree"
run_fixture "no scripts/ directory at all" FAIL

rm -rf "$WORK/tree"
mkdir -p "$WORK/tree/scripts"
run_fixture "scripts/ exists but .github/workflows does not" FAIL

rm -rf "$WORK/tree"
mkdir -p "$WORK/tree/scripts" "$WORK/tree/.github/workflows"
run_fixture "no .ps1 under scripts/ (guard would be vacuous)" FAIL

if [ "$failures" -ne 0 ]; then
    printf '\ncheck-script-layer self-test FAILED (%d fixture(s))\n' "$failures" >&2
    exit 1
fi
printf '\ncheck-script-layer self-test passed\n'