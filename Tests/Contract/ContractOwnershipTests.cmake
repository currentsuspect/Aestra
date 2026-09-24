# Ownership contract tests (label contract:ownership).
#
# Each test encodes one invariant of the Aestra ownership contract. The contract
# document lives in Aestra-Internals ("Ownership & Reference Contract"); the
# F-numbers below refer to its evidence ledger.
#
# Rules (contract §7.2):
#   * Every test carries contract:ownership and one guards:F<n> label per finding.
#   * WILL_FAIL is set ONLY while a guarded finding is open, with a comment naming
#     the finding and the pass expected to close it. The fixing pass removes it.
#   * Never add WILL_FAIL to a passing test, never re-add it, never delete a test.
#   * Setup failures abort (ctest reports "Exception"; WILL_FAIL cannot mask it).
#
# Included from Tests/CMakeLists.txt. Keep all registration here so rebases touch
# one file.

set(_contract_dir ${CMAKE_CURRENT_SOURCE_DIR}/Contract)

set(_contract_audio_includes
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/DSP
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Playback
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/IO
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Drivers
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Commands
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Headless
    ${CMAKE_SOURCE_DIR}/AestraCore/include
    ${CMAKE_SOURCE_DIR}/Source
    ${CMAKE_SOURCE_DIR}/Tests
)

# aestra_contract_test(<test-name> <target> <mode> GUARDS <F..> [KNOWN_VIOLATION])
function(aestra_contract_test test_name target mode)
    cmake_parse_arguments(ARG "KNOWN_VIOLATION" "" "GUARDS" ${ARGN})
    add_test(NAME ${test_name} COMMAND ${target} ${mode})
    set(_labels "contract:ownership")
    foreach(_f IN LISTS ARG_GUARDS)
        list(APPEND _labels "guards:${_f}")
    endforeach()
    set_tests_properties(${test_name} PROPERTIES LABELS "${_labels}" TIMEOUT 120)
    if(ARG_KNOWN_VIOLATION)
        set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Clip model: split referent, exact undo, MIDI windows (via the real scheduler)
add_executable(ContractClipModelTest ${_contract_dir}/ContractClipModelTest.cpp)
target_link_libraries(ContractClipModelTest PRIVATE AestraAudio)
target_include_directories(ContractClipModelTest PRIVATE ${_contract_audio_includes})

# guards: F8 — isPatternUsed misses the split half's pattern (stale sourceId). Fix: Pass 2.
aestra_contract_test(ContractSplitReferentTest ContractClipModelTest split-referent GUARDS F8 KNOWN_VIOLATION)
# guards: F6,F7 — split undo recomputes durationSeconds and leaks the cloned pattern. Fix: Pass 3.
aestra_contract_test(ContractSplitUndoExactTest ContractClipModelTest split-undo-exact GUARDS F6 F7 KNOWN_VIOLATION)
# guards: F24 — the scheduler treats sourceOffset as a filter, not an origin shift. Fix: Pass 3.
aestra_contract_test(ContractMidiOffsetWindowTest ContractClipModelTest midi-offset GUARDS F24 KNOWN_VIOLATION)
# guards: F10 — splitting an offset MIDI clip changes what plays. Fix: Pass 3.
aestra_contract_test(ContractMidiSplitOffsetTest ContractClipModelTest midi-split-offset GUARDS F10 KNOWN_VIOLATION)
# guards: F11 — left-trim slides MIDI notes instead of hiding them. Fix: Pass 3.
aestra_contract_test(ContractMidiLeftTrimTest ContractClipModelTest midi-left-trim GUARDS F11 KNOWN_VIOLATION)
aestra_contract_test(ContractIdenticalNotesCharacterizationTest ContractClipModelTest identical-notes GUARDS F4)

# ---------------------------------------------------------------------------
# Units, sampler audio ownership, persistence
add_executable(ContractUnitSourceTest
    ${_contract_dir}/ContractUnitSourceTest.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
)
target_link_libraries(ContractUnitSourceTest PRIVATE AestraAudio)
target_include_directories(ContractUnitSourceTest PRIVATE ${_contract_audio_includes})

# guards: F12 — duplicateUnit clones the home pattern with the old unitId. Fix: Pass 4.
aestra_contract_test(ContractDuplicateUnitNoNotesTest ContractUnitSourceTest duplicate-no-notes GUARDS F12 KNOWN_VIOLATION)
# guards: F23 — sampler audio is a private decode, not a SourceManager Source. Fix: Pass 4.
aestra_contract_test(ContractSamplerSharedSourceTest ContractUnitSourceTest sampler-shared-source GUARDS F23 KNOWN_VIOLATION)
# guards: F13 — sampler reverse is in-memory only; lost on reload. Fix: Pass 4.
aestra_contract_test(ContractSamplerReversePersistTest ContractUnitSourceTest sampler-reverse-persist GUARDS F13 KNOWN_VIOLATION)
# guards: F13 — sampler normalize is in-memory only; lost on reload. Fix: Pass 4.
aestra_contract_test(ContractSamplerNormalizePersistTest ContractUnitSourceTest sampler-normalize-persist GUARDS F13 KNOWN_VIOLATION)
# guards: F20 — source ids are clamped to 32 bits on load. Fix: Pass 2.
aestra_contract_test(ContractSourceIdWidthTest ContractUnitSourceTest source-id-width GUARDS F20 KNOWN_VIOLATION)
# guards: F25 — audio clip beat-domain sourceOffset is re-derived on load in another domain. Fix: Pass 3 (owner to confirm).
aestra_contract_test(ContractRoundTripStableTest ContractUnitSourceTest roundtrip-stable GUARDS F25 KNOWN_VIOLATION)
# guards: F26 — type defaults override saved mono/glide on load. Fix: Pass 4.
aestra_contract_test(ContractSamplerParamsPersistTest ContractUnitSourceTest sampler-params-persist GUARDS F26 KNOWN_VIOLATION)

# ---------------------------------------------------------------------------
# Structural proxies for app-shell rules no harness can drive yet
add_executable(ContractStructuralCheck ${_contract_dir}/ContractStructuralCheck.cpp)
target_compile_definitions(ContractStructuralCheck PRIVATE AESTRA_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_include_directories(ContractStructuralCheck PRIVATE ${CMAKE_SOURCE_DIR}/Tests)

# guards: F9 — ClipInstance::sourceId is read at runtime. Fix: Pass 2.
aestra_contract_test(ContractNoClipSourceIdReadsTest ContractStructuralCheck sourceid-reads GUARDS F9 KNOWN_VIOLATION)
# guards: F16 — Arsenal context changes rewind the scheduler. Fix: Pass 3.
aestra_contract_test(ContractSelectionNoPlaybackRewindTest ContractStructuralCheck selection-playback GUARDS F16 KNOWN_VIOLATION)
# guards: F17 — ad-hoc unit resolution and find-pattern-by-name. Fix: Pass 3.
aestra_contract_test(ContractSingleContextResolutionTest ContractStructuralCheck context-resolution GUARDS F17 KNOWN_VIOLATION)
# guards: F14 — sample editor draws a fake sine on decode failure. Fix: Pass 4.
aestra_contract_test(ContractNoFabricatedWaveformTest ContractStructuralCheck fake-waveform GUARDS F14 KNOWN_VIOLATION)
# guards: F15,F22 — UI mutates unit lifecycle/identity directly. Fix: Pass 4.
aestra_contract_test(ContractUnitLifecycleViaCommandsTest ContractStructuralCheck unit-lifecycle GUARDS F15 F22 KNOWN_VIOLATION)
# guards: F21 — sampler editor calls plugin setters directly. Fix: Pass 4.
aestra_contract_test(ContractSamplerEditsViaCommandsTest ContractStructuralCheck sampler-edits GUARDS F21 KNOWN_VIOLATION)

# ---------------------------------------------------------------------------
# Piano Roll panel (UI lane; same harness as PianoRollPanelPersistenceTest)
#
# One target per test, named like the test: the UI lane's three required-test
# lists (AESTRA_REQUIRED_UI_TESTS, ci.yml --target, ci.yml -R) assume target ==
# test name, and UIRequiredTestListsGuard enforces that they agree.
if(AESTRA_ENABLE_UI)
    function(aestra_contract_piano_roll_test test_name mode)
        aestra_require_ui_targets(${test_name} AestraUI_Core AestraUI_Platform)
        add_executable(${test_name}
            ${_contract_dir}/ContractPianoRollPanelTest.cpp
            ${CMAKE_SOURCE_DIR}/Source/Panels/WindowPanel.cpp
            ${CMAKE_SOURCE_DIR}/Source/Panels/PianoRollPanel.cpp
            ${CMAKE_SOURCE_DIR}/Source/Components/TimelineMinimapBar.cpp
            ${CMAKE_SOURCE_DIR}/Source/Components/TimelineMinimapRenderer.cpp
            ${CMAKE_SOURCE_DIR}/Source/Components/TimelineSummaryCache.cpp
        )
        target_link_libraries(${test_name} PRIVATE AestraUI_Core AestraUI_Platform AestraAudio)
        target_include_directories(${test_name} PRIVATE
            ${_contract_audio_includes}
            ${CMAKE_SOURCE_DIR}/AestraUI
            ${CMAKE_SOURCE_DIR}/AestraUI/Core
            ${CMAKE_SOURCE_DIR}/AestraUI/Widgets
            ${CMAKE_SOURCE_DIR}/Source/Core
            ${CMAKE_SOURCE_DIR}/Source/Panels
            ${CMAKE_SOURCE_DIR}/Source/Components
        )
        aestra_contract_test(${test_name} ${test_name} ${mode} ${ARGN})
    endfunction()

    # guards: F3 — one gesture produces several history entries. Fix: Pass 2.
    aestra_contract_piano_roll_test(ContractPianoRollOneEntryPerGestureTest gesture GUARDS F3 KNOWN_VIOLATION)
    # guards: F2 — Piano Roll local undo writes into another pattern. Fix: Pass 2.
    aestra_contract_piano_roll_test(ContractPianoRollUndoStaysInPatternTest cross-pattern GUARDS F2 KNOWN_VIOLATION)
    # guards: F1 — Piano Roll swallows Ctrl+Z. Fix: Pass 2.
    aestra_contract_piano_roll_test(ContractPianoRollReachesGlobalUndoTest global-undo GUARDS F1 KNOWN_VIOLATION)
    # guards: F5 — paste keeps the copied unitId. Fix: Pass 3.
    aestra_contract_piano_roll_test(ContractPianoRollPasteTargetsEditingUnitTest paste-target GUARDS F5 KNOWN_VIOLATION)
endif()
