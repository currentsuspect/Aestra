#!/usr/bin/env bash
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Tests for check-decision-citation.sh — the FD-12 citation parser.
#
# The direction that matters here is the FALSE PASS. A wrong rejection is loud: the
# author sees a red check and a message, and fixes the block. A wrong acceptance is
# silent, and a governance mechanism that silently accepts anything binds nothing
# while reporting that it does. So the accept cases below are deliberately few and
# the reject cases deliberately hostile.
#
# Everything here is offline. The cross-repo half (does the SHA exist in
# Aestra-Internals, does the entry exist at that SHA) lives in the workflow and is
# not exercised by these tests.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECK="${HERE}/check-decision-citation.sh"

failures=0
checks=0

# expect <pass|fail> <label> <<< body
expect() {
    local expected="$1" label="$2"
    local body tmp actual status
    body="$(cat)"
    tmp="$(mktemp)"
    printf '%s' "${body}" > "${tmp}"
    checks=$((checks + 1))

    # GITHUB_OUTPUT is deliberately unset: the script must not require it.
    actual="$(env -u GITHUB_OUTPUT bash "${CHECK}" "${tmp}" 2>&1)"
    status=$?
    rm -f "${tmp}"

    local got="pass"
    [ "${status}" -ne 0 ] && got="fail"

    if [ "${got}" = "${expected}" ]; then
        printf 'PASS  %-52s -> %s\n' "${label}" "${got}"
    else
        printf 'FAIL  %-52s -> %s (expected %s)\n' "${label}" "${got}" "${expected}"
        printf '%s\n' "${actual}" | sed 's/^/        | /'
        failures=$((failures + 1))
    fi
}

# ---------------------------------------------------------------------------
# Accepted
# ---------------------------------------------------------------------------

expect pass "canonical planned block" <<'EOF'
## Summary
- Adds the loop scenario.

Objective:  A1 — example objective
Decision:   FD-04 @ a30696a
Kind:       planned
Reversal:   —
EOF

expect pass "corrective work needs no objective" <<'EOF'
Objective:  —
Decision:   FD-06 @ a30696a858b2f723030c3732e7a9deb80bb103c5
Kind:       corrective
Reversal:   —
EOF

expect pass "unparked with an authorising entry" <<'EOF'
Objective:  A2 — example objective
Decision:   FD-04 @ a30696a
Kind:       unparked
Reversal:   FD-13 — authorised by decision entry
EOF

expect pass "lowercase keys and kind" <<'EOF'
objective:  a1
decision:   fd-04 @ A30696A
kind:       PLANNED
EOF

expect pass "markdown list markers and ragged spacing" <<'EOF'
- Objective:A1
* Decision:  FD-04@a30696a
- Kind:planned
EOF

expect pass "full 40-character sha" <<'EOF'
Objective:  A3 — example objective
Decision:   FD-11 @ a30696a858b2f723030c3732e7a9deb80bb103c5
Kind:       planned
EOF

# ---------------------------------------------------------------------------
# Rejected — absent or inert
# ---------------------------------------------------------------------------

expect fail "no block at all" <<'EOF'
## Summary
- Fixed a typo.
EOF

expect fail "empty body" <<'EOF'
EOF

# The trap this check exists to survive. The pull request template documents each
# field inside an HTML comment, worked examples included. Parse before stripping
# comments and the template answers the check on the author's behalf, forever.
expect fail "template example inside an HTML comment does not count" <<'EOF'
## Summary
- Something.

<!--
Objective:  A1 — example objective
Decision:   FD-04 @ a30696a
Kind:       planned
-->
EOF

expect fail "multi-line comment spanning the whole block" <<'EOF'
<!-- fill this in:
Objective: x
Decision: FD-01 @ a30696a
Kind: planned
--> and nothing else
EOF

expect fail "untouched template placeholders" <<'EOF'
Objective:  <what this advances>
Decision:   <FD-NN @ sha>
Kind:       planned | corrective | unparked
EOF

expect fail "em-dash placeholders throughout" <<'EOF'
Objective:  —
Decision:   —
Kind:       —
EOF

# ---------------------------------------------------------------------------
# Rejected — the SHA requirement, which is the entire mechanism
# ---------------------------------------------------------------------------

expect fail "entry id with no sha" <<'EOF'
Objective:  A1
Decision:   FD-04
Kind:       planned
EOF

expect fail "sha too short to be unambiguous" <<'EOF'
Objective:  A1
Decision:   FD-04 @ a306
Kind:       planned
EOF

expect fail "sha is not hex" <<'EOF'
Objective:  A1
Decision:   FD-04 @ zzzzzzz
Kind:       planned
EOF

expect fail "prose instead of a citation" <<'EOF'
Objective:  A1
Decision:   see the decision record
Kind:       planned
EOF

expect fail "entry id malformed" <<'EOF'
Objective:  A1
Decision:   FD4 @ a30696a
Kind:       planned
EOF

# ---------------------------------------------------------------------------
# Rejected — Kind
# ---------------------------------------------------------------------------

expect fail "kind missing" <<'EOF'
Objective:  A1
Decision:   FD-04 @ a30696a
EOF

expect fail "kind invented" <<'EOF'
Objective:  A1
Decision:   FD-04 @ a30696a
Kind:       urgent
EOF

expect fail "kind left as the template's alternatives" <<'EOF'
Objective:  A1
Decision:   FD-04 @ a30696a
Kind:       planned | corrective | unparked
EOF

# ---------------------------------------------------------------------------
# Rejected — unparking without authority
#
# Work that a decision parks, started anyway. Self-authorisation must not pass.
# ---------------------------------------------------------------------------

expect fail "unparked with no reversal" <<'EOF'
Objective:  A4 — example objective
Decision:   FD-03 @ a30696a
Kind:       unparked
EOF

expect fail "unparked with an empty reversal" <<'EOF'
Objective:  A4 — example objective
Decision:   FD-03 @ a30696a
Kind:       unparked
Reversal:   —
EOF

expect fail "unparked authorised by prose rather than an entry" <<'EOF'
Objective:  A4 — example objective
Decision:   FD-03 @ a30696a
Kind:       unparked
Reversal:   approved verbally
EOF

# ---------------------------------------------------------------------------
# Rejected — objective
# ---------------------------------------------------------------------------

expect fail "planned work with no objective" <<'EOF'
Objective:
Decision:   FD-04 @ a30696a
Kind:       planned
EOF

expect fail "objective is a bare dash" <<'EOF'
Objective:  -
Decision:   FD-04 @ a30696a
Kind:       planned
EOF

expect fail "objective is TBD" <<'EOF'
Objective:  TBD
Decision:   FD-04 @ a30696a
Kind:       planned
EOF

# ---------------------------------------------------------------------------
# Robustness
# ---------------------------------------------------------------------------

# GitHub returns bodies CRLF-terminated. Left unhandled, the trailing \r rides
# along inside the captured SHA, fails the hex test, and prints an error message
# in which the SHA looks perfectly correct.
printf 'Objective:  A1\r\nDecision:   FD-04 @ a30696a\r\nKind:       planned\r\n' \
    | expect pass "CRLF line endings"

expect pass "block below a long description" <<'EOF'
## Summary

Long prose. Several paragraphs. Mentions FD-99 @ deadbeef in passing, which must
not be mistaken for the citation because it is not on a keyed line.

## Authority / invariant

- Example invariant note.

Objective:  A3 — example objective
Decision:   FD-11 @ a30696a
Kind:       planned
EOF

# First keyed occurrence wins, so a later stray mention cannot override a real
# block by appearing further down the body.
expect fail "later stray key cannot rescue an earlier bad citation" <<'EOF'
Objective:  A1
Decision:   nonsense
Kind:       planned

Decision:   FD-04 @ a30696a
EOF

# ---------------------------------------------------------------------------

printf '\n%d checks, %d failures\n' "${checks}" "${failures}"
[ "${failures}" -eq 0 ]
