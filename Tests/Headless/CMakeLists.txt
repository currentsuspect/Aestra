# © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
# Headless Tests — Deterministic, no audio hardware required

cmake_minimum_required(VERSION 3.22)

# Headless Offline Renderer
add_executable(HeadlessOfflineRenderer
    HeadlessOfflineRenderer.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
)
target_link_libraries(HeadlessOfflineRenderer PRIVATE AestraAudio)
target_include_directories(HeadlessOfflineRenderer PRIVATE
    ${CMAKE_SOURCE_DIR}/Source
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME HeadlessOfflineRenderer
    COMMAND HeadlessOfflineRenderer
            "${CMAKE_CURRENT_SOURCE_DIR}/dummy.aes"
            "${CMAKE_CURRENT_BINARY_DIR}/dummy_out.wav"
)
set_tests_properties(HeadlessOfflineRenderer PROPERTIES
    LABELS "headless;offline;rendering;contract:audio"
    ENVIRONMENT "AESTRA_HEADLESS=1"
)

# Offline Render Regression Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/OfflineRenderRegressionTest.cpp")
    add_executable(OfflineRenderRegressionTest
        OfflineRenderRegressionTest.cpp
    )
    target_link_libraries(OfflineRenderRegressionTest PRIVATE AestraAudio)
    target_include_directories(OfflineRenderRegressionTest PRIVATE
        ${CMAKE_SOURCE_DIR}/Source
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME OfflineRenderRegressionTest
        COMMAND OfflineRenderRegressionTest
    )
    set_tests_properties(OfflineRenderRegressionTest PROPERTIES
        LABELS "headless;regression;offline;contract:audio"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Engine Output Stage Characterization (pins processBlock behavior pre-refactor)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/EngineOutputStageCharacterizationTest.cpp")
    add_executable(EngineOutputStageCharacterizationTest
        EngineOutputStageCharacterizationTest.cpp
    )
    target_link_libraries(EngineOutputStageCharacterizationTest PRIVATE AestraAudio)
    target_include_directories(EngineOutputStageCharacterizationTest PRIVATE
        ${CMAKE_SOURCE_DIR}/Source
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME EngineOutputStageCharacterizationTest
        COMMAND EngineOutputStageCharacterizationTest
    )
    set_tests_properties(EngineOutputStageCharacterizationTest PROPERTIES
        LABELS "headless;characterization;engine;contract:audio"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Headless export session invariance (exportTo must not mutate live transport)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/HeadlessExportSessionInvarianceTest.cpp")
    add_executable(HeadlessExportSessionInvarianceTest
        HeadlessExportSessionInvarianceTest.cpp
    )
    target_link_libraries(HeadlessExportSessionInvarianceTest PRIVATE AestraAudio)
    target_include_directories(HeadlessExportSessionInvarianceTest PRIVATE
        ${CMAKE_SOURCE_DIR}/Source
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME HeadlessExportSessionInvarianceTest
        COMMAND HeadlessExportSessionInvarianceTest
    )
    set_tests_properties(HeadlessExportSessionInvarianceTest PROPERTIES
        LABELS "headless;export;engine;contract:audio"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Rumble DSP Quality Contract
if(AESTRA_ENABLE_TEST_LICENSES AND TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RumbleQualityTest.cpp")
    add_executable(RumbleQualityTest RumbleQualityTest.cpp)
    if(UNIX AND NOT APPLE)
        target_link_libraries(RumbleQualityTest PRIVATE -Wl,--start-group AestraRumble AestraAudioCore -Wl,--end-group)
    else()
        target_link_libraries(RumbleQualityTest PRIVATE AestraPremiumPlugins AestraAudioCore)
    endif()
    target_include_directories(RumbleQualityTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME RumbleQualityTest COMMAND RumbleQualityTest)
    set_tests_properties(RumbleQualityTest PROPERTIES
        LABELS "headless;rumble;plugin;quality"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Rumble Factory Preset Bank Contract
if(AESTRA_ENABLE_TEST_LICENSES AND TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RumblePresetBankTest.cpp")
    add_executable(RumblePresetBankTest RumblePresetBankTest.cpp)
    if(UNIX AND NOT APPLE)
        target_link_libraries(RumblePresetBankTest PRIVATE -Wl,--start-group AestraRumble AestraAudioCore -Wl,--end-group)
    else()
        target_link_libraries(RumblePresetBankTest PRIVATE AestraPremiumPlugins AestraAudioCore)
    endif()
    target_include_directories(RumblePresetBankTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME RumblePresetBankTest COMMAND RumblePresetBankTest)
    set_tests_properties(RumblePresetBankTest PROPERTIES
        LABELS "headless;rumble;plugin;presets"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Rumble Spectral + Performance Contract
if(AESTRA_ENABLE_TEST_LICENSES AND TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RumbleSpectralPerformanceTest.cpp")
    add_executable(RumbleSpectralPerformanceTest RumbleSpectralPerformanceTest.cpp)
    if(UNIX AND NOT APPLE)
        target_link_libraries(RumbleSpectralPerformanceTest PRIVATE
            -Wl,--start-group AestraRumble AestraAudioCore -Wl,--end-group)
    else()
        target_link_libraries(RumbleSpectralPerformanceTest PRIVATE AestraPremiumPlugins AestraAudioCore)
    endif()
    target_include_directories(RumbleSpectralPerformanceTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_SOURCE_DIR}/Tests/Research
    )
    add_test(NAME RumbleSpectralPerformanceTest COMMAND RumbleSpectralPerformanceTest)
    set_tests_properties(RumbleSpectralPerformanceTest PROPERTIES
        LABELS "headless;rumble;plugin;spectral;performance"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Rumble Render Test
if(TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RumbleRenderTest.cpp")
    add_executable(RumbleRenderTest
        RumbleRenderTest.cpp
    )
    if(UNIX AND NOT APPLE)
        target_link_libraries(RumbleRenderTest PRIVATE -Wl,--start-group AestraRumble AestraAudioCore -Wl,--end-group)
    else()
        target_link_libraries(RumbleRenderTest PRIVATE AestraPremiumPlugins AestraAudioCore)
    endif()
    target_include_directories(RumbleRenderTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME RumbleRenderTest COMMAND RumbleRenderTest)
    set_tests_properties(RumbleRenderTest PROPERTIES
        LABELS "headless;rumble;plugin;render"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Rumble Arsenal Audible Test
if(TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RumbleArsenalAudibleTest.cpp")
    add_executable(RumbleArsenalAudibleTest
        RumbleArsenalAudibleTest.cpp
    )
    target_link_libraries(RumbleArsenalAudibleTest PRIVATE AestraAudioCore AestraPremiumPlugins)
    target_include_directories(RumbleArsenalAudibleTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME RumbleArsenalAudibleTest COMMAND RumbleArsenalAudibleTest)
    set_tests_properties(RumbleArsenalAudibleTest PROPERTIES
        LABELS "headless;rumble;arsenal;audio"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Live MIDI Input Test — live note input audible with transport stopped.
# Core-mode: uses the built-in SamplerPlugin with an injected synthetic sample,
# so it runs on every CI lane (no premium modules, no files, no devices).
if(TARGET AestraAudioCore AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LiveMidiInputTest.cpp")
    add_executable(LiveMidiInputTest
        LiveMidiInputTest.cpp
    )
    target_link_libraries(LiveMidiInputTest PRIVATE AestraAudioCore)
    target_include_directories(LiveMidiInputTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME LiveMidiInputTest COMMAND LiveMidiInputTest)
    set_tests_properties(LiveMidiInputTest PROPERTIES
        LABELS "headless;arsenal;audio;midi;contract:audio"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Hardware MIDI Input Test — RtMidi message translation (pure) plus the
# hardware SPSC queue path (AudioEngine::postHardwareMidiEvent) audible with
# transport stopped. Uses only header-inline APIs, so it links AestraAudioCore
# and runs on every CI lane regardless of MIDI backend presence.
# No EXISTS guard: a missing source must fail configure, not silently skip
# the test (review finding on #454).
if(TARGET AestraAudioCore)
    add_executable(HardwareMidiInputTest
        HardwareMidiInputTest.cpp
    )
    target_link_libraries(HardwareMidiInputTest PRIVATE AestraAudioCore)
    target_include_directories(HardwareMidiInputTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME HardwareMidiInputTest COMMAND HardwareMidiInputTest)
    set_tests_properties(HardwareMidiInputTest PROPERTIES
        LABELS "headless;arsenal;audio;midi;contract:audio"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Sampler One-Shot Envelope Test — regression for #452: one-shot samples
# shorter than the configured release must be audible with the DEFAULT
# envelope. Drives SamplerPlugin directly; core-linked, every CI lane.
# No EXISTS guard: a missing committed test source must fail configure.
if(TARGET AestraAudioCore)
    add_executable(SamplerOneShotEnvelopeTest
        SamplerOneShotEnvelopeTest.cpp
    )
    target_link_libraries(SamplerOneShotEnvelopeTest PRIVATE AestraAudioCore)
    target_include_directories(SamplerOneShotEnvelopeTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME SamplerOneShotEnvelopeTest COMMAND SamplerOneShotEnvelopeTest)
    set_tests_properties(SamplerOneShotEnvelopeTest PROPERTIES
        LABELS "headless;sampler;audio;dsp;regression;contract:audio"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Automation Playback Test — end-to-end regression: lane automation curves
# must reach the engine (snapshot copy), evaluate in the beat domain across
# tempo changes, and follow UI point drags. Core-linked, every CI lane.
# No EXISTS guard: a missing committed test source must fail configure.
if(TARGET AestraAudioCore)
    add_executable(AutomationPlaybackTest
        AutomationPlaybackTest.cpp
    )
    target_link_libraries(AutomationPlaybackTest PRIVATE AestraAudioCore)
    target_include_directories(AutomationPlaybackTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME AutomationPlaybackTest COMMAND AutomationPlaybackTest)
    set_tests_properties(AutomationPlaybackTest PROPERTIES
        LABELS "headless;automation;audio;engine;regression;contract:audio"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# PDC v2 P1 — scaffolding tests (see Aestra-Internals: aestra-docs/PDC-v2-Design.md §12)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PDCFlatChainTest.cpp")
    add_executable(PDCFlatChainTest PDCFlatChainTest.cpp)
    target_link_libraries(PDCFlatChainTest PRIVATE AestraAudio)
    target_include_directories(PDCFlatChainTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME PDCFlatChainTest COMMAND PDCFlatChainTest)
    set_tests_properties(PDCFlatChainTest PROPERTIES
        LABELS "headless;pdc;regression;contract:plugins"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Plugin lifecycle smoke: Sat->Filter->OTT->LFO chain through save/reload,
# reprepare (device change), and audio — parameter preservation + finite output.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PluginLifecycleSmokeTest.cpp")
    add_executable(PluginLifecycleSmokeTest PluginLifecycleSmokeTest.cpp)
    target_link_libraries(PluginLifecycleSmokeTest PRIVATE AestraAudio)
    target_include_directories(PluginLifecycleSmokeTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME PluginLifecycleSmokeTest COMMAND PluginLifecycleSmokeTest)
    set_tests_properties(PluginLifecycleSmokeTest PROPERTIES
        LABELS "headless;plugins;lifecycle;contract:plugins"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PDCSolverPurityTest.cpp")
    add_executable(PDCSolverPurityTest PDCSolverPurityTest.cpp)
    target_link_libraries(PDCSolverPurityTest PRIVATE AestraAudio)
    target_include_directories(PDCSolverPurityTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME PDCSolverPurityTest COMMAND PDCSolverPurityTest)
    set_tests_properties(PDCSolverPurityTest PROPERTIES
        LABELS "headless;pdc;solver;contract:plugins"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# PDC v2 P4a — graph-aware solver tests (solver level; engine RT path in P4b)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PDCBusChainTest.cpp")
    add_executable(PDCBusChainTest PDCBusChainTest.cpp)
    target_link_libraries(PDCBusChainTest PRIVATE AestraAudio)
    target_include_directories(PDCBusChainTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME PDCBusChainTest COMMAND PDCBusChainTest)
    set_tests_properties(PDCBusChainTest PROPERTIES
        LABELS "headless;pdc;solver;contract:plugins"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PDCBranchingConvergenceTest.cpp")
    add_executable(PDCBranchingConvergenceTest PDCBranchingConvergenceTest.cpp)
    target_link_libraries(PDCBranchingConvergenceTest PRIVATE AestraAudio)
    target_include_directories(PDCBranchingConvergenceTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME PDCBranchingConvergenceTest COMMAND PDCBranchingConvergenceTest)
    set_tests_properties(PDCBranchingConvergenceTest PROPERTIES
        LABELS "headless;pdc;solver;regression;contract:plugins"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# PDC v2 P4b.3 — RT path exercises per-send compensation via processBlock
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PDCRTConsumptionTest.cpp")
    add_executable(PDCRTConsumptionTest PDCRTConsumptionTest.cpp)
    target_link_libraries(PDCRTConsumptionTest PRIVATE AestraAudio)
    target_include_directories(PDCRTConsumptionTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME PDCRTConsumptionTest COMMAND PDCRTConsumptionTest)
    set_tests_properties(PDCRTConsumptionTest PROPERTIES
        LABELS "headless;pdc;engine;rt"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# PDC v2 P4b.1 — engine populates LatencyGraph::edges from real project routing
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PDCEngineEdgeConstructionTest.cpp")
    add_executable(PDCEngineEdgeConstructionTest PDCEngineEdgeConstructionTest.cpp)
    target_link_libraries(PDCEngineEdgeConstructionTest PRIVATE AestraAudio)
    target_include_directories(PDCEngineEdgeConstructionTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME PDCEngineEdgeConstructionTest COMMAND PDCEngineEdgeConstructionTest)
    set_tests_properties(PDCEngineEdgeConstructionTest PROPERTIES
        LABELS "headless;pdc;engine;contract:plugins"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Routing send parity — acceptance tests for BUG-3 (#261) and BUG-5 (#262)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RoutingSendPanParityTest.cpp")
    add_executable(RoutingSendPanParityTest RoutingSendPanParityTest.cpp)
    target_link_libraries(RoutingSendPanParityTest PRIVATE AestraAudio)
    target_include_directories(RoutingSendPanParityTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME RoutingSendPanParityTest COMMAND RoutingSendPanParityTest)
    set_tests_properties(RoutingSendPanParityTest PROPERTIES
        LABELS "headless;routing;dsp;regression;contract:audio"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# PDC disable-clears-compensation regression (#684)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/PDCDisableClearsCompensationTest.cpp")
    add_executable(PDCDisableClearsCompensationTest PDCDisableClearsCompensationTest.cpp)
    target_link_libraries(PDCDisableClearsCompensationTest PRIVATE AestraAudio)
    target_include_directories(PDCDisableClearsCompensationTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_test(NAME PDCDisableClearsCompensationTest COMMAND PDCDisableClearsCompensationTest)
    set_tests_properties(PDCDisableClearsCompensationTest PROPERTIES
        LABELS "headless;pdc;engine;regression;contract:audio"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

message(STATUS "Headless tests enabled - AudioEngine API implemented")
