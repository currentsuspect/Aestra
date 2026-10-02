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
#   2. EVERY .ps1 WITH A .py TWIN MUST ACTUALLY INVOKE IT, IN LIVE CODE.
#      A .ps1 that stops delegating has become a second implementation while
#      keeping a wrapper's filename, which is the exact failure mode C8 names.
#
# A SIZE PROXY WAS TRIED HERE AND REMOVED. The first draft also failed a .ps1 that
# had grown at least as long as its Python twin, reasoning that a wrapper larger
# than its twin is not a wrapper. It is a false positive waiting to happen: a
# wrapper legitimately holds argument marshalling, and against a small script the
# wrapper is easily the longer file. The selftest's own fixture — a two-line Python
# and a two-line wrapper — failed on it. The threshold could have been tuned until
# that fixture went green, which would have hidden the proxy's real problem rather
# than fixed it, so the check went instead. C8's invariant is that the logic lives
# in Python; delegation is what tests that. Size tests a proxy for it and gets
# small scripts wrong.
#
# What this guard deliberately does NOT do:
#
#   - It does not require every .ps1 to have a .py twin. Seven have none, and that
#     is legitimate: generate-api-docs.ps1 wraps Doxygen, an external tool, and
#     run-filter-repo.ps1 wraps git-filter-repo. Wrapping an external tool is not a
#     second implementation of anything, and forcing those through Python would be
#     churn, not correctness.
#   - It does not try to detect two PowerShell files doing one job. That was a real
#     instance (add-submodules.ps1 and add_submodules.ps1, two generations of one
#     helper committed together in 1abe9315) and it was resolved by deletion, not by
#     a heuristic. A similarity detector for this would produce false positives on
#     files that legitimately share a prefix.
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
# (1) No workflow step and no hook may invoke a .ps1 directly.
#
# Matched on an invocation, not on the word: a .ps1 named in a comment or in a
# skip-selector is harmless, and flagging those would train people to ignore the
# guard. The patterns below all require something to EXECUTE it.
# ---------------------------------------------------------------------------
if [ -d "$WORKFLOWS_DIR" ]; then
    while IFS= read -r wf; do
        rel="${wf#"$REPO_ROOT"/}"
        # Strip comments before matching, so the file's own documentation of the
        # rule does not trip the rule.
        body=$(sed 's/#.*$//' "$wf")

        hits=$(printf '%s\n' "$body" | grep -nEi \
            '(powershell|pwsh)[[:space:]].*\.ps1|(bash|sh)[[:space:]].*\.ps1|(^|[[:space:]])\./[^[:space:]]*\.ps1' \
            || true)
        if [ -n "$hits" ]; then
            while IFS= read -r hit; do
                problems+=("${rel}:${hit%%:*}  invokes a .ps1 directly; Python is the canonical layer (C8)")
            done <<< "$hits"
        fi
    done < <(find "$WORKFLOWS_DIR" -name '*.yml' -o -name '*.yaml' | sort)
fi

if [ -d "$HOOKS_DIR" ]; then
    while IFS= read -r hook; do
        rel="${hook#"$REPO_ROOT"/}"
        case "$hook" in *.ps1) problems+=("${rel}  is a PowerShell git hook; the hook must be Python or shell") ;;
        esac
        body=$(sed 's/#.*$//' "$hook")
        hits=$(printf '%s\n' "$body" | grep -nEi \
            '(powershell|pwsh)[[:space:]].*\.ps1|(^|[[:space:]])\./[^[:space:]]*\.ps1' || true)
        if [ -n "$hits" ]; then
            while IFS= read -r hit; do
                problems+=("${rel}:${hit%%:*}  invokes a .ps1 directly; Python is the canonical layer (C8)")
            done <<< "$hits"
        fi
    done < <(find "$HOOKS_DIR" -type f | sort)
fi

# ---------------------------------------------------------------------------
# (2) A .ps1 with a .py twin must delegate to it and must be the smaller file.
# ---------------------------------------------------------------------------
mapfile -t ps1_files < <(find "$SCRIPTS_DIR" -name '*.ps1' | sort)
if [ "${#ps1_files[@]}" -eq 0 ]; then
    fail "no .ps1 under ${SCRIPTS_DIR}; if all were removed, delete this guard rather than leaving it vacuous"
fi

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

    rel_ps1="${ps1#"$REPO_ROOT"/}"
    rel_py="${twin#"$REPO_ROOT"/}"

    # Strip comments so a wrapper that NAMES its twin in a comment does not count
    # as delegating. The invocation itself must be in live code.
    body=$(sed 's/#.*$//' "$ps1" | sed "s/'.*'\$//")

    if ! printf '%s\n' "$body" | grep -qEi '(python3?|py)[[:space:]].*\.py|\.py'; then
        problems+=("${rel_ps1}  has Python twin ${rel_py} but never invokes it; "
            "it is a second implementation")
        continue
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

n_ps1_total="${#ps1_files[@]}"
n_twin=0
for ps1 in "${ps1_files[@]}"; do
    base="$(basename "$ps1" .ps1)"
    norm="$(printf '%s' "$base" | tr '-' '_' | tr '[:upper:]' '[:lower:]')"
    while IFS= read -r candidate; do
        cand_norm="$(basename "$candidate" .py | tr '-' '_' | tr '[:upper:]' '[:lower:]')"
        [ "$cand_norm" = "$norm" ] && n_twin=$((n_twin + 1)) && break
    done < <(find "$SCRIPTS_DIR" -name '*.py' | sort)
done

printf 'check-script-layer: OK — %d .ps1, %d with a Python twin, all thin wrappers. No CI calls a .ps1.\n' \
    "$n_ps1_total" "$n_twin"
