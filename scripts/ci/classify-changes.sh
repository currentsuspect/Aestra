#!/usr/bin/env bash
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Decide whether a changed-file set can safely skip the C++ build lanes.
#
# Every PR currently recompiles the universe: 85 runner-minutes, with a 17.9-minute
# critical path set by Windows (MSVC). A docs-only change waits for the whole engine
# to compile on three platforms in order to approve a Markdown edit. See issue #620.
#
# THE CONTRACT — this may only save compute when it is CERTAIN. It must never guess
# narrow. Anything unrecognised, oversized, truncated, or aimed at main falls back to
# "broad", because classifier uncertainty is allowed to cost compute and is never
# allowed to cost correctness.
#
# Phase 1 is deliberately coarse: it answers one question — "is this change entirely
# within a set of paths that provably cannot affect a C++ build?" — and nothing else.
# There is no per-subsystem selection here, because Aestra's lanes are organised by
# PLATFORM, not by subsystem: each of Linux/Windows/macOS runs the whole suite. The
# safe unit today is therefore the whole lane. Subsystem and test-level selection are
# phases 2 and 3 of #620, and phase 3 is blocked on 45 unlabelled tests.
#
# Usage:  classify-changes.sh <file-with-one-changed-path-per-line> [expected-count]
#
# expected-count is the PR's authoritative changedFiles total. When supplied, the
# list must contain exactly that many entries or the answer is broad. Without it,
# a SILENTLY TRUNCATED list is indistinguishable from a genuinely small change:
# the size guard below only fires when the returned list is large, and a truncated
# list is by definition short. The cross-check is what closes that hole.
#
# Output: "broad" or "skip-cxx" on stdout.
#
# Exit status is 0 for a successful classification; a non-zero exit means the caller
# must treat the result as broad.

set -uo pipefail

FILE_LIST="${1:-}"
EXPECTED_COUNT="${2:-}"

# Above this many changed files, do not trust the list. GitHub's changed-files API
# truncates at 300 entries, and a truncated list looks exactly like a smaller change
# — it would read as "fewer paths touched" and classify narrower than reality. A
# 129-file promotion PR is on record, and promotions are precisely the changes that
# most need the full matrix.
readonly MAX_TRUSTED_FILES=200

# Paths that cannot affect a C++ compile or test run. Anchored, so a path merely
# CONTAINING one of these words (docs/workers-guide.md, Source/AestraDocsPanel.cpp)
# does not match. Every entry here is a promise; add to it only with evidence.
is_cxx_irrelevant() {
    case "$1" in
        # Documentation trees and top-level prose.
        docs/*|AestraDocs/*|meta/*) return 0 ;;
        README.md|LICENSE|LICENSING.md|NOTICE|CHANGELOG.md|CONTRIBUTING.md) return 0 ;;
        CODE_OF_CONDUCT.md|SECURITY.md|SUPPORT.md|RELEASES.md|BUILD.md|philosophy.md) return 0 ;;
        mkdocs.yml|Doxyfile) return 0 ;;
        # The Cloudflare worker: TypeScript, its own toolchain, its own CI lane.
        workers/*) return 0 ;;
        # Files that are inert everywhere.
        .gitignore|.gitattributes|.editorconfig) return 0 ;;
        *) return 1 ;;
    esac
}

main() {
    # No list, unreadable list, or empty list: we know nothing. Broad.
    if [[ -z "${FILE_LIST}" || ! -r "${FILE_LIST}" ]]; then
        echo "broad"
        return 0
    fi

    # NOT `grep -c . file || echo 0`: grep exits non-zero on no match AND prints 0,
    # so the fallback appends a second 0 and the count becomes the string "0\n0".
    # That is not a number, every numeric guard below then errors out instead of
    # firing, and control falls through to the loop — which reads nothing and
    # returns skip-cxx. An empty change set would have skipped the whole matrix.
    local count
    count=$(grep -c . "${FILE_LIST}" 2>/dev/null) || count=0

    if [[ "${count}" -eq 0 ]]; then
        echo "broad"
        return 0
    fi

    if [[ "${count}" -ge "${MAX_TRUSTED_FILES}" ]]; then
        echo "broad"
        return 0
    fi

    # Cross-check against the PR's own changedFiles total. Any disagreement means
    # the list we classified is not the change that is being merged — truncation,
    # pagination stopping early, or a partial fetch — so we do not trust it.
    if [[ -n "${EXPECTED_COUNT}" ]]; then
        if ! [[ "${EXPECTED_COUNT}" =~ ^[0-9]+$ ]]; then
            echo "broad"
            return 0
        fi
        if [[ "${count}" -ne "${EXPECTED_COUNT}" ]]; then
            echo "broad"
            return 0
        fi
    fi

    # A single unrecognised path is enough to force the full matrix.
    local path
    while IFS= read -r path; do
        [[ -z "${path}" ]] && continue
        if ! is_cxx_irrelevant "${path}"; then
            echo "broad"
            return 0
        fi
    done < "${FILE_LIST}"

    echo "skip-cxx"
}

main
