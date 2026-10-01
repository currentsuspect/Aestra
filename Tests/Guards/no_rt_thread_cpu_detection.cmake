# NoRtThreadCpuDetectionGuard — CPU feature detection off the audio thread.
#
# CPUDetection::get() is a function-local static singleton (AestraCore/include/
# CPUDetection.h). Reaching it from an audio callback means taking its
# thread-safe-initialisation guard AND running __cpuid / __cpuidex / xgetbv in
# its constructor. CPUID is a VM exit on a virtualised host: microseconds to
# tens of microseconds of unbounded work, on the callback, mid-render. It is
# also exactly what -Wfunction-effects refuses, which is why AestraVerb could
# not carry AESTRA_RT_NONBLOCKING on process() until this was fixed (#1009).
#
# It reads as a cheap pattern -- one static local bool, cached after the first
# call -- and the header's own comment ("cached in static booleans for
# zero-overhead queries") is true of every query except the first. That is the
# whole trap: the cost is once per instance, so it has plausibly never been
# heard, and it will not show up in a profile of a steady-state render.
#
# Correct shape: resolve the flags in initialize(), which runs on the
# preparing thread, and read plain members from process(). AestraVerb is the
# worked example (resolveSimdCapabilities()).
#
# Contract limits (deliberate): this is a textual tripwire, not a static
# analyzer. It cannot see a CPUID reached through a renamed wrapper or a
# function pointer. The compile-time half of this contract is
# scripts/ci/check-rt-effects.sh, which is what proves the absence of the
# allocation, the lock and the syscall; this guard covers the case where a
# plugin is not yet annotated, and therefore not yet covered by that lane.
# A determined hidden indirection is caught by review.
#
# Required -D args:
#   REPO_ROOT  absolute path to the repository root

if(NOT REPO_ROOT)
    message(FATAL_ERROR "REPO_ROOT must be provided")
endif()

set(scan_dirs
    AestraAudio
    AestraCore
    Source
)

# Any CPU feature query. CPUDetection is the only provider in the tree today;
# the tokens are listed rather than the class so a future provider is caught by
# adding one line here, which is a reviewable edit, instead of by noticing a
# silent regression.
set(cpu_tokens
    "CPUDetection"
    "__cpuid"
    "__cpuidex"
    "xgetbv"
    "getXCR0"
)

set(violations "")
foreach(dir ${scan_dirs})
    if(NOT EXISTS "${REPO_ROOT}/${dir}")
        continue()
    endif()
    file(GLOB_RECURSE files
        "${REPO_ROOT}/${dir}/*.h"
        "${REPO_ROOT}/${dir}/*.hpp"
        "${REPO_ROOT}/${dir}/*.cpp")
    foreach(f ${files})
        if(f MATCHES "/External/")
            continue()
        endif()

        file(READ "${f}" contents)
        string(REPLACE "${REPO_ROOT}/" "" rel "${f}")

        # Find every process( / processBlock( definition and check only its body.
        # Brace counting, not a regex to end-of-line: process() bodies here run
        # to hundreds of lines, and a line-scoped check would miss a CPU query
        # buried in the middle of one -- which is where all three Verb sites
        # were.
        # Any void-returning member whose name starts with "process", so
        # processStereo/processBlock/processMono are covered too. A guard that
        # only matches the exact name "process" would have passed a header whose
        # second audio entry point ran CPUID — and the self-test caught exactly
        # that. Prefix-anchored, case-sensitive, whitespace tolerant.
        string(REGEX MATCHALL "void[ \t\n]+process[A-Za-z0-9_]*[ \t\n]*\\(" open_spans "${contents}")
        if(NOT open_spans)
            continue()
        endif()

        list(LENGTH open_spans n_spans)
        math(EXPR last "${n_spans} - 1")
        foreach(i RANGE 0 ${last})
            list(GET open_spans ${i} _match)

            # Byte offset just past the '(' this match ends on.
            string(FIND "${contents}" "${_match}" _at)
            math(EXPR _body_start "${_at} + 0")
            string(LENGTH "${_match}" _mlen)
            math(EXPR _paren "${_at} + ${_mlen} - 1")

            # Walk forward to the matching close brace of the body.
            set(_depth 0)
            set(_seen_open FALSE)
            set(_i ${_paren})
            set(_len 0)
            string(LENGTH "${contents}" _len)
            set(_body "")
            while(_i LESS _len)
                string(SUBSTRING "${contents}" ${_i} 1 _ch)
                if(_ch STREQUAL "{")
                    math(EXPR _depth "${_depth} + 1")
                    set(_seen_open TRUE)
                elseif(_ch STREQUAL "}")
                    math(EXPR _depth "${_depth} - 1")
                    if(_seen_open AND _depth EQUAL 0)
                        break()
                    endif()
                endif()
                if(_seen_open)
                    string(APPEND _body "${_ch}")
                endif()
                math(EXPR _i "${_i} + 1")
            endwhile()

            # Line number of the '(' for a readable diagnostic.
            string(SUBSTRING "${contents}" 0 ${_paren} _head)
            string(REGEX MATCHALL "\n" _newlines "${_head}")
            list(LENGTH _newlines _line)
            math(EXPR _line "${_line} + 1")

            foreach(tok ${cpu_tokens})
                if(_body MATCHES "${tok}")
                    list(APPEND violations
                        "${rel}:${_line} — process() body references '${tok}'")
                endif()
            endforeach()
        endforeach()
    endforeach()
endforeach()

if(violations)
    set(dashes "----------------------------------------------------------------")
    set(joined "")
    foreach(v ${violations})
        string(APPEND joined "  ${v}\n")
    endforeach()
    message("FAIL: CPU feature detection reached from an audio-thread path.")
    message("")
    message("${joined}")
    message("Each site above runs CPUID (a VM exit on a virtualised host) inside")
    message("a callback, behind a thread-safe-static guard. Resolve the flags in")
    message("initialize(), which runs on the preparing thread, and read plain")
    message("members from process(). AestraVerb::resolveSimdCapabilities() is the")
    message("worked example; issue #1009 is the original report.")
    message("")
    message("${dashes}")
    message(FATAL_ERROR
        "CPU feature detection reached from an audio-thread path.\n${joined}")
endif()

message(STATUS "NoRtThreadCpuDetectionGuard: clean")