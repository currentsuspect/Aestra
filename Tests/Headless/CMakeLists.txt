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
add_test(NAME HeadlessOfflineRenderer COMMAND HeadlessOfflineRenderer dummy.aes dummy.wav --duration-seconds 5)
set_tests_properties(HeadlessOfflineRenderer PROPERTIES
    LABELS "headless;offline;rendering"
    ENVIRONMENT "AESTRA_HEADLESS=1"
)

# Offline Render Regression Test
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
add_test(NAME OfflineRenderRegressionTest COMMAND OfflineRenderRegressionTest dummy.aes dummy.wav --duration-seconds 5)
set_tests_properties(OfflineRenderRegressionTest PROPERTIES
    LABELS "headless;regression;offline"
    ENVIRONMENT "AESTRA_HEADLESS=1"
)

message(STATUS "Headless tests enabled - AudioEngine API implemented")
