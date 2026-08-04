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
# Phase 1 was deliberately coarse: it answered one question — "is this change entirely
# within a set of paths that provably cannot affect a C++ build?" — and nothing else.
#
# Phase 2 adds one more verdict, and it rests on a fact about how Aestra is built
# rather than on a judgement about how coupled its subsystems feel:
#
#   AESTRA_CI=ON force-disables AESTRA_ENABLE_UI (CMakeLists.txt), and every lane
#   except "Linux (UI/App compile)" passes AESTRA_CI=ON.
#
# So AestraUI/ and Source/ are compiled by exactly ONE lane. A change confined to
# them cannot break the others, because the others never see those files — the same
# reason the UI/App lane had to be added in the first place (#396, #443).
#
# The exception, and the reason this is derived rather than hardcoded: a handful of
# .cpp files under Source/ and AestraUI/ are named explicitly in the root and test
# CMakeLists and therefore DO compile in headless builds — ProjectSerializer, the
# Muse agent, the panel tests. Those are read out of the CMake files at classification
# time (see headless_compiled_sources), so adding one cannot silently invalidate the
# rule later.
#
# Headers are not eligible. A .h under Source/ or AestraUI/ may be included by one of
# those headless-compiled .cpp files, and proving otherwise needs a real include graph.
#
# Test-level selection is phase 3 of #620 and remains blocked on 45 unlabelled tests.
#
# Usage:  classify-changes.sh <file-with-one-changed-path-per-line> [expected-count]
#
# expected-count is the PR's authoritative changedFiles total. When supplied, the
# list must contain exactly that many entries or the answer is broad. Without it,
# a SILENTLY TRUNCATED list is indistinguishable from a genuinely small change:
# the size guard below only fires when the returned list is large, and a truncated
# list is by definition short. The cross-check is what closes that hole.
#
# Output, on stdout, one of:
#   broad        run every lane
#   skip-cxx     nothing here can affect a C++ build; no C++ lane needs to run
#   ui-app-only  only the UI/App compile lane can observe this change
#
# scripts/ci/lane-runs.sh turns a verdict into a per-lane yes/no.
#
# Exit status is 0 for a successful classification; a non-zero exit means the caller
# must treat the result as broad.

set -uo pipefail

FILE_LIST="${1:-}"
EXPECTED_COUNT="${2:-}"

# Scratch file holding headless_compiled_sources(); populated by main().
HEADLESS_LIST=""

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
        docs/*|meta/*) return 0 ;;
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

# The repository this script lives in. Derived from the script's own location so it
# does not depend on the caller's working directory.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." 2>/dev/null && pwd)" || REPO_ROOT=""

# .cpp files under Source/ or AestraUI/ that headless builds compile anyway, because
# the root or test CMakeLists names them explicitly rather than reaching them through
# add_subdirectory(Source) / add_subdirectory(AestraUI) — both of which are inside
# `if(AESTRA_ENABLE_UI)` and therefore absent whenever AESTRA_CI=ON.
#
# Read out of the CMake files rather than pinned here: a hardcoded list would go stale
# the first time somebody adds a Source/*.cpp to a headless test target, and it would
# go stale INTO A SKIP — the direction that costs correctness. Source/CMakeLists.txt is
# deliberately not consulted; that file is the UI application itself.
#
# Over-capture is harmless. The list is only ever used to force a path back to broad,
# so matching too much costs compute and never costs correctness.
headless_compiled_sources() {
    [[ -n "${REPO_ROOT}" ]] || return 1
    local files=("${REPO_ROOT}/CMakeLists.txt" "${REPO_ROOT}/Tests/CMakeLists.txt")
    local f
    for f in "${REPO_ROOT}"/Tests/cmake/*.cmake; do
        [[ -r "${f}" ]] && files+=("${f}")
    done
    grep -rhoE '(Source|AestraUI)/[A-Za-z0-9_./-]+\.cpp' "${files[@]}" 2>/dev/null | sort -u
}

# Is this path compiled only by the "Linux (UI/App compile)" lane?
is_ui_app_only() {
    # `case` globs span '/', so these cover every depth under the two trees.
    case "$1" in
        AestraUI/*.cpp|Source/*.cpp) ;;
        *) return 1 ;;
    esac
    # Named by a headless CMake target: every lane compiles it.
    grep -qxF -- "$1" "${HEADLESS_LIST}" && return 1
    return 0
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

    # Materialise the headless-compiled set once. If it comes back empty the
    # derivation is broken — wrong working directory, renamed CMake file, a grep that
    # stopped matching — and every UI path would look skippable. That is a fail-open,
    # so refuse to classify narrow at all rather than trust it.
    HEADLESS_LIST="$(mktemp)" || { echo "broad"; return 0; }
    trap 'rm -f "${HEADLESS_LIST}"' EXIT
    headless_compiled_sources > "${HEADLESS_LIST}"
    local headless_count
    headless_count=$(grep -c . "${HEADLESS_LIST}" 2>/dev/null) || headless_count=0
    local trust_ui_rule=1
    if [[ "${headless_count}" -eq 0 ]]; then
        trust_ui_rule=0
    fi

    # A single unrecognised path is enough to force the full matrix.
    local path
    local saw_ui_app=0
    while IFS= read -r path; do
        [[ -z "${path}" ]] && continue
        if is_cxx_irrelevant "${path}"; then
            continue
        fi
        if [[ "${trust_ui_rule}" -eq 1 ]] && is_ui_app_only "${path}"; then
            saw_ui_app=1
            continue
        fi
        echo "broad"
        return 0
    done < "${FILE_LIST}"

    if [[ "${saw_ui_app}" -eq 1 ]]; then
        echo "ui-app-only"
        return 0
    fi

    echo "skip-cxx"
}

main
