#!/usr/bin/env bash
# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Tests for check-rt-effects.sh — the B-005 compile-time constraint gate.
#
# THE DIRECTION THAT MATTERS IS THE FALSE PASS.
#
# A wrong rejection is loud: someone sees a red check and a diagnostic naming a
# function. A wrong acceptance is silent, and this gate's whole product is the
# sentence "the audio thread provably does not allocate or lock". A gate that
# emits that sentence without testing it is worse than no gate, because the claim
# then travels — into review, into the changelog, into a release note.
#
# So the tests below are mostly hostile, and the two that matter most are:
#
#   * a genuinely violating translation unit must FAIL, and the failure must
#     name -Wfunction-effects. Asserting merely "exit != 0" would pass if the
#     compiler crashed, if a header went missing, or if the script had a typo in
#     a variable name — three ways to look like a working gate while checking
#     nothing.
#   * a tree with no annotations at all must FAIL rather than report success on
#     an empty set. "0 of 0 translation units are clean" is true and useless.
#
# Everything here is hermetic: each case builds its own tiny source tree and its
# own compile_commands.json, so the tests never depend on the repo being
# configured, on a build directory existing, or on the current annotation set.

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECK="${HERE}/check-rt-effects.sh"
CXX="${CXX:-clang++}"

failures=0
checks=0

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "SKIP: ${CXX} not available; the gate under test requires Clang."
    exit 0
fi

# Build a throwaway repo-shaped tree: AestraAudio/src/<name>.cpp plus a build
# directory holding a compile_commands.json that points at it.
#
# The script derives its file list by grepping AestraAudio/src and Source, so the
# fixture has to have that shape for the derivation to be under test at all. A
# fixture that fed the script a file list directly would be testing a different
# program from the one CI runs.
# The third argument is extra compile flags, defaulting to a define whose value
# contains quotes. That default is deliberate and is the regression: the script
# used to hand args to the compiler through an unquoted shell string, so a flag
# needing quoting arrived with its quote characters intact and clang reported a
# missing file. Every flag in the tree was quote-free until AESTRA_VERSION_STRING
# was added, so the gate broke on its own plumbing the day a version reached it.
# Baking it into the default fixture means every case below now exercises it.
make_fixture() {
    local root="$1" body="$2"
    # Written as the compiler would receive it: a define whose *value* is a
    # quoted string, exactly the shape CMake emits for AESTRA_VERSION_STRING.
    local flags="${3:--DAESTRA_VERSION_STRING=\"0.0.0\"}"
    mkdir -p "${root}/AestraAudio/src" "${root}/build"
    cat >"${root}/AestraAudio/src/unit.cpp" <<EOF
#define AESTRA_RT_NONBLOCKING [[clang::nonblocking]]
${body}
EOF
    # A header the unit includes, plus the -I that finds it, in EVERY fixture
    # rather than only the header-only one — so the include graph is exercised
    # by the whole suite and the header-only case is not a special path.
    mkdir -p "${root}/AestraAudio/include"
    cat >"${root}/AestraAudio/include/unit.h" <<'EOF'
#pragma once
// Deliberately declares nothing. The .cpp below defines mix, and a bare
// declaration beside a definition that carries AESTRA_RT_NONBLOCKING is an
// attribute-mismatch error rather than a check -- which would have made these
// fixtures fail for a reason unrelated to the gate.
EOF
    if ! grep -q 'unit\.h' "${root}/AestraAudio/src/unit.cpp"; then
        sed -i '1i #include "unit.h"' "${root}/AestraAudio/src/unit.cpp"
    fi
    # Generated rather than heredoc'd. The command field has to survive two
    # decodings -- JSON, then shlex -- and hand-escaping through both in a
    # shell heredoc is how you end up testing your own escaping instead of the
    # script. Python does both layers correctly, so the fixture holds what a
    # real compile_commands.json holds.
    AESTRA_FIXTURE_ROOT="$root" AESTRA_FIXTURE_CXX="$CXX" \
    AESTRA_FIXTURE_FLAGS="$flags" python3 - <<'PY'
import json, os, shlex
root = os.environ["AESTRA_FIXTURE_ROOT"]
src = f"{root}/AestraAudio/src/unit.cpp"
# ONE literal argument, quoted so shlex.split hands it back byte-for-byte --
# which is precisely what CMake does when a define's value contains quotes or
# spaces. Splitting it on whitespace first would strip the very quotes this
# fixture exists to carry, and the test would then pass for the wrong reason.
flag = shlex.quote(os.environ["AESTRA_FIXTURE_FLAGS"])
command = (f'{os.environ["AESTRA_FIXTURE_CXX"]} -std=c++20 {flag} '
           f'-I {root}/AestraAudio/include -c -o unit.o {src}')
with open(f"{root}/build/compile_commands.json", "w") as fh:
    json.dump([{"directory": root, "command": command, "file": src}], fh, indent=2)
PY
}

# expect <pass|fail> <label> <build-dir-relative> [required-substring]
#
# The optional fourth argument is what keeps a rejection honest: it asserts the
# output explains the rejection for the expected reason, not an accidental one.
expect() {
    local expected="$1" label="$2" root="$3" needle="${4:-}"
    local actual status got
    checks=$((checks + 1))

    actual="$(cd "${root}" 2>/dev/null &&
              AESTRA_RT_EFFECT_ROOT="${root}" bash "${CHECK}" build 2>&1)"
    status=$?

    got="pass"
    [ "${status}" -ne 0 ] && got="fail"

    if [ "${got}" != "${expected}" ]; then
        printf 'FAIL  %-56s -> %s (expected %s)\n' "${label}" "${got}" "${expected}"
        printf '%s\n' "${actual}" | sed 's/^/        | /'
        failures=$((failures + 1))
        return
    fi

    if [ -n "${needle}" ] && ! printf '%s' "${actual}" | grep -qF -- "${needle}"; then
        printf 'FAIL  %-56s -> %s, but output never mentioned %s\n' \
               "${label}" "${got}" "${needle}"
        printf '%s\n' "${actual}" | sed 's/^/        | /'
        failures=$((failures + 1))
        return
    fi

    printf 'PASS  %-56s -> %s\n' "${label}" "${got}"
}

WORK="$(mktemp -d -t rt-effects-tests-XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

# ── A clean annotated unit passes ───────────────────────────────────────────
make_fixture "${WORK}/clean" '
void mix(float* out, unsigned n) AESTRA_RT_NONBLOCKING {
    for (unsigned i = 0; i < n; ++i) out[i] *= 0.5f;
}'
expect pass "clean annotated translation unit" "${WORK}/clean"

# ── An annotation that lives only in a HEADER must still be checked ────────
# This is the case the whole derivation change exists for. Every built-in
# effect is header-only, so its AESTRA_RT_NONBLOCKING sits in a .h while the
# translation unit that compiles it says nothing. The old rule --
# grep --include='*.cpp' -- could not see those annotations at all, and the
# gate reported three translation units while eleven headers carried them.
#
# Here the .cpp includes a header holding a *violating* annotation and does
# not mention the macro itself, so a derivation that greps .cpp files passes
# this and a derivation that follows includes fails it. The failing reason is
# asserted too, so this cannot pass by failing for some unrelated reason.
make_fixture "${WORK}/header_only" '
extern void mix(float*, unsigned);
void use(float* out, unsigned n) { mix(out, n); }'
cat >"${WORK}/header_only/AestraAudio/include/unit.h" <<'EOF'
#pragma once
#define AESTRA_RT_NONBLOCKING [[clang::nonblocking]]
// Attribute on the declaration, which is where RealtimeThreadGuard.h says it
// belongs; repeating it on the definition is a mismatch, and a mismatch
// mentions -Wfunction-effects too -- so a test asserting that substring would
// have passed for a reason that has nothing to do with the check.
void mix(float* out, unsigned n) AESTRA_RT_NONBLOCKING;
inline void mix(float* out, unsigned n) AESTRA_RT_NONBLOCKING {
    float* scratch = new float[n];
    for (unsigned i = 0; i < n; ++i) out[i] = scratch[i];
    delete[] scratch;
}
EOF
expect fail "violation reached through a header include" \
    "${WORK}/header_only" "function-effects"

# ── And a TU that includes an annotated header with no violation passes ───
make_fixture "${WORK}/header_clean" '
extern void mix(float*, unsigned);
void use(float* out, unsigned n) { mix(out, n); }'
cat >"${WORK}/header_clean/AestraAudio/include/unit.h" <<'EOF'
#pragma once
#define AESTRA_RT_NONBLOCKING [[clang::nonblocking]]
void mix(float* out, unsigned n) AESTRA_RT_NONBLOCKING;
inline void mix(float* out, unsigned n) AESTRA_RT_NONBLOCKING {
    for (unsigned i = 0; i < n; ++i) out[i] *= 0.5f;
}
EOF
expect pass "clean annotation reached through a header include" \
    "${WORK}/header_clean"

# ── A flag whose value contains quotes must reach the compiler intact ───────
# The unit reads the define, so if the flag were mangled in transit this fails
# to compile rather than passing for the wrong reason.
make_fixture "${WORK}/quoted" '
const char* version() { return AESTRA_VERSION_STRING; }
void mix(float* out, unsigned n) AESTRA_RT_NONBLOCKING {
    for (unsigned i = 0; i < n; ++i) out[i] *= 0.5f;
}'
expect pass "define whose value contains quotes" "${WORK}/quoted"

# ── And a flag containing a space survives too ─────────────────────────────
# The same defect class: anything the old shell round-trip had to quote.
make_fixture "${WORK}/spaced" '
const char* who() { return AESTRA_BUILD_LABEL; }
void mix(float* out, unsigned n) AESTRA_RT_NONBLOCKING {
    for (unsigned i = 0; i < n; ++i) out[i] *= 0.5f;
}' '-DAESTRA_BUILD_LABEL="Aestra 0.7.1 (Core)"'
expect pass "define whose value contains a space" "${WORK}/spaced"

# ── Allocation is caught, FOR THE RIGHT REASON ──────────────────────────────
make_fixture "${WORK}/alloc" '
void mix(float* out, unsigned n) AESTRA_RT_NONBLOCKING {
    float* scratch = new float[n];
    for (unsigned i = 0; i < n; ++i) out[i] = scratch[i];
    delete[] scratch;
}'
expect fail "allocation on an annotated path" "${WORK}/alloc" "function-effects"

# ── So is a lock ────────────────────────────────────────────────────────────
make_fixture "${WORK}/lock" '
#include <mutex>
static std::mutex g_m;
void mix(float*, unsigned) AESTRA_RT_NONBLOCKING { g_m.lock(); g_m.unlock(); }'
expect fail "mutex lock on an annotated path" "${WORK}/lock" "function-effects"

# ── And a call the compiler cannot clear, which is the common real case ─────
make_fixture "${WORK}/opaque" '
void elsewhere();
void mix(float*, unsigned) AESTRA_RT_NONBLOCKING { elsewhere(); }'
expect fail "call to a function with no visible definition" "${WORK}/opaque" \
       "function-effects"

# ── An unannotated violation is NOT the gate's business ─────────────────────
# Opt-in is the adoption story: turning the flag on must not light up code that
# has made no claim. If this ever fails, the gate has become a tree-wide lint and
# the incremental-adoption argument in RealtimeThreadGuard.h is no longer true.
make_fixture "${WORK}/optout" '
void allocates(unsigned n) { delete[] new float[n]; }
void mix(float* out, unsigned n) AESTRA_RT_NONBLOCKING {
    for (unsigned i = 0; i < n; ++i) out[i] = 0.0f;
}'
expect pass "unannotated neighbour may still allocate" "${WORK}/optout"

# ── An empty annotation set must not report success ─────────────────────────
mkdir -p "${WORK}/empty/AestraAudio/src" "${WORK}/empty/build"
echo 'void mix(float*, unsigned) {}' > "${WORK}/empty/AestraAudio/src/unit.cpp"
echo '[]' > "${WORK}/empty/build/compile_commands.json"
expect fail "no annotated translation units anywhere" "${WORK}/empty" \
       "no translation unit carries"

# ── Missing inputs fail closed ─────────────────────────────────────────────
mkdir -p "${WORK}/nodb/AestraAudio/src" "${WORK}/nodb/build"
printf '#define AESTRA_RT_NONBLOCKING [[clang::nonblocking]]\nvoid mix() AESTRA_RT_NONBLOCKING {}\n' \
       > "${WORK}/nodb/AestraAudio/src/unit.cpp"
expect fail "compile_commands.json absent" "${WORK}/nodb" "compile_commands.json"

mkdir -p "${WORK}/nobuild"
expect fail "build directory absent" "${WORK}/nobuild" "does not exist"

# ── An annotated unit missing from the database is a coverage hole ─────────
# This is the silent-gap case the derived file list exists to prevent: the
# annotation says "checked", the database says "not compiled here", and without
# this rule the script would report a clean pass over a file it never opened.
make_fixture "${WORK}/orphan" '
void mix(float*, unsigned) AESTRA_RT_NONBLOCKING {}'
echo '[]' > "${WORK}/orphan/build/compile_commands.json"
expect fail "annotated unit absent from compile_commands" "${WORK}/orphan" \
       "no compile"

echo
if [ "${failures}" -ne 0 ]; then
    echo "${failures} of ${checks} checks FAILED."
    exit 1
fi
echo "All ${checks} checks passed."
