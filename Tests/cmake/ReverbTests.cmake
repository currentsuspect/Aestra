# =============================================================================
# Reverb Tests — AestraVerb regressions, probes and measurement labs.
# =============================================================================
#
# Split out of Tests/CMakeLists.txt so parallel branches stop colliding (#635),
# following the pattern #614 established for Commands tests. Every new target in
# this area used to be appended at the same line, so two independent PRs adding
# unrelated tests conflicted on it — and because develop dismisses stale
# approvals on ANY new commit, each trivial "keep both blocks" resolution cost a
# full review cycle.
#
# WHEN ADDING A TEST: append it at the END of this file. This fragment is
# append-only, so branches adding to different fragments never touch the same
# lines. Resist inserting next to a target your test merely resembles — that
# recreates the shared insertion point this split removed.
# =============================================================================

# Reverb SIMD Benchmark
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbBenchmark.cpp")
    add_executable(AestraReverbBenchmark AestraAudio/ReverbBenchmark.cpp)
    target_link_libraries(AestraReverbBenchmark PRIVATE AestraAudio)
    target_include_directories(AestraReverbBenchmark PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbSIMDParityTest.cpp")
    add_executable(ReverbSIMDParityTest AestraAudio/ReverbSIMDParityTest.cpp)
    target_link_libraries(ReverbSIMDParityTest PRIVATE AestraAudio)
    target_include_directories(ReverbSIMDParityTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    # The test TU gates its real body on AESTRA_ENABLE_RUNTIME_TESTS, but this
    # check is pure computation (SSE vs scalar FDN kernels — no devices, no
    # hardware I/O), and the test is registered on the always-on tier. Without
    # this definition the body compiles out and ctest reports a "pass" that is
    # actually a silent skip on every SSE machine.
    target_compile_definitions(ReverbSIMDParityTest PRIVATE AESTRA_ENABLE_RUNTIME_TESTS=1)
    add_test(NAME ReverbSIMDParityTest COMMAND ReverbSIMDParityTest)
    set_tests_properties(ReverbSIMDParityTest PROPERTIES LABELS "audio;dsp;reverb")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbSafetyRegressionTest.cpp")
    add_executable(ReverbSafetyRegressionTest AestraAudio/ReverbSafetyRegressionTest.cpp)
    target_link_libraries(ReverbSafetyRegressionTest PRIVATE AestraAudio)
    target_include_directories(ReverbSafetyRegressionTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME ReverbSafetyRegressionTest COMMAND ReverbSafetyRegressionTest)
    set_tests_properties(ReverbSafetyRegressionTest PROPERTIES LABELS "audio;dsp;reverb;safety")
endif()

# Reverb Consistency Probe — hunts for inconsistency/offness (bypass parity,
# NaN rejection, silence tail, state roundtrip are hard invariants; Low Cut
# mapping, freeze stability, sample-rate consistency, mode-switch clicks, and
# stereo correlation are printed diagnostics).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbConsistencyProbe.cpp")
    add_executable(ReverbConsistencyProbe AestraAudio/ReverbConsistencyProbe.cpp)
    target_link_libraries(ReverbConsistencyProbe PRIVATE AestraAudio)
    target_include_directories(ReverbConsistencyProbe PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME ReverbConsistencyProbe COMMAND ReverbConsistencyProbe)
    set_tests_properties(ReverbConsistencyProbe PROPERTIES LABELS "audio;dsp;reverb;probe")
endif()

# Reverb F4 modulation regression test (taps the real per-line modulator)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbModulationTest.cpp")
    add_executable(ReverbModulationTest AestraAudio/ReverbModulationTest.cpp)
    target_link_libraries(ReverbModulationTest PRIVATE AestraAudio)
    target_include_directories(ReverbModulationTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME ReverbModulationTest COMMAND ReverbModulationTest)
    set_tests_properties(ReverbModulationTest PROPERTIES LABELS "audio;dsp;reverb;modulation")
endif()

# Reverb F6 stereo regression test (mono compatibility + width)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbStereoTest.cpp")
    add_executable(ReverbStereoTest AestraAudio/ReverbStereoTest.cpp)
    target_link_libraries(ReverbStereoTest PRIVATE AestraAudio)
    target_include_directories(ReverbStereoTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME ReverbStereoTest COMMAND ReverbStereoTest)
    set_tests_properties(ReverbStereoTest PROPERTIES LABELS "audio;dsp;reverb;stereo")
endif()

# Reverb F8 tail-dormancy test (idle-instance optimization correctness)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbDormancyTest.cpp")
    add_executable(ReverbDormancyTest AestraAudio/ReverbDormancyTest.cpp)
    target_link_libraries(ReverbDormancyTest PRIVATE AestraAudio)
    target_include_directories(ReverbDormancyTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME ReverbDormancyTest COMMAND ReverbDormancyTest)
    set_tests_properties(ReverbDormancyTest PROPERTIES LABELS "audio;dsp;reverb;dormancy")
endif()

# Reverb Harmonic-Motion / Tremolo investigation labs — experimental tooling,
# gated so CI-safe/public test builds stay lean (opt in via AESTRA_ENABLE_EXPERIMENTAL_TESTS).
if(AESTRA_ENABLE_EXPERIMENTAL_TESTS)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbHarmonicMotionLab.cpp")
        add_executable(AestraReverbHarmonicMotionLab AestraAudio/ReverbHarmonicMotionLab.cpp)
        target_link_libraries(AestraReverbHarmonicMotionLab PRIVATE AestraAudio)
        target_include_directories(AestraReverbHarmonicMotionLab PRIVATE
            ${CMAKE_SOURCE_DIR}/AestraAudio/include
            ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
            ${CMAKE_SOURCE_DIR}/AestraCore/include
        )
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbTremoloLab.cpp")
        add_executable(AestraReverbTremoloLab AestraAudio/ReverbTremoloLab.cpp)
        target_link_libraries(AestraReverbTremoloLab PRIVATE AestraAudio)
        target_include_directories(AestraReverbTremoloLab PRIVATE
            ${CMAKE_SOURCE_DIR}/AestraAudio/include
            ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
            ${CMAKE_SOURCE_DIR}/AestraCore/include
        )
    endif()
else()
    message(STATUS "Reverb tremolo/harmonic-motion investigation labs skipped (set AESTRA_ENABLE_EXPERIMENTAL_TESTS=ON to build)")
endif()

# Reverb Listening Pack generator (evaluation aid, not a test)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbListeningPack.cpp")
    add_executable(AestraReverbListeningPack AestraAudio/ReverbListeningPack.cpp)
    target_link_libraries(AestraReverbListeningPack PRIVATE AestraAudio)
    target_include_directories(AestraReverbListeningPack PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

# Reverb Quality Measurement Lab
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbQualityLab.cpp")
    add_executable(AestraReverbQualityLab AestraAudio/ReverbQualityLab.cpp)
    target_link_libraries(AestraReverbQualityLab PRIVATE AestraAudio)
    target_include_directories(AestraReverbQualityLab PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

# Reverb Stereo Characterization Lab (F5/F6 measurement)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbStereoLab.cpp")
    add_executable(AestraReverbStereoLab AestraAudio/ReverbStereoLab.cpp)
    target_link_libraries(AestraReverbStereoLab PRIVATE AestraAudio)
    target_include_directories(AestraReverbStereoLab PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

# Reverb Modulation Baseline Lab (F4 pre/post measurement)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbModulationLab.cpp")
    add_executable(AestraReverbModulationLab AestraAudio/ReverbModulationLab.cpp)
    target_link_libraries(AestraReverbModulationLab PRIVATE AestraAudio)
    target_include_directories(AestraReverbModulationLab PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

# Reverb Material Validation Lab
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbMaterialLab.cpp")
    add_executable(AestraReverbMaterialLab AestraAudio/ReverbMaterialLab.cpp)
    target_link_libraries(AestraReverbMaterialLab PRIVATE AestraAudio)
    target_include_directories(AestraReverbMaterialLab PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    # Regression thresholds are calibrated for Linux/GCC floating-point behavior.
    # Register test only on Linux to avoid false failures from cross-platform FP drift.
    if(AESTRA_ENABLE_RUNTIME_TESTS AND UNIX AND NOT APPLE)
        add_test(NAME ReverbMaterialLabTest COMMAND AestraReverbMaterialLab)
    elseif(NOT AESTRA_ENABLE_RUNTIME_TESTS)
        message(STATUS "ReverbMaterialLabTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
    endif()
endif()
