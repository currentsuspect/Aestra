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
# WHAT THIS GUARD STILL DID NOT COVER, until 2026-10-02. It checked that those
# three copies agree with EACH OTHER. It never checked them against what the tree
# actually contains, so AESTRA_REQUIRED_UI_TESTS is a hand-maintained SUBSET that
# can silently fall behind. Measured at 506460d8: three UI-only tests built, passed
# and gated nothing --
#
#   BPMEditorPaintTest   MixerDropdownKeyboardTest   TransportClockFormatTest
#
# because they were declared inside if(AESTRA_ENABLE_UI) and never added to the
# list. They appear zero times in ci.yml. `--no-tests=error` cannot see it: 31 tests
# DID run, just not those three. Same shape, one level up -- "test existence is not
# coverage". Check (4) below is what closes it: every add_test() lexically inside an
# AESTRA_ENABLE_UI block must be in AESTRA_REQUIRED_UI_TESTS.
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

# --- (4) reality: every UI-only test must be REQUIRED -----------------------
#
# Lists (1), (2) and (3) can all agree and all be wrong together, because none of
# them is derived from the tree. This check makes the required set positive with
# respect to reality, not merely self-consistent.
#
# The parse walks if/endif nesting with an explicit stack, because the question is
# which add_test() calls sit LEXICALLY inside a UI block, and a flat regex cannot
# answer that. Each line's stack is recomputed from scratch rather than kept as a
# running counter: a counter that opens two blocks on one line and closes one has
# to get the interleaving exactly right, and the recompute cannot drift.
#
# The anchor [^A-Za-z0-9_] is what stops `endif(` and `elseif(` from counting as
# block opens -- in both, the `if` is preceded by a letter.
#
# KNOWN LIMIT, stated rather than hidden: a condition is read up to its first `)`,
# so `if(<something>) AND (AESTRA_ENABLE_UI ...` would be misread. No such
# condition exists in this tree, and the empty-derivation check below turns a
# future one into a loud failure rather than a silent pass.
set(ui_only_names "")
set(ui_stack "")
set(_scan "${cmake_text}")
while(NOT _scan STREQUAL "")
    string(FIND "${_scan}" "\n" _nl)
    if(_nl EQUAL -1)
        set(_line "${_scan}")
        set(_scan "")
    else()
        string(SUBSTRING "${_scan}" 0 ${_nl} _line)
        math(EXPR _next "${_nl} + 1")
        string(SUBSTRING "${_scan}" ${_next} -1 _scan)
    endif()

    # Push one entry per block opened on this line.
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])if[ \t]*[(][^)]*[)]" _if_hits "${_line}")
    foreach(_hit IN LISTS _if_hits)
        string(REGEX MATCH "if[ \t]*[(]([^)]*)[)]" _one "${_hit}")
        set(_cond "${CMAKE_MATCH_1}")
        # Pushed as its final value rather than appended-then-patched: CMake has no
        # list(SET), and deciding at push time removes the index arithmetic that a
        # patch would need.
        if(_cond MATCHES "AESTRA_ENABLE_UI")
            list(APPEND ui_stack "1")
        else()
            list(APPEND ui_stack "0")
        endif()
    endforeach()

    set(ui_open 0)
    foreach(_slot IN LISTS ui_stack)
        if(_slot STREQUAL "1")
            set(ui_open 1)
        endif()
    endforeach()

    if(ui_open)
        string(REGEX MATCH "add_test[ \t]*[(][ \t]*NAME[ \t]+([A-Za-z0-9_]+)" _m "${_line}")
        if(_m)
            list(APPEND ui_only_names "${CMAKE_MATCH_1}")
        endif()
    endif()

    # Pop one entry per block closed on this line. Counting them independently of
    # the opens matters: an `elseif()` opens nothing and closes nothing, but a
    # single line can both open and close.
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])endif[ \t]*[(]" _endif_hits "${_line}")
    foreach(_hit IN LISTS _endif_hits)
        if(NOT ui_stack STREQUAL "")
            list(POP_BACK ui_stack)
        endif()
    endforeach()
endwhile()

if(NOT ui_only_names)
    # A guard that cannot establish reality must not report agreement. Zero derived
    # UI tests on a real tree means the tree changed shape, not that there is nothing
    # to require.
    message(FATAL_ERROR
        "ui_required_test_lists: parsed ZERO add_test() calls inside an AESTRA_ENABLE_UI block.\n"
        "The guard cannot establish reality, so it must not report agreement. Either the tree no "
        "longer declares UI tests inside an AESTRA_ENABLE_UI block, or this guard's parse no longer "
        "understands it. Investigate rather than re-running.")
endif()

foreach(name IN LISTS ui_only_names)
    if(NOT name IN_LIST cmake_names)
        string(APPEND problems
            "  - ${name} is declared inside if(AESTRA_ENABLE_UI) but is NOT in AESTRA_REQUIRED_UI_TESTS: "
            "it builds, it passes, and it gates NOTHING.\n")
    endif()
endforeach()

if(NOT problems STREQUAL "")
    message(FATAL_ERROR
        "The required-UI-test list has diverged from the tree (V8-G2 / FD-19):\n"
        "${problems}"
        "Fix by making all three carry the same names:\n"
        "  1. set(AESTRA_REQUIRED_UI_TESTS ...) in Tests/CMakeLists.txt\n"
        "  2. the --target list in ci.yml's \"Build UI test targets\" step\n"
        "  3. the -R '^(...)$' selector in ci.yml's \"Run UI tests\" step\n")
endif()

list(LENGTH cmake_names final_count)
list(LENGTH ui_only_names final_ui_only)
message(STATUS
    "ui_required_test_lists: OK — ${final_count} UI tests, consistent across all three lists; "
    "all ${final_ui_only} tests declared inside if(AESTRA_ENABLE_UI) are among them.")
