# Self-test for NoRtThreadCpuDetectionGuard.
#
# A textual tripwire that cannot fail is worse than no guard: it reports clean
# forever and everyone trusts it. So this builds fixture trees and asserts both
# directions —
#
#   MUST FAIL: each CPU token inside a process() body, including one buried
#              mid-body. All three AestraVerb sites sat in the middle of a
#              several-hundred-line process(), and a line-scoped or
#              first-match-only check would have missed every one.
#   MUST PASS: the correct shape (resolve in initialize, read plain members),
#              a non-process() CPU query, and vendored code.
#
# Required -D args:
#   GUARD_SCRIPT  absolute path to the guard under test
#   WORK_DIR      scratch directory for fixtures

if(NOT GUARD_SCRIPT)
    message(FATAL_ERROR "GUARD_SCRIPT must be provided")
endif()
if(NOT WORK_DIR)
    message(FATAL_ERROR "WORK_DIR must be provided")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(failures 0)
set(checks 0)

function(expect_guard label fixture_root expected)
    math(EXPR n "${checks} + 1")
    set(checks ${n} PARENT_SCOPE)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DREPO_ROOT=${fixture_root} -P ${GUARD_SCRIPT}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    set(out "${out}${err}")

    if(expected STREQUAL "fail")
        if(rc EQUAL 0)
            math(EXPR n "${failures} + 1")
            set(failures ${n} PARENT_SCOPE)
            message("FAIL  ${label}  -> passed, but a CPU query in process() must fail")
        else()
            message("PASS  ${label}  -> fail")
        endif()
    else()
        if(NOT rc EQUAL 0)
            math(EXPR n "${failures} + 1")
            set(failures ${n} PARENT_SCOPE)
            message("FAIL  ${label}  -> ${rc}")
            string(REGEX MATCH "process\\(\\) body references[^
]*" first "${out}")
            if(first)
                message("        | ${first}")
            endif()
        else()
            message("PASS  ${label}  -> pass")
        endif()
    endif()
endfunction()

# Writes a fixture with a Plugin header plus a .cpp that includes it.
function(make_plugin_fixture root header_body)
    file(MAKE_DIRECTORY "${root}/AestraAudio/include/Plugin")
    file(MAKE_DIRECTORY "${root}/AestraAudio/src")
    file(WRITE "${root}/AestraAudio/include/Plugin/Foo.h" "${header_body}")
    file(WRITE "${root}/AestraAudio/src/Foo.cpp"
        "#include \"Plugin/Foo.h\"\n")
endfunction()

# ── The defect, in each of its real shapes ─────────────────────────────────
# Each mirrors how the bug actually appeared rather than a minimal token: a
# function-local static initialized from CPUDetection::get(), which is what
# produced both the guard variable and the CPUID on the audio thread.

make_plugin_fixture("${WORK_DIR}/static_local"
"#pragma once
class Foo {
public:
    void process(float* out, unsigned n);
    void initialize(double sr);
private:
    bool m_useAVX2 = false;
};
")

file(APPEND "${WORK_DIR}/static_local/AestraAudio/include/Plugin/Foo.h"
"")
# Rebuild with the body, so the static sits inside process() as it did in Verb.
file(WRITE "${WORK_DIR}/static_local/AestraAudio/include/Plugin/Foo.h"
"#pragma once
class Foo {
public:
    void process(float* out, unsigned n) {
        static const bool useAVX2 = Aestra::Core::CPUDetection::get().hasAVX2();
        if (useAVX2) { out[0] = 1.0f; }
    }
};
")
expect_guard("function-local static from CPUDetection::get()" "${WORK_DIR}/static_local" "fail")

# Buried mid-body: the shape that defeated a line-scoped check.
file(WRITE "${WORK_DIR}/static_local/AestraAudio/include/Plugin/Foo.h"
"#pragma once
class Foo {
public:
    void process(float* out, unsigned n) {
        float a = out[0];
        float b = a * 2.0f;
        float c = b + 1.0f;
        float d = c * 0.5f;
        static const bool useSSE = Aestra::Core::CPUDetection::get().hasSSE41();
        if (useSSE) { out[0] = d; }
        float e = d * 3.0f;
        float f = e + 2.0f;
        float g = f * 0.25f;
        float h = g - 1.0f;
        out[1] = h;
    }
};
")
expect_guard("CPU query buried mid-body (the AestraVerb shape)" "${WORK_DIR}/static_local" "fail")

# The raw intrinsic, with no CPUDetection mention at all.
file(WRITE "${WORK_DIR}/static_local/AestraAudio/include/Plugin/Foo.h"
"#pragma once
class Foo {
public:
    void process(float* out, unsigned n) {
        unsigned regs[4] = {};
        __cpuid(regs, 1);
        out[0] = static_cast<float>(regs[2]);
    }
};
")
expect_guard("raw __cpuid in process()" "${WORK_DIR}/static_local" "fail")

# getXCR0, which is how the AVX/OSXSAVE check reaches the CPU.
file(WRITE "${WORK_DIR}/static_local/AestraAudio/include/Plugin/Foo.h"
"#pragma once
class Foo {
public:
    void process(float* out, unsigned n) {
        if (getXCR0() & 0x6) { out[0] = 1.0f; }
    }
};
")
expect_guard("getXCR0 in process()" "${WORK_DIR}/static_local" "fail")

# ── Correct shapes: must pass ─────────────────────────────────────────────

# The fix as shipped in AestraVerb: resolved in initialize(), read as a member.
file(WRITE "${WORK_DIR}/static_local/AestraAudio/include/Plugin/Foo.h"
"#pragma once
class Foo {
public:
    void initialize(double sr) {
#ifdef HAS_AVX2
        m_useAVX2 = Aestra::Core::CPUDetection::get().hasAVX2();
#endif
        m_sampleRate = sr;
    }
    void process(float* out, unsigned n) {
        if (m_useAVX2) { out[0] *= 2.0f; }
    }
private:
    bool m_useAVX2 = false;
    double m_sampleRate = 48000.0;
};
")
expect_guard("resolve in initialize(), plain member in process() (the fix)" "${WORK_DIR}/static_local" "pass")

# CPU detection outside process() is the whole point — only the callback matters.
file(WRITE "${WORK_DIR}/static_local/AestraAudio/include/Plugin/Foo.h"
"#pragma once
class Foo {
public:
    void process(float* out, unsigned n) { out[0] *= 2.0f; }
    bool hasAVX2() const { return Aestra::Core::CPUDetection::get().hasAVX2(); }
};
")
expect_guard("CPU query outside process() is allowed" "${WORK_DIR}/static_local" "pass")

# A process() with braces in a string or comment must not desync the brace walk.
file(WRITE "${WORK_DIR}/static_local/AestraAudio/include/Plugin/Foo.h"
"#pragma once
class Foo {
public:
    void process(float* out, unsigned n) {
        // A comment with an unbalanced brace: }
        const char* s = \"{{{\";
        out[0] = s[0] == '{' ? 1.0f : 0.0f;
    }
};
")
expect_guard("stray braces in comments/strings do not desync the walk" "${WORK_DIR}/static_local" "pass")

# A second process() in the same file must be checked too — Verb had three
# separate query sites in one translation unit.
file(WRITE "${WORK_DIR}/static_local/AestraAudio/include/Plugin/Foo.h"
"#pragma once
class Foo {
public:
    void process(float* out, unsigned n) { out[0] *= 2.0f; }
    void processStereo(float* l, float* r, unsigned n) {
        static const bool useAVX2 = Aestra::Core::CPUDetection::get().hasAVX2();
        if (useAVX2) { l[0] = r[0]; }
    }
};
")
expect_guard("second process()-shaped function in the same header" "${WORK_DIR}/static_local" "fail")

# Vendored third-party code is not ours to police. A FRESH tree: the previous
# fixture leaves Foo.h planted, and reusing it would test that residue rather
# than the exemption.
file(REMOVE_RECURSE "${WORK_DIR}/vendored")
file(MAKE_DIRECTORY "${WORK_DIR}/vendored/AestraAudio/include/Plugin")
file(MAKE_DIRECTORY "${WORK_DIR}/vendored/AestraAudio/External/vendored")
# Clean in-tree header, so the only CPU query in this tree is the vendored one.
file(WRITE "${WORK_DIR}/vendored/AestraAudio/include/Plugin/Foo.h"
"#pragma once
class Foo {
public:
    void process(float* out, unsigned n) { out[0] *= 2.0f; }
};
")
file(WRITE "${WORK_DIR}/vendored/AestraAudio/External/vendored/Legacy.h"
"#pragma once
inline void legacyProcess(float* out) {
    static const bool useAVX2 = Aestra::Core::CPUDetection::get().hasAVX2();
    out[0] = useAVX2 ? 1.0f : 0.0f;
}
")
expect_guard("vendored tree under External/ is exempt" "${WORK_DIR}/vendored" "pass")

# An empty tree must pass rather than error.
file(MAKE_DIRECTORY "${WORK_DIR}/empty/AestraAudio/include")
expect_guard("tree with no plugins" "${WORK_DIR}/empty" "pass")

message("")
if(failures GREATER 0)
    message("${failures} of ${checks} checks FAILED.")
    message(FATAL_ERROR "NoRtThreadCpuDetectionGuard self-test failed")
endif()

message("All ${checks} guard self-test checks passed.")