# © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
# Headless Tests — Deterministic, no audio hardware required

cmake_minimum_required(VERSION 3.22)

# Headless Offline Renderer
add_executable(HeadlessOfflineRenderer
    HeadlessOfflineRenderer.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
)
target_link_libraries(HeadlessOfflineRenderer PRIVATE AestraAudio AestraCore)
target_include_directories(HeadlessOfflineRenderer PRIVATE
    ${CMAKE_SOURCE_DIR}/Source
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME HeadlessOfflineRenderer COMMAND HeadlessOfflineRenderer)
set_tests_properties(HeadlessOfflineRenderer PROPERTIES
    LABELS "headless;offline;rendering"
    ENVIRONMENT "AESTRA_HEADLESS=1"
)

# Offline Render Regression Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/OfflineRenderRegressionTest.cpp")
    add_executable(OfflineRenderRegressionTest
        OfflineRenderRegressionTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
    )
    target_link_libraries(OfflineRenderRegressionTest PRIVATE AestraAudio AestraCore)
    target_include_directories(OfflineRenderRegressionTest PRIVATE
        ${CMAKE_SOURCE_DIR}/Source
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME OfflineRenderRegressionTest COMMAND OfflineRenderRegressionTest)
    set_tests_properties(OfflineRenderRegressionTest PROPERTIES
        LABELS "headless;regression;offline"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Rumble Render Test
if(TARGET AestraRumble AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RumbleRenderTest.cpp")
    add_executable(RumbleRenderTest
        RumbleRenderTest.cpp
    )
    target_link_libraries(RumbleRenderTest PRIVATE AestraRumble)
    target_include_directories(RumbleRenderTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME RumbleRenderTest COMMAND RumbleRenderTest)
    set_tests_properties(RumbleRenderTest PROPERTIES
        LABELS "headless;rumble;plugin;render"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

message(STATUS "Headless tests enabled - AudioEngine API implemented")
