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
add_test(NAME HeadlessOfflineRenderer COMMAND HeadlessOfflineRenderer ${CMAKE_CURRENT_SOURCE_DIR}/dummy.aes output.wav)
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
    add_test(NAME OfflineRenderRegressionTest COMMAND OfflineRenderRegressionTest ${CMAKE_CURRENT_SOURCE_DIR}/dummy.aes reference.wav)
    set_tests_properties(OfflineRenderRegressionTest PROPERTIES
        LABELS "headless;regression;offline"
        ENVIRONMENT "AESTRA_HEADLESS=1"
    )
endif()

# Rumble Render Test
if(TARGET AestraAudioCore AND TARGET AestraRumble AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RumbleRenderTest.cpp")
    add_executable(RumbleRenderTest
        RumbleRenderTest.cpp
    )
    target_link_libraries(RumbleRenderTest PRIVATE AestraRumble AestraAudioCore AestraCore)
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
if(TARGET AestraAudioCore AND TARGET AestraRumble AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/RumbleArsenalAudibleTest.cpp")
    add_executable(RumbleArsenalAudibleTest
        RumbleArsenalAudibleTest.cpp
    )
    target_link_libraries(RumbleArsenalAudibleTest PRIVATE AestraAudioCore AestraRumble AestraCore)
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

message(STATUS "Headless tests enabled - AudioEngine API implemented")
