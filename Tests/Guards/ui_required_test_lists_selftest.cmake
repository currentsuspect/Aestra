# Self-test for ui_required_test_lists.cmake — proves the guard rejects each
# way the three copies can diverge, in both directions.
#
# A guard that only ever passes is indistinguishable from no guard. Each fixture
# starts from a baseline where all three lists agree and applies exactly ONE
# mutation, so every result isolates a single guard behaviour.
#
# Required -D args:
#   GUARD_SCRIPT  absolute path to ui_required_test_lists.cmake
#   WORK_DIR      scratch directory (recreated per fixture)

if(NOT GUARD_SCRIPT OR NOT WORK_DIR)
    message(FATAL_ERROR "GUARD_SCRIPT and WORK_DIR must be provided")
endif()

set(failures 0)

# Writes a fixture tree. Each list is passed as a ready-made string so a fixture
# can express a divergence that a shared list could not.
function(write_tree cmake_list build_list run_list)
    file(REMOVE_RECURSE "${WORK_DIR}/tree")
    file(MAKE_DIRECTORY "${WORK_DIR}/tree/Tests")
    file(MAKE_DIRECTORY "${WORK_DIR}/tree/.github/workflows")

    file(WRITE "${WORK_DIR}/tree/Tests/CMakeLists.txt"
"if(AESTRA_ENABLE_UI)
    set(AESTRA_REQUIRED_UI_TESTS
${cmake_list}
    )
endif()
")

    file(WRITE "${WORK_DIR}/tree/.github/workflows/ci.yml"
"      - name: Build UI test targets
        run: >-
          cmake --build build-app --config Release
          --target ${build_list}
          --parallel 2

      - name: Run UI tests
        run: ctest --test-dir build-app -C Release -R '^(${run_list})$' --no-tests=error --output-on-failure
")
endfunction()

function(run_fixture name expect)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -DREPO_ROOT=${WORK_DIR}/tree -P "${GUARD_SCRIPT}"
        RESULT_VARIABLE rc
        OUTPUT_QUIET ERROR_QUIET)
    if(expect STREQUAL "FAIL" AND rc EQUAL 0)
        message(SEND_ERROR "fixture '${name}': guard PASSED but must FAIL")
        set(failures 1 PARENT_SCOPE)
    elseif(expect STREQUAL "PASS" AND NOT rc EQUAL 0)
        message(SEND_ERROR "fixture '${name}': guard FAILED but must PASS")
        set(failures 1 PARENT_SCOPE)
    else()
        message(STATUS "fixture '${name}': ok (${expect})")
    endif()
endfunction()

set(BASE_CMAKE "        NUIThemeConsistencyTest\n        AlphaTest\n        BetaTest")
set(BASE_BUILD "NUIThemeConsistencyTest AlphaTest BetaTest")
set(BASE_RUN "NUIThemeConsistencyTest|AlphaTest|BetaTest")

# --- the agreeing baseline must pass, or every FAIL below proves nothing -----
write_tree("${BASE_CMAKE}" "${BASE_BUILD}" "${BASE_RUN}")
run_fixture("all three lists agree" PASS)

# --- the loud direction: named but never built ------------------------------
write_tree("${BASE_CMAKE}" "NUIThemeConsistencyTest AlphaTest" "${BASE_RUN}")
run_fixture("missing from --target (CI reports it as Not Run)" FAIL)

# --- THE SILENT DIRECTION, and the reason this guard exists ------------------
# The test builds, the others run, the lane is green, and it gates nothing.
write_tree("${BASE_CMAKE}" "${BASE_BUILD}" "NUIThemeConsistencyTest|AlphaTest")
run_fixture("missing from -R selector (builds, silently never runs)" FAIL)

# --- the reverse direction: CI naming something configure does not guarantee -
write_tree("${BASE_CMAKE}" "${BASE_BUILD} GhostTest" "${BASE_RUN}")
run_fixture("--target names a test absent from AESTRA_REQUIRED_UI_TESTS" FAIL)

write_tree("${BASE_CMAKE}" "${BASE_BUILD}" "${BASE_RUN}|GhostTest")
run_fixture("-R names a test absent from AESTRA_REQUIRED_UI_TESTS" FAIL)

# --- a guard that cannot read its input must not report agreement ------------
write_tree("${BASE_CMAKE}" "${BASE_BUILD}" "${BASE_RUN}")
file(WRITE "${WORK_DIR}/tree/Tests/CMakeLists.txt" "# the list was renamed or removed\n")
run_fixture("unreadable AESTRA_REQUIRED_UI_TESTS" FAIL)

write_tree("${BASE_CMAKE}" "${BASE_BUILD}" "${BASE_RUN}")
file(WRITE "${WORK_DIR}/tree/.github/workflows/ci.yml" "# the UI steps were restructured\n")
run_fixture("unreadable ci.yml UI steps" FAIL)

# --- an empty list is not agreement -----------------------------------------
write_tree("" "" "")
run_fixture("all three empty" FAIL)

if(failures)
    message(FATAL_ERROR "ui_required_test_lists self-test failed")
endif()
message(STATUS "ui_required_test_lists self-test passed")
