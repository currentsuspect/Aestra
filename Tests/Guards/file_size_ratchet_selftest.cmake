# Self-test for FileSizeRatchetGuard.
#
# A ratchet that cannot fail reports clean forever, so this builds fixture trees
# and asserts both directions:
#
#   MUST PASS: the exact baseline; an unpinned file at the cap; an oversized file
#              under External/; an oversized non-source file.
#   MUST FAIL: a pinned file that grew; one that shrank but is still over the cap
#              (stale row); one that fell to the cap; a pinned file that is gone;
#              an unpinned file one line over the cap; a malformed baseline row.
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

set(kCap 1500)
set(failures 0)
set(checks 0)

# Writes a file of exactly `lines` newline-terminated lines.
function(write_lines path lines)
    get_filename_component(dir "${path}" DIRECTORY)
    file(MAKE_DIRECTORY "${dir}")
    string(REPEAT "int x;\n" ${lines} body)
    file(WRITE "${path}" "${body}")
endfunction()

# A fixture root with one pinned hub at 2000 lines, one ordinary file, and the
# matching baseline. `hub_lines` is what the hub actually has; the baseline
# always pins it at 2000.
function(make_fixture root hub_lines)
    file(MAKE_DIRECTORY "${root}/Tests/Guards")
    if(hub_lines GREATER 0)
        write_lines("${root}/Source/Core/Hub.cpp" ${hub_lines})
    endif()
    write_lines("${root}/AestraUI/Widgets/Small.cpp" 40)
    file(WRITE "${root}/Tests/Guards/file_size_baseline.txt"
        "# lines path\n2000 Source/Core/Hub.cpp\n")
endfunction()

# For a "fail" case, `needle` must appear in the guard's output, so a fixture
# that fails for the wrong reason (a typo in the fixture, a crash) is caught.
function(expect_guard label root expected)
    set(needle "${ARGV3}")
    math(EXPR n "${checks} + 1")
    set(checks ${n} PARENT_SCOPE)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DREPO_ROOT=${root} -P ${GUARD_SCRIPT}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)
    if(expected STREQUAL "fail")
        if(rc EQUAL 0)
            math(EXPR n "${failures} + 1")
            set(failures ${n} PARENT_SCOPE)
            message("FAIL  ${label}  -> passed, but must fail")
        elseif(needle AND NOT "${out}${err}" MATCHES "${needle}")
            math(EXPR n "${failures} + 1")
            set(failures ${n} PARENT_SCOPE)
            message("FAIL  ${label}  -> failed, but not with '${needle}'
${out}${err}")
        else()
            message("PASS  ${label}  -> fail")
        endif()
    else()
        if(NOT rc EQUAL 0)
            math(EXPR n "${failures} + 1")
            set(failures ${n} PARENT_SCOPE)
            message("FAIL  ${label}  -> ${rc}\n${out}${err}")
        else()
            message("PASS  ${label}  -> pass")
        endif()
    endif()
endfunction()

# ── Must pass ────────────────────────────────────────────────────────────────
make_fixture("${WORK_DIR}/exact" 2000)
expect_guard("pinned file at exactly its baseline" "${WORK_DIR}/exact" pass)

make_fixture("${WORK_DIR}/at_cap" 2000)
write_lines("${WORK_DIR}/at_cap/AestraAudio/src/AtCap.cpp" ${kCap})
expect_guard("unpinned file exactly at the cap" "${WORK_DIR}/at_cap" pass)

make_fixture("${WORK_DIR}/external" 2000)
write_lines("${WORK_DIR}/external/AestraAudio/External/vendor/huge.cpp" 9000)
expect_guard("oversized file under External/ is vendored, not ours" "${WORK_DIR}/external" pass)

make_fixture("${WORK_DIR}/non_source" 2000)
write_lines("${WORK_DIR}/non_source/Source/Core/notes.txt" 9000)
expect_guard("oversized non-source file is out of scope" "${WORK_DIR}/non_source" pass)

# ── Must fail ────────────────────────────────────────────────────────────────
make_fixture("${WORK_DIR}/grew" 2001)
expect_guard("pinned file grew by one line" "${WORK_DIR}/grew" fail "GREW to 2001")

make_fixture("${WORK_DIR}/stale" 1999)
expect_guard("pinned file shrank, row not lowered (stale)" "${WORK_DIR}/stale" fail "baseline is stale, lower its row to 1999")

make_fixture("${WORK_DIR}/under_cap" ${kCap})
expect_guard("pinned file fell to the cap, row not deleted" "${WORK_DIR}/under_cap" fail "Delete its row")

make_fixture("${WORK_DIR}/gone" 0)
expect_guard("pinned file no longer exists" "${WORK_DIR}/gone" fail "no longer exists")

make_fixture("${WORK_DIR}/new_hub" 2000)
math(EXPR over "${kCap} + 1")
write_lines("${WORK_DIR}/new_hub/AestraUI/Widgets/NewHub.cpp" ${over})
expect_guard("unpinned file one line over the cap" "${WORK_DIR}/new_hub" fail "NewHub.cpp: 1501 lines, over the 1500-line cap")

make_fixture("${WORK_DIR}/header" 2000)
write_lines("${WORK_DIR}/header/AestraAudio/include/Plugin/Big.h" ${over})
expect_guard("unpinned header one line over the cap" "${WORK_DIR}/header" fail "Big.h: 1501 lines")

make_fixture("${WORK_DIR}/malformed" 2000)
file(APPEND "${WORK_DIR}/malformed/Tests/Guards/file_size_baseline.txt" "lots Source/Core/Other.cpp\n")
expect_guard("malformed baseline row" "${WORK_DIR}/malformed" fail "malformed")

if(failures GREATER 0)
    message(FATAL_ERROR "FileSizeRatchetGuardSelfTest: ${failures} of ${checks} check(s) failed")
endif()
message("All ${checks} checks passed.")
