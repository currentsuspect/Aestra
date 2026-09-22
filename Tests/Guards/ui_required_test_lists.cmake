# V8-G2 — the required-UI-test list must be identical in all three places it lives.
#
# FD-19's mechanism ("no UI completion claim is admissible until UI tests can
# fail a build") is carried by a list of UI test names. That list is duplicated
# THREE times, and nothing has ever asserted the copies agree:
#
#   1. AESTRA_REQUIRED_UI_TESTS in Tests/CMakeLists.txt
#        — fails CONFIGURE if a named target does not exist.
#   2. the `--target` list in ci.yml's "Build UI test targets" step
#        — decides what is BUILT.
#   3. the `-R '^(...)$'` selector in ci.yml's "Run UI tests" step
#        — decides what is RUN.
#
# THE ASYMMETRY IS WHY THIS GUARD EXISTS. Omitting a test from (2) fails loudly
# — ctest reports "(Not Run)" and the lane exits non-zero. Omitting it from (3)
# fails SILENTLY: the test builds, the other 23 run, the lane is green, and the
# new test gates nothing. `--no-tests=error` does not help, because tests did
# run — just not that one. That is the exact "test existence is not coverage"
# shape FD-19 exists to prevent, reintroduced by the mechanism meant to prevent
# it. Observed on PR #945, where the test was added to (1) and (3) but not (2).
#
# Required -D args:
#   REPO_ROOT   repository root to scan
#
# Fails with FATAL_ERROR naming each divergence and its direction.

# Script mode (cmake -P) does not inherit the project's cmake_minimum_required,
# so without this every policy is unset: on CMake 3.x IN_LIST (CMP0057) falls
# back to OLD behaviour and if() errors. CMake 4 defaults it NEW, which is why
# this passed locally and failed in CI.
cmake_minimum_required(VERSION 3.22)

if(NOT REPO_ROOT)
    message(FATAL_ERROR "REPO_ROOT must be provided")
endif()

set(cmake_lists "${REPO_ROOT}/Tests/CMakeLists.txt")
set(ci_workflow "${REPO_ROOT}/.github/workflows/ci.yml")

foreach(required_file "${cmake_lists}" "${ci_workflow}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "ui_required_test_lists: missing ${required_file}")
    endif()
endforeach()

file(READ "${cmake_lists}" cmake_text)
file(READ "${ci_workflow}" ci_text)

# --- (1) AESTRA_REQUIRED_UI_TESTS -------------------------------------------
if(NOT cmake_text MATCHES "set\\(AESTRA_REQUIRED_UI_TESTS[ \t\r\n]*([^)]*)\\)")
    message(FATAL_ERROR
        "ui_required_test_lists: could not find set(AESTRA_REQUIRED_UI_TESTS ...) in Tests/CMakeLists.txt.\n"
        "A guard that cannot read its input must fail rather than report agreement.")
endif()
string(REGEX MATCHALL "[A-Za-z0-9_]+" cmake_names "${CMAKE_MATCH_1}")

# --- (2) the --target build list --------------------------------------------
if(NOT ci_text MATCHES "--target[ \t\r\n]+(NUIThemeConsistencyTest[^\n]*(\n[ \t]+[^\n-][^\n]*)*)")
    message(FATAL_ERROR
        "ui_required_test_lists: could not find the '--target' UI test list in ci.yml.\n"
        "A guard that cannot read its input must fail rather than report agreement.")
endif()
string(REGEX MATCHALL "[A-Za-z0-9_]+Test" build_names "${CMAKE_MATCH_1}")

# --- (3) the -R run selector -------------------------------------------------
if(NOT ci_text MATCHES "-R '\\^\\(([^)]*)\\)\\$'")
    message(FATAL_ERROR
        "ui_required_test_lists: could not find the ctest -R UI selector in ci.yml.\n"
        "A guard that cannot read its input must fail rather than report agreement.")
endif()
string(REGEX MATCHALL "[A-Za-z0-9_]+" run_names "${CMAKE_MATCH_1}")

list(LENGTH cmake_names cmake_count)
if(cmake_count EQUAL 0)
    message(FATAL_ERROR "ui_required_test_lists: AESTRA_REQUIRED_UI_TESTS parsed as empty; refusing to pass.")
endif()

list(SORT cmake_names)
list(SORT build_names)
list(SORT run_names)

set(problems "")

# Reported per direction, because the directions mean different things: a name
# missing from CI is lost coverage, while a name in CI but not in CMake is a
# lane that will fail on a target that is never configured.
foreach(name IN LISTS cmake_names)
    if(NOT name IN_LIST build_names)
        string(APPEND problems
            "  - ${name} is in AESTRA_REQUIRED_UI_TESTS but NOT in ci.yml's --target list: it is never BUILT, "
            "so the lane reports it as \"(Not Run)\".\n")
    endif()
    if(NOT name IN_LIST run_names)
        string(APPEND problems
            "  - ${name} is in AESTRA_REQUIRED_UI_TESTS but NOT in ci.yml's -R selector: it builds and is "
            "SILENTLY never run, so it gates nothing. This is the failure this guard exists for.\n")
    endif()
endforeach()

foreach(name IN LISTS build_names)
    if(NOT name IN_LIST cmake_names)
        string(APPEND problems
            "  - ${name} is in ci.yml's --target list but NOT in AESTRA_REQUIRED_UI_TESTS: CI will try to build "
            "a target configure does not guarantee exists.\n")
    endif()
endforeach()

foreach(name IN LISTS run_names)
    if(NOT name IN_LIST cmake_names)
        string(APPEND problems
            "  - ${name} is in ci.yml's -R selector but NOT in AESTRA_REQUIRED_UI_TESTS.\n")
    endif()
endforeach()

if(NOT problems STREQUAL "")
    message(FATAL_ERROR
        "The required-UI-test list has diverged between its three copies (V8-G2 / FD-19):\n"
        "${problems}"
        "Fix by making all three carry the same names:\n"
        "  1. set(AESTRA_REQUIRED_UI_TESTS ...) in Tests/CMakeLists.txt\n"
        "  2. the --target list in ci.yml's \"Build UI test targets\" step\n"
        "  3. the -R '^(...)$' selector in ci.yml's \"Run UI tests\" step\n")
endif()

list(LENGTH cmake_names final_count)
message(STATUS "ui_required_test_lists: OK — ${final_count} UI tests, consistent across all three lists.")
