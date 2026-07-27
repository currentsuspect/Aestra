#!/usr/bin/env bash
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Tests for classify-changes.sh. This script gates REQUIRED status checks, so a bug
# here either blocks every merge or silently skips the compile that would have caught
# a break. It is worth more coverage than its size suggests.
#
# The important cases are the negative ones: the classifier is only allowed to say
# "skip-cxx" when it is certain, so most of this file is about proving it says
# "broad" whenever it is not.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLASSIFY="${HERE}/classify-changes.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT

failures=0
checks=0

# expect <expected> <label> <path>...
expect() {
    local expected="$1"; shift
    local label="$1"; shift
    local list="${TMP}/list"
    : > "${list}"
    local p
    for p in "$@"; do printf '%s\n' "${p}" >> "${list}"; done

    checks=$((checks + 1))
    local actual
    actual="$("${CLASSIFY}" "${list}")"
    if [[ "${actual}" == "${expected}" ]]; then
        printf 'PASS  %-58s -> %s\n' "${label}" "${actual}"
    else
        printf 'FAIL  %-58s -> %s (expected %s)\n' "${label}" "${actual}" "${expected}"
        failures=$((failures + 1))
    fi
}

# --- changes that provably cannot affect a C++ build ------------------------
expect skip-cxx "docs only"                      "docs/technical/roadmap.md"
expect skip-cxx "top-level prose"                "README.md" "LICENSING.md" "NOTICE"
expect skip-cxx "internal docs tree"             "AestraDocs/images/aestra_daw_interface.png"
expect skip-cxx "worker only"                    "workers/license-signing/package.json"
expect skip-cxx "worker + lockfile"              "workers/license-signing/package.json" \
                                                 "workers/license-signing/package-lock.json"
expect skip-cxx "docs + worker together"         "docs/index.md" "workers/license-signing/src/index.ts"
expect skip-cxx "mkdocs config"                  "mkdocs.yml"
expect skip-cxx "changelog"                      "meta/CHANGELOGS/CHANGELOG_2026Q2.md"

# Real PRs from 2026-07-25 that should have skipped the C++ matrix (#620).
expect skip-cxx "#619 as merged"                 "AestraDocs/images/aestra_daw_interface.png" \
                                                 "LICENSING.md" "NOTICE" "README.md" \
                                                 "docs/about/licensing.md"
expect skip-cxx "#615 as merged"                 "workers/license-signing/package.json" \
                                                 "workers/license-signing/package-lock.json"

# --- phase 2: changes only the UI/App compile lane can observe ---------------
# AESTRA_CI=ON force-disables AESTRA_ENABLE_UI, and every lane but "Linux (UI/App
# compile)" sets it — so nothing else compiles these files.
expect ui-app-only "UI widget source"            "AestraUI/Widgets/UIMixerButtonRow.cpp"
expect ui-app-only "app source"                  "Source/App/AestraApp.cpp"
expect ui-app-only "component source"            "Source/Components/TrackManagerUIClipOps.cpp"
expect ui-app-only "several UI sources"          "AestraUI/Widgets/UIMixerButtonRow.cpp" \
                                                 "Source/Panels/MixerPanel.cpp"
expect ui-app-only "UI source plus docs"         "docs/index.md" "Source/App/AestraApp.cpp"

# Headers are never eligible: a header under these trees may be included by one of
# the .cpp files that headless targets DO compile, and one-level grepping cannot
# prove otherwise.
expect broad "UI header"                         "AestraUI/Widgets/UIMixerButtonRow.h"
expect broad "app header"                        "Source/App/AestraApp.h"
expect broad "UI source plus its own header"     "Source/App/AestraApp.cpp" "Source/App/AestraApp.h"

# .cpp files under the UI trees that headless targets compile by name. These are read
# out of the CMake files at classification time; if that derivation breaks, these
# cases start failing rather than silently widening the skip.
expect broad "serializer (compiled by tests)"    "Source/Core/ProjectSerializer.cpp"
expect broad "take manager (compiled by tests)"  "Source/Core/TakeManager.cpp"
expect broad "headless main"                     "Source/App/HeadlessMain.cpp"
expect broad "muse agent"                        "Source/MuseAgent/AgentLoop.cpp"
expect broad "panel compiled by a test"          "Source/Panels/WindowPanel.cpp"
expect broad "theme compiled by a test"          "AestraUI/Core/NUITheme.cpp"
expect broad "cursor service compiled by a test" "AestraUI/Platform/NUICursorService.cpp"
expect broad "one headless source among UI"      "Source/App/AestraApp.cpp" \
                                                 "Source/Core/ProjectSerializer.cpp"

# --- anything touching the build must be broad ------------------------------
expect broad "C++ source"                        "AestraAudio/src/Core/AudioEngine.cpp"
expect broad "audio source with UI source"       "AestraAudio/src/Core/AudioEngine.cpp" \
                                                 "Source/App/AestraApp.cpp"
expect broad "header"                            "AestraCore/include/AestraJSON.h"
expect broad "root CMakeLists"                   "CMakeLists.txt"
expect broad "cmake module"                      "cmake/LowMemory.cmake"
expect broad "test source"                       "Tests/Commands/MuseServiceTest.cpp"
expect broad "test cmake fragment"               "Tests/cmake/CommandsTests.cmake"
expect broad "security test tree"                "tests/security/out_of_process_plugin_host.cpp"
expect broad "workflow change"                   ".github/workflows/ci.yml"
expect broad "the classifier itself"             "scripts/ci/classify-changes.sh"

# --- one unrecognised path poisons an otherwise skippable set ---------------
expect broad "docs plus one source file"         "docs/index.md" "AestraAudio/src/Core/AudioEngine.cpp"
expect broad "worker plus one header"            "workers/license-signing/package.json" \
                                                 "AestraCore/include/AestraLog.h"
expect broad "mostly docs, one CMake"            "README.md" "docs/index.md" "CMakeLists.txt"

# --- paths that merely LOOK like the safe ones ------------------------------
# These are the ones an unanchored pattern gets wrong, and the reason every rule
# in the classifier is anchored at the start of the path.
# These two are app sources whose paths contain a safe-list word. Phase 2 classifies
# them as UI/app work, which is the point: what must never happen is their being read
# as documentation or as the worker tree and skipping the C++ matrix entirely.
expect ui-app-only "source file named after docs"    "Source/Panels/AestraDocsPanel.cpp"
expect ui-app-only "source path containing 'workers'" "Source/Core/WorkersPool.cpp"
expect broad "a README inside a source tree"     "AestraAudio/README.md"
expect broad "top-level file not on the list"    "requirements.txt"
expect broad "new unknown directory"             "AestraNewModule/src/Thing.cpp"
expect broad "dotfile not on the list"           ".clang-format"

# --- degenerate and hostile input -------------------------------------------
expect broad "empty change set"                  ""
: > "${TMP}/list"; checks=$((checks + 1))
if [[ "$("${CLASSIFY}" "${TMP}/list")" == "broad" ]]; then
    printf 'PASS  %-58s -> broad\n' "genuinely empty file"
else
    printf 'FAIL  %-58s\n' "genuinely empty file"; failures=$((failures + 1))
fi

checks=$((checks + 1))
if [[ "$("${CLASSIFY}" "${TMP}/does-not-exist")" == "broad" ]]; then
    printf 'PASS  %-58s -> broad\n' "missing file list"
else
    printf 'FAIL  %-58s\n' "missing file list"; failures=$((failures + 1))
fi

checks=$((checks + 1))
if [[ "$("${CLASSIFY}")" == "broad" ]]; then
    printf 'PASS  %-58s -> broad\n' "no argument at all"
else
    printf 'FAIL  %-58s\n' "no argument at all"; failures=$((failures + 1))
fi

# --- the truncation guard ---------------------------------------------------
# GitHub's changed-files API truncates at 300 entries. A truncated list is
# indistinguishable from a genuinely smaller change, so a large docs-only set must
# still fall back to broad rather than trusting a count it cannot verify.
big="${TMP}/big"; : > "${big}"
for i in $(seq 1 250); do printf 'docs/page-%s.md\n' "${i}" >> "${big}"; done
checks=$((checks + 1))
if [[ "$("${CLASSIFY}" "${big}")" == "broad" ]]; then
    printf 'PASS  %-58s -> broad\n' "250 docs files (over the trust threshold)"
else
    printf 'FAIL  %-58s\n' "250 docs files (over the trust threshold)"; failures=$((failures + 1))
fi

# Just under the threshold, all provably safe: skipping is allowed.
small="${TMP}/small"; : > "${small}"
for i in $(seq 1 150); do printf 'docs/page-%s.md\n' "${i}" >> "${small}"; done
checks=$((checks + 1))
if [[ "$("${CLASSIFY}" "${small}")" == "skip-cxx" ]]; then
    printf 'PASS  %-58s -> skip-cxx\n' "150 docs files (under the threshold)"
else
    printf 'FAIL  %-58s\n' "150 docs files (under the threshold)"; failures=$((failures + 1))
fi

# --- the truncation cross-check --------------------------------------------
# The size guard alone cannot catch truncation: a truncated list is SHORT, so it
# sails under the threshold and classifies narrow on a change it never saw. The
# expected-count argument is what closes that.
tc() {  # tc <expected-out> <label> <expected-count> <path>...
    local want="$1"; shift; local label="$1"; shift; local n="$1"; shift
    local list="${TMP}/tc"; : > "${list}"
    local p; for p in "$@"; do printf '%s\n' "${p}" >> "${list}"; done
    checks=$((checks + 1))
    local got; got="$("${CLASSIFY}" "${list}" "${n}")"
    if [[ "${got}" == "${want}" ]]; then
        printf 'PASS  %-58s -> %s\n' "${label}" "${got}"
    else
        printf 'FAIL  %-58s -> %s (expected %s)\n' "${label}" "${got}" "${want}"
        failures=$((failures + 1))
    fi
}

tc skip-cxx "count matches, all safe"            2 "docs/a.md" "README.md"
tc broad    "list truncated (2 of 40 returned)"  40 "docs/a.md" "README.md"
tc broad    "list longer than reported"          1 "docs/a.md" "README.md"
tc broad    "expected count not a number"        "abc" "docs/a.md"
tc broad    "expected count empty-ish garbage"   "-1" "docs/a.md"
tc broad    "truncated AND source present"       40 "docs/a.md" "AestraAudio/src/x.cpp"

# --- the derivation must fail safe, not fail open ---------------------------
# The ui-app-only rule depends on reading the headless-compiled .cpp list out of the
# CMake files. If that read returns nothing — moved file, renamed variable, a grep
# that stopped matching — every UI path would look skippable and the classifier would
# quietly start skipping the one lane that compiles them. Copy the script somewhere
# with no repository around it and confirm it refuses to classify narrow.
orphan="${TMP}/orphan/scripts/ci"
mkdir -p "${orphan}"
cp "${CLASSIFY}" "${orphan}/classify-changes.sh"
printf 'Source/App/AestraApp.cpp\n' > "${TMP}/orphan-list"
checks=$((checks + 1))
orphan_verdict="$(bash "${orphan}/classify-changes.sh" "${TMP}/orphan-list")"
if [[ "${orphan_verdict}" == "broad" ]]; then
    printf 'PASS  %-58s -> broad\n' "no CMake files to derive from (fail safe)"
else
    printf 'FAIL  %-58s -> %s (expected broad)\n' \
        "no CMake files to derive from (fail safe)" "${orphan_verdict}"
    failures=$((failures + 1))
fi

# LIVENESS, stated directly rather than left implicit. Every ui-app-only expectation
# above would also pass if the classifier had degraded into answering broad to
# everything, because broad is the safe answer and safety is what most of this file
# asserts. Name the case that can only pass when the feature is actually working.
checks=$((checks + 1))
printf 'AestraUI/Widgets/UIMixerButtonRow.cpp\n' > "${TMP}/live-list"
live_verdict="$("${CLASSIFY}" "${TMP}/live-list")"
if [[ "${live_verdict}" == "ui-app-only" ]]; then
    printf 'PASS  %-58s -> ui-app-only\n' "liveness: the narrow verdict is reachable"
else
    printf 'FAIL  %-58s -> %s (expected ui-app-only)\n' \
        "liveness: the narrow verdict is reachable" "${live_verdict}"
    failures=$((failures + 1))
fi

echo
if [[ "${failures}" -eq 0 ]]; then
    echo "ALL PASSED (${checks} checks)"
    exit 0
fi
echo "FAILURES: ${failures} of ${checks}"
exit 1
