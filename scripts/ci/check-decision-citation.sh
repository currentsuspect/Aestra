#!/usr/bin/env bash
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Validate a pull request's decision citation block. Implements FD-12.
#
# Usage:  check-decision-citation.sh <pr-body-file>
# Exit:   0 = the block is well formed. 1 = it is not.
#
# Diagnostics go to stdout. When GITHUB_OUTPUT is set, the parsed fields are
# appended to it as decision-id / decision-sha / kind, so the workflow can do the
# cross-repo half (does that SHA exist, does that entry exist in it) afterwards.
#
# THE CONTRACT — this script inverts lane-runs.sh's default deliberately.
#
# lane-runs.sh answers "true" for anything it does not recognise, because a typo in
# a lane id must cost compute and never coverage. Here the opposite is true: an
# unparsable citation must FAIL. A citation check that passes when it cannot read
# the citation is not a check, it is a green tick over a gate that is not running —
# which is worse than having no gate, because it reports compliance.
#
# So: every path out of this script that is not a positively verified citation
# exits 1.
#
# WHAT FD-12 REQUIRES, AND WHAT THAT MEANS HERE
#
#   Objective    the active objective the work advances
#   Decision     the governing decision, cited as FD-NN @ <sha>
#   Kind         planned | corrective | unparked
#   Reversal     the decision entry authorising a reversal — unparked only
#
# The SHA is not decoration. Citing "FD-04" alone says nothing about WHICH FD-04;
# the strategic record is a living file. FD-04 @ <sha> is what makes staleness
# visible during review, which is the requirement FD-12 actually states.
#
# STRUCTURAL ONLY
#
# This half is offline and therefore unit-testable. It does not know whether the
# SHA exists, whether the entry exists, or whether either is current — the workflow
# does that against Aestra-Internals, because it needs a token and a network.
# Keeping the split here is what lets the parser have tests at all.

set -uo pipefail

BODY_FILE="${1:-}"

# Fields the block may carry. Order here is the order errors are reported in.
KEY_OBJECTIVE="Objective"
KEY_DECISION="Decision"
KEY_KIND="Kind"
KEY_REVERSAL="Reversal"

errors=0

fail() {
    printf '::error::%s\n' "$1"
    errors=$((errors + 1))
}

note() {
    printf '  %s\n' "$1"
}

# --- reading the body -------------------------------------------------------
#
# Two transformations, both load-bearing:
#
#   CRLF     GitHub returns pull request bodies with \r\n. Left in place, a value
#            captured to end-of-line carries a trailing \r, so "FD-04 @ a30696a"
#            arrives as "a30696a\r", fails the hex test, and reports a malformed
#            SHA that looks perfectly correct in the error message. Hours of
#            confusion for one invisible byte.
#
#   comments The pull request template explains each field inside <!-- --> blocks,
#            and those blocks contain worked examples. Parsing before stripping
#            them means the template's own example satisfies the check and every
#            PR passes without an author typing anything. The comment stripper
#            must therefore run first, and must handle multi-line comments.
read_body() {
    if [ -z "${BODY_FILE}" ]; then
        fail "no pull request body file given (usage: check-decision-citation.sh <file>)"
        return 1
    fi
    if [ ! -f "${BODY_FILE}" ]; then
        fail "pull request body file not found: ${BODY_FILE}"
        return 1
    fi
    # sed strips CR; awk strips <!-- ... --> across lines.
    sed 's/\r$//' "${BODY_FILE}" | awk '
        BEGIN { in_comment = 0 }
        {
            line = $0
            out = ""
            while (1) {
                if (in_comment) {
                    idx = index(line, "-->")
                    if (idx == 0) { line = ""; break }
                    line = substr(line, idx + 3)
                    in_comment = 0
                } else {
                    idx = index(line, "<!--")
                    if (idx == 0) { out = out line; break }
                    out = out substr(line, 1, idx - 1)
                    line = substr(line, idx + 4)
                    in_comment = 1
                }
            }
            print out
        }
    '
}

# --- field extraction -------------------------------------------------------
#
# Keys are matched case-insensitively, anywhere in the body, with any leading
# whitespace or list marker. Authors reformat templates; the check should care
# about the citation being present and correct, not about it sitting at a
# particular line number. Only the first occurrence of a key counts, so a stray
# mention later in a description cannot override the real block.
#
# Everything up to the first colon is discarded rather than matching the key a
# second time in sed. sed's case-insensitive flag is a GNU extension, so matching
# the key twice would make this script pass under CI and fail on a contributor's
# machine — the class of difference that is worst to debug because the code looks
# right in both places.
extract() {
    local key="$1" body="$2"
    printf '%s\n' "${body}" \
        | grep -iE "^[[:space:]]*[-*]?[[:space:]]*${key}[[:space:]]*:" \
        | head -n 1 \
        | sed -E 's/^[^:]*:[[:space:]]*//' \
        | sed -E 's/[[:space:]]+$//'
}

# A value that is present but says nothing. Backticks, angle brackets and the
# template's own em-dash placeholder all count as absent — otherwise the check
# passes on an untouched template, which is the same as not having a check.
is_placeholder() {
    local value="$1"
    [ -z "${value}" ] && return 0
    case "$(printf '%s' "${value}" | tr '[:upper:]' '[:lower:]')" in
        -|--|—|–|n/a|na|none|tbd|todo|to\ do|xxx|\?|\?\?\?) return 0 ;;
    esac
    # <angle bracket placeholder> or `backtick placeholder` left from the template.
    # Matched with grep and a single-quoted ERE, not a case pattern: a backtick in
    # an unquoted case pattern is a command substitution waiting to happen, and a
    # validator that executes fragments of the thing it is validating is a
    # vulnerability rather than a check.
    printf '%s' "${value}" | grep -qE '^<.*>$|^`.*`$' && return 0
    return 1
}

# --- validation -------------------------------------------------------------

main() {
    local body
    body="$(read_body)" || return 1

    local objective decision kind reversal
    objective="$(extract "${KEY_OBJECTIVE}" "${body}")"
    decision="$(extract "${KEY_DECISION}" "${body}")"
    kind="$(extract "${KEY_KIND}" "${body}")"
    reversal="$(extract "${KEY_REVERSAL}" "${body}")"

    # Absent block, as opposed to a wrong one. Reported separately because the fix
    # is different: paste the template, versus correct the field you got wrong.
    if [ -z "${objective}${decision}${kind}" ]; then
        fail "no decision citation block found. FD-12 requires every pull request to carry one:"
        note ""
        note "Objective:  N3 — producer-loop scenario in CI"
        note "Decision:   FD-04 @ <sha of the governing entry>"
        note "Kind:       planned | corrective | unparked"
        note "Reversal:   <required only when Kind is unparked>"
        note ""
        note "See the decision record in Aestra-Internals."
        return 1
    fi

    # --- Kind ---------------------------------------------------------------
    # Validated first: it decides how strict the other three are.
    local kind_lc
    kind_lc="$(printf '%s' "${kind}" | tr '[:upper:]' '[:lower:]')"
    case "${kind_lc}" in
        planned|corrective|unparked) : ;;
        "")
            fail "Kind is missing. Must be one of: planned, corrective, unparked."
            ;;
        *)
            fail "Kind is '${kind}'. Must be one of: planned, corrective, unparked."
            note "planned    — advances an objective the strategic record authorises"
            note "corrective — fixes a defect in shipped or in-flight work"
            note "unparked   — starts work a decision forbids or parks; needs a new decision entry"
            ;;
    esac

    # --- Objective ----------------------------------------------------------
    # Corrective work is exempt from naming a strategic objective, because fixing a
    # defect does not advance one and pretending otherwise teaches authors to write
    # fiction in the field. Everything else must name what it serves.
    if is_placeholder "${objective}"; then
        if [ "${kind_lc}" = "corrective" ]; then
            note "Objective is empty; allowed because Kind is corrective."
        else
            fail "Objective is empty. Name the objective this work advances (e.g. 'N3 — producer-loop scenario')."
            note "If this is a defect fix rather than planned work, set Kind: corrective."
        fi
    fi

    # --- Decision -----------------------------------------------------------
    # FD-NN @ <7-40 hex>. Short SHAs are accepted because that is what people paste;
    # the workflow resolves whatever is given against the vault, and an ambiguous
    # short SHA fails there with a clearer message than a regex could give.
    if is_placeholder "${decision}"; then
        fail "Decision is empty. Cite the governing entry as 'FD-NN @ <sha>'."
    elif ! printf '%s' "${decision}" \
        | grep -qiE '^FD-[0-9]{2}[[:space:]]*@[[:space:]]*[0-9a-f]{7,40}$'; then
        fail "Decision is '${decision}', which is not of the form 'FD-NN @ <sha>'."
        if printf '%s' "${decision}" | grep -qiE '^FD-[0-9]{2}[[:space:]]*$'; then
            note "An entry id alone is not enough. The strategic record is a living file,"
            note "so 'FD-04' does not say which FD-04. The SHA is what makes staleness visible."
        fi
        note "Example: FD-04 @ a30696a"
    else
        local decision_id decision_sha
        decision_id="$(printf '%s' "${decision}" | sed -E 's/^([Ff][Dd]-[0-9]{2}).*/\1/' | tr '[:lower:]' '[:upper:]')"
        decision_sha="$(printf '%s' "${decision}" | sed -E 's/.*@[[:space:]]*//' | tr '[:upper:]' '[:lower:]')"
        note "Cited: ${decision_id} @ ${decision_sha}"
        if [ -n "${GITHUB_OUTPUT:-}" ]; then
            {
                printf 'decision-id=%s\n' "${decision_id}"
                printf 'decision-sha=%s\n' "${decision_sha}"
                printf 'kind=%s\n' "${kind_lc}"
            } >> "${GITHUB_OUTPUT}"
        fi
    fi

    # --- Reversal -----------------------------------------------------------
    # The whole point of FD-12. Starting work a decision parks is the one kind that
    # cannot be self-authorised, because self-authorisation is indistinguishable
    # from the deferral never having existed. It needs a decision entry, by id.
    if [ "${kind_lc}" = "unparked" ]; then
        if is_placeholder "${reversal}"; then
            fail "Kind is unparked, so Reversal must name the decision entry authorising it."
            note "Unparking work a decision forbids requires a NEW entry in the strategic"
            note "record, cited by id. Without one, the deferral has no force."
        elif ! printf '%s' "${reversal}" | grep -qiE 'FD-[0-9]{2}'; then
            fail "Reversal is '${reversal}', which names no decision entry."
            note "Cite the entry that authorises the reversal, e.g. 'FD-13 — hosting unparked'."
        fi
    elif ! is_placeholder "${reversal}" && [ "${kind_lc}" != "unparked" ]; then
        note "Reversal given but Kind is ${kind_lc}; ignored."
    fi

    [ "${errors}" -eq 0 ]
}

if main; then
    printf 'Decision citation OK.\n'
    exit 0
fi

printf '\nDecision citation check failed with %d error(s).\n' "${errors}"
printf 'FD-12: this vault is the durable authority for strategic scope.\n'
exit 1
