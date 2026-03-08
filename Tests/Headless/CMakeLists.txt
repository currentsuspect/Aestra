# © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
# Headless Tests — Deterministic, no audio hardware required

cmake_minimum_required(VERSION 3.22)

# TODO: Enable headless tests once AudioEngine API is implemented
# Currently disabled because tests reference non-existent methods:
# - AudioEngine::setTrackManager()
# - AudioEngine::initialize() 
# - AudioEngine::setPlayhead()
#
# To enable: Uncomment below and implement the AudioEngine interface

# Headless Offline Renderer
# add_executable(HeadlessOfflineRenderer HeadlessOfflineRenderer.cpp)
# target_link_libraries(HeadlessOfflineRenderer PRIVATE AestraAudioCore AestraCore)
# target_include_directories(HeadlessOfflineRenderer PRIVATE
#     ${CMAKE_SOURCE_DIR}/Source
#     ${CMAKE_SOURCE_DIR}/AestraAudio/include
#     ${CMAKE_SOURCE_DIR}/AestraCore/include
# )
# add_test(NAME HeadlessOfflineRenderer COMMAND HeadlessOfflineRenderer)
# set_tests_properties(HeadlessOfflineRenderer PROPERTIES
#     LABELS "headless;offline;rendering"
#     ENVIRONMENT "AESTRA_HEADLESS=1"
# )

# Offline Render Regression Test
# add_executable(OfflineRenderRegressionTest OfflineRenderRegressionTest.cpp)
# target_link_libraries(OfflineRenderRegressionTest PRIVATE AestraAudioCore AestraCore)
# target_include_directories(OfflineRenderRegressionTest PRIVATE
#     ${CMAKE_SOURCE_DIR}/Source
#     ${CMAKE_SOURCE_DIR}/AestraAudio/include
#     ${CMAKE_SOURCE_DIR}/AestraCore/include
# )
# add_test(NAME OfflineRenderRegressionTest COMMAND OfflineRenderRegressionTest)
# set_tests_properties(OfflineRenderRegressionTest PROPERTIES
#     LABELS "headless;regression;offline"
#     ENVIRONMENT "AESTRA_HEADLESS=1"
# )

message(STATUS "Headless tests temporarily disabled - waiting for AudioEngine API implementation")
