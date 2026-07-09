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
    LABELS "headless;offline;rendering"
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
        LABELS "headless;regression;offline"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Rumble Render Test
if(TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RumbleRenderTest.cpp")
    add_executable(RumbleRenderTest
        RumbleRenderTest.cpp
    )
    target_link_libraries(RumbleRenderTest PRIVATE AestraPremiumPlugins AestraAudioCore)
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
        LABELS "headless;arsenal;audio;midi"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# PDC v2 P1 — scaffolding tests (see AestraDocs/PDC-v2-Design.md §12)
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
        LABELS "headless;pdc;regression"
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
        LABELS "headless;pdc;solver"
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
        LABELS "headless;pdc;solver"
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
        LABELS "headless;pdc;solver;regression"
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
        LABELS "headless;pdc;engine"
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
        LABELS "headless;routing;dsp;regression"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

message(STATUS "Headless tests enabled - AudioEngine API implemented")
