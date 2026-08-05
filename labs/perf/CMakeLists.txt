# © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#
# Folio performance-baseline apparatus.
#
# Opt-in only (AESTRA_BUILD_PERF_APPARATUS=ON). The apparatus measures the
# engine; it must never become part of the engine's default build, or the thing
# being measured stops being the thing that shipped.
#
# ProjectSerializer.cpp is compiled directly into each tool rather than linked
# from a library, matching the established Tests/Headless convention — it lives
# in Source/ and is not part of the AestraAudio target.

cmake_minimum_required(VERSION 3.22)

set(AESTRA_PERF_TOOL_INCLUDES
    ${CMAKE_SOURCE_DIR}/Source
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)

# Builds the one canonical baseline session and saves it via ProjectSerializer.
add_executable(FolioBuildFixture
    tools/build-fixture.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
)
target_link_libraries(FolioBuildFixture PRIVATE AestraAudio)
target_include_directories(FolioBuildFixture PRIVATE ${AESTRA_PERF_TOOL_INCLUDES})

# Proves the reopened project still satisfies every claim in CAPABILITIES.json.
add_executable(FolioValidateFixture
    tools/validate-fixture.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
)
target_link_libraries(FolioValidateFixture PRIVATE AestraAudio)
target_include_directories(FolioValidateFixture PRIVATE ${AESTRA_PERF_TOOL_INCLUDES})

# Proves the fixture renders the behaviour CAPABILITIES.json declares.
add_executable(FolioProbeFixture
    tools/probe-fixture.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
)
target_link_libraries(FolioProbeFixture PRIVATE AestraAudio)
target_include_directories(FolioProbeFixture PRIVATE ${AESTRA_PERF_TOOL_INCLUDES})

message(STATUS "Folio perf apparatus enabled (FolioBuildFixture, FolioValidateFixture, FolioProbeFixture)")
