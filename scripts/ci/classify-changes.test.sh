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

# --- anything touching the build must be broad ------------------------------
expect broad "C++ source"                        "AestraAudio/src/Core/AudioEngine.cpp"
expect broad "UI source"                         "AestraUI/Widgets/UIMixerButtonRow.cpp"
expect broad "app source"                        "Source/App/AestraApp.cpp"
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
expect broad "source file named after docs"      "Source/Panels/AestraDocsPanel.cpp"
expect broad "source path containing 'workers'"  "Source/Core/WorkersPool.cpp"
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

echo
if [[ "${failures}" -eq 0 ]]; then
    echo "ALL PASSED (${checks} checks)"
    exit 0
fi
echo "FAILURES: ${failures} of ${checks}"
exit 1
