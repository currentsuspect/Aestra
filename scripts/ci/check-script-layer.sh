#!/usr/bin/env bash
# © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# check-script-layer.sh — V8-C8: Python is the canonical script layer.
#
# Usage:  check-script-layer.sh <repo-root>
# Exit:   0 = the script layer obeys the rule. 1 = it does not.
#
# THE RULE, and why most of it is not checkable.
#
# C8's acceptance criterion reads: "One implementation per semantic check.
# PowerShell survives only as a thin wrapper that shells out to the Python, never
# as a second implementation."
#
# Half of that sentence is a judgement no script can make. Whether two files
# implement "the same semantic check" is a reading task. So this guard checks the
# half that is mechanical, and that half is the one with teeth:
#
#   1. NO CI STEP AND NO GIT HOOK MAY INVOKE A .ps1 DIRECTLY.
#      This is what makes Python canonical in practice rather than in the docs.
#      docs/CONTRIBUTING_CORE.md already says Python is canonical — and a workflow
#      calling powershell -File silently overrides that sentence forever. Measured
#      at 11887c8a: no workflow invokes one, so the rule holds today.
#
#   2. EVERY .ps1 WITH A .py TWIN MUST INVOKE THAT TWIN, IN LIVE CODE.
#      A .ps1 that stops delegating has become a second implementation while
#      keeping a wrapper's filename, which is the exact failure mode C8 names.
#
# WHAT THIS GUARD DELIBERATELY DOES NOT DO:
#
#   - It does not require every .ps1 to have a .py twin. Seven have none, and that
#     is legitimate: generate-api-docs.ps1 wraps Doxygen, an external tool, and
#     run-filter-repo.ps1 wraps git-filter-repo. Wrapping an external tool is not a
#     second implementation of anything, and forcing those through Python would be
#     churn, not correctness.
#   - It does not try to detect two PowerShell files doing one job. That was a real
#     instance (add-submodules.ps1 and add_submodules.ps1, two generations of one
#     helper committed together in 1abe9315) and it was resolved by hand, not by a
#     heuristic. A similarity detector for this would false-positive on files that
#     legitimately share a prefix.
#
# Every path that cannot establish the invariant exits 1. A guard that reports
# agreement it did not verify is worse than no guard, because it looks like coverage.

set -uo pipefail

REPO_ROOT="${1:-.}"
SCRIPTS_DIR="${REPO_ROOT}/scripts"
WORKFLOWS_DIR="${REPO_ROOT}/.github/workflows"
HOOKS_DIR="${REPO_ROOT}/.githooks"

fail() {
    printf 'check-script-layer: %s\n' "$1" >&2
    exit 1
}

[ -d "$SCRIPTS_DIR" ] || fail "no scripts/ directory at ${SCRIPTS_DIR}"
[ -d "$WORKFLOWS_DIR" ] || fail "no .github/workflows directory at ${WORKFLOWS_DIR}"

problems=()

# ---------------------------------------------------------------------------
# ONE scanner, used by both the workflow and the hook passes.
#
# The first draft wrote this pattern out twice, and the two copies had already
# drifted by the time CodeRabbit read them: the workflow scan knew about `bash`
# and `sh` invoking a .ps1, and the hook scan did not. Two copies of a rule is
# the same duplication C8 exists to remove, one layer down. So there is one now.
#
# Two things are normalised before matching, and both were evasion routes in the
# first version:
#
#   - Line continuations are joined. `run: bash -c \` followed by a line reading
#     `powershell -File x.ps1` is the same invocation spread over two lines, and a
#     per-line grep never sees it.
#
# ORDER MATTERS HERE, and getting it wrong is silent. Comments are stripped BEFORE
# continuations are joined, never after. Joining first collapses the file to a
# single line, so the first '#' deletes everything after it — which made all three
# real wrappers look like second implementations until it was caught. Both normalisers
# are correct alone; only this order is.
#   - Comments are stripped, so a .ps1 named in documentation or in a skip-selector
#     is not an invocation. Flagging prose would train people to ignore the guard.
# ---------------------------------------------------------------------------
scan_for_ps1_invocation() {
    # $1 = file. Prints "lineno<TAB>text" per hit, or nothing.
    sed 's/#.*$//' "$1" \
        | sed ':a;N;$!ba;s/\n/ /g' \
        | grep -nEi \
            '(powershell|pwsh)[[:space:]].*\.ps1|(bash|sh)[[:space:]].*\.ps1|(^|[[:space:]])\./[^[:space:]]*\.ps1' \
        || true
}

if [ -d "$WORKFLOWS_DIR" ]; then
    while IFS= read -r wf; do
        rel="${wf#"$REPO_ROOT"/}"
        while IFS= read -r hit; do
            problems+=("${rel}:${hit%%:*}  invokes a .ps1 directly; Python is canonical (C8)")
        done < <(scan_for_ps1_invocation "$wf")
    done < <(find "$WORKFLOWS_DIR" \( -name '*.yml' -o -name '*.yaml' \) | sort)
fi

if [ -d "$HOOKS_DIR" ]; then
    while IFS= read -r hook; do
        rel="${hook#"$REPO_ROOT"/}"
        case "$hook" in
            *.ps1) problems+=("${rel}  is a PowerShell git hook; the hook must be Python or shell") ;;
        esac
        while IFS= read -r hit; do
            problems+=("${rel}:${hit%%:*}  invokes a .ps1 directly; Python is canonical (C8)")
        done < <(scan_for_ps1_invocation "$hook")
    done < <(find "$HOOKS_DIR" -type f | sort)
fi

# ---------------------------------------------------------------------------
# (2) A .ps1 with a .py twin must delegate to THAT twin, in live code.
#
# The first draft stripped every single-quoted line before looking for the
# invocation, on the theory that the delegate line was a quoted path. That also
# erased any wrapper that reached Python through a single-quoted argument, so a
# legitimate wrapper could be reported as a second implementation. Instead the
# twin's own filename is required to appear — which is stricter AND does not need
# the destructive strip.
# ---------------------------------------------------------------------------
mapfile -t ps1_files < <(find "$SCRIPTS_DIR" -name '*.ps1' | sort)
if [ "${#ps1_files[@]}" -eq 0 ]; then
    fail "no .ps1 under ${SCRIPTS_DIR}; if all were removed, delete this guard rather than leaving it vacuous"
fi

n_twin=0
for ps1 in "${ps1_files[@]}"; do
    base="$(basename "$ps1" .ps1)"
    # Normalise the separator, so add-submodules and add_submodules are recognised
    # as the same logical script rather than two unrelated ones.
    norm="$(printf '%s' "$base" | tr '-' '_' | tr '[:upper:]' '[:lower:]')"

    twin=""
    while IFS= read -r candidate; do
        cand_norm="$(basename "$candidate" .py | tr '-' '_' | tr '[:upper:]' '[:lower:]')"
        if [ "$cand_norm" = "$norm" ]; then twin="$candidate"; break; fi
    done < <(find "$SCRIPTS_DIR" -name '*.py' | sort)

    [ -n "$twin" ] || continue   # no twin: wrapping an external tool is legitimate
    n_twin=$((n_twin + 1))

    rel_ps1="${ps1#"$REPO_ROOT"/}"
    rel_py="${twin#"$REPO_ROOT"/}"
    twin_name="$(basename "$twin")"

    # Comments stripped, continuations joined — same normalisation as the scanner,
    # so a delegate reached through a line continuation still counts.
    body=$(sed 's/#.*$//' "$ps1" | sed ':a;N;$!ba;s/\n/ /g')

    if ! printf '%s\n' "$body" | grep -qF "$twin_name"; then
        problems+=("${rel_ps1}  has Python twin ${rel_py} but never names it; it is a second implementation")
    fi
done

if [ "${#problems[@]}" -gt 0 ]; then
    printf 'The script layer violates C8 (Python is canonical):\n' >&2
    for p in "${problems[@]}"; do printf '  - %s\n' "$p" >&2; done
    printf '\n' >&2
    printf 'Fix by making the Python script canonical and the .ps1 delegate to it.\n' >&2
    printf 'A .ps1 with no .py twin is fine — those wrap external tools.\n' >&2
    exit 1
fi

printf 'check-script-layer: OK — %d .ps1, %d with a Python twin, all thin wrappers. No CI calls a .ps1.\n' \
    "${#ps1_files[@]}" "$n_twin"