cmake_minimum_required(VERSION 3.22)

# Low-memory build optimizations (include early before project())
if(AESTRA_LOW_MEMORY_BUILD)
    include(${CMAKE_SOURCE_DIR}/cmake/LowMemory.cmake)
endif()

# Use consistent CRT runtime across all targets (must be set before project())
if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Optional: auto core-mode helper
if(EXISTS "${CMAKE_SOURCE_DIR}/aestra-core/cmake/AestraCoreMode.cmake")
	include(${CMAKE_SOURCE_DIR}/aestra-core/cmake/AestraCoreMode.cmake)
endif()

project(AESTRA VERSION 0.1.0 LANGUAGES C CXX)

# Ensure CTest discovery/registration works from the build root.
include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Build Options
# =============================================================================

# Headless-only mode is intended for CI/containers: configure & build without UI.
option(AESTRA_HEADLESS_ONLY "Configure/build headless targets only (skips UI + app)" OFF)
option(AESTRA_ENABLE_UI "Build the AESTRA_DAW UI application" ON)
option(AESTRA_ENABLE_TESTS "Build test executables" ON)
option(AESTRA_CI "Configure deterministic CI-safe defaults" OFF)
option(AESTRA_ENABLE_PREMIUM_PLUGINS "Build private premium plugin modules" OFF)
option(AESTRA_LICENSE_ENABLE_PREMIUM_LEASES "Allow signed Supporter/Founder leases to activate premium entitlements" OFF)
option(AESTRA_ALLOW_LICENSE_GATE_OFF "Allow lab/development builds to omit offline license verification" OFF)
option(AESTRA_ENABLE_TEST_LICENSES "Enable test-only mock license acceptance" OFF)
option(AESTRA_REVERB_PROFILE "Enable AestraVerb stage profiling (lab/benchmark only)" OFF)
option(AESTRA_REVERB_DIAGNOSTICS "Enable AestraVerb output-level diagnostics (lab only)" OFF)

if(AESTRA_HEADLESS_ONLY)
  set(AESTRA_ENABLE_UI OFF CACHE BOOL "Build the AESTRA_DAW UI application" FORCE)
  # Tests should be allowed in headless mode (e.g. for CI)
endif()

if(AESTRA_CI)
  set(AESTRA_HEADLESS_ONLY ON CACHE BOOL "Configure/build headless targets only (skips UI + app)" FORCE)
  set(AESTRA_ENABLE_UI OFF CACHE BOOL "Build the AESTRA_DAW UI application" FORCE)
  set(AESTRA_ENABLE_TESTS ON CACHE BOOL "Build test executables" FORCE)
  set(AESTRA_ENABLE_RUNTIME_TESTS OFF CACHE BOOL "Enable runtime/device-dependent tests" FORCE)
  set(AESTRA_ENABLE_EXPERIMENTAL_TESTS OFF CACHE BOOL "Enable unstable/experimental tests" FORCE)
endif()

if(AESTRA_ENABLE_PREMIUM_PLUGINS)
  set(AESTRAUI_ENABLE_PREMIUM_EDITORS ON CACHE BOOL "Build private premium plugin editors" FORCE)
endif()

# =============================================================================
# Platform Detection & Flags
# =============================================================================
if(UNIX AND NOT APPLE)
  set(AESTRA_PLATFORM_LINUX ON)
  add_compile_definitions(AESTRA_PLATFORM_LINUX=1)

  # Detect distribution for logging/diagnostics
  find_program(LSB_RELEASE lsb_release)
  if(LSB_RELEASE)
    execute_process(COMMAND ${LSB_RELEASE} -is OUTPUT_VARIABLE LINUX_DISTRO OUTPUT_STRIP_TRAILING_WHITESPACE)
  else()
    set(LINUX_DISTRO "Unknown")
  endif()
  message(STATUS "Detected Linux platform (${LINUX_DISTRO})")
  
  # PIC for libraries
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# =============================================================================
# Dependencies
# =============================================================================
# SDL2
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
  if(EXISTS "${CMAKE_SOURCE_DIR}/external/SDL2/CMakeLists.txt")
    add_subdirectory(external/SDL2)
  else()
    message(STATUS "SDL2 not found and external/SDL2 missing. Linux platform layer may fail to build.")
  endif()
endif()

# RtAudio
if(AESTRA_PLATFORM_LINUX)
  option(RTAUDIO_API_ALSA "Compile with ALSA support" ON)
  option(RTAUDIO_API_PULSE "Compile with PulseAudio support" ON)
  option(RTAUDIO_API_JACK "Compile with JACK support" OFF)
  
  if(EXISTS "${CMAKE_SOURCE_DIR}/AestraAudio/External/rtaudio/CMakeLists.txt")
    message(STATUS "RtAudio source available via AestraAudio/External/rtaudio")
  else()
    message(STATUS "AestraAudio/External/rtaudio missing. Native Linux audio backend will not build.")
  endif()
endif()

# =============================================================================
# AestraCore - Base Utilities
# =============================================================================
add_subdirectory(AestraCore)

if(AESTRA_PLATFORM_LINUX)
    add_compile_options(-Wall -Wextra -Wpedantic)
    # Suppress known noisy warnings
    add_compile_options(
        -Wno-sequence-point
        -Wno-pedantic
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-maybe-uninitialized
        -Wno-switch
        -Wno-format-truncation
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder>
    )
endif()

# =============================================================================
# AestraPlat - Platform Abstraction (Must be before AestraAudio)
# =============================================================================
add_subdirectory(AestraPlat)

# =============================================================================
# AestraLicense - Offline License Verification
# =============================================================================
if(AESTRA_ALLOW_LICENSE_GATE_OFF)
    message(WARNING "AESTRA_ALLOW_LICENSE_GATE_OFF=ON: omitting offline license verification for this lab/development build.")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/AestraLicense/CMakeLists.txt")
    add_subdirectory(AestraLicense)
else()
    message(FATAL_ERROR "AestraLicense is required. Set AESTRA_ALLOW_LICENSE_GATE_OFF=ON only for lab/development builds that intentionally omit offline license verification.")
endif()

# =============================================================================
# Shared Plugin API Interface
# Provides the plugin contract headers without creating target cycles.
# =============================================================================
add_library(AestraPluginAPI INTERFACE)
target_include_directories(AestraPluginAPI INTERFACE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
)
target_link_libraries(AestraPluginAPI INTERFACE AestraCore)

# =============================================================================
# AestraPlugins - Premium Plugins (Optional Private Modules)
# =============================================================================
if(AESTRA_ENABLE_PREMIUM_PLUGINS AND EXISTS "${CMAKE_SOURCE_DIR}/AestraPlugins/CMakeLists.txt")
    message(STATUS "Aestra premium plugins enabled")
    add_subdirectory(AestraPlugins)
elseif(AESTRA_ENABLE_PREMIUM_PLUGINS)
    message(WARNING "AESTRA_ENABLE_PREMIUM_PLUGINS=ON but AestraPlugins/CMakeLists.txt was not found")
else()
    message(STATUS "Aestra premium plugins disabled")
endif()

# =============================================================================
# AestraAudio - Audio Engine
# =============================================================================
add_subdirectory(AestraAudio)

# =============================================================================
# AestraHeadless - Offline engine smoke test (no UI/OpenGL)
# =============================================================================

if(EXISTS "${CMAKE_SOURCE_DIR}/Source/App/HeadlessMain.cpp")
  add_executable(AestraHeadless
    Source/App/HeadlessMain.cpp
    Source/Core/AudioSettingsStore.cpp
    Source/Core/ProjectSerializer.cpp
  )

  if(MSVC)
    target_compile_options(AestraHeadless PRIVATE /utf-8)
  endif()

  set_target_properties(AestraHeadless PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  )

  target_link_libraries(AestraHeadless PRIVATE
    AestraAudioCore
    AestraCore
  )

  target_include_directories(AestraHeadless PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
    ${CMAKE_SOURCE_DIR}/Source
  )

  if(WIN32)
    target_compile_definitions(AestraHeadless PRIVATE
      _CRT_SECURE_NO_WARNINGS
      NOMINMAX
    )
    # Media Foundation symbols used by MiniAudioDecoder on Windows
    target_link_libraries(AestraHeadless PRIVATE mfplat mfreadwrite mfuuid)
  endif()

  message(STATUS "AestraHeadless configured")
endif()

# =============================================================================
# MuseRepl - Agentic runtime entry point (JSONL over stdin/stdout, no UI)
# =============================================================================

if(EXISTS "${CMAKE_SOURCE_DIR}/Source/App/MuseReplMain.cpp")
  add_executable(MuseRepl
    Source/App/MuseReplMain.cpp
  )

  if(MSVC)
    target_compile_options(MuseRepl PRIVATE /utf-8)
  endif()

  set_target_properties(MuseRepl PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  )

  target_link_libraries(MuseRepl PRIVATE
    AestraAudioCore
    AestraCore
  )

  target_include_directories(MuseRepl PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraCore/include
  )

  if(WIN32)
    target_compile_definitions(MuseRepl PRIVATE
      _CRT_SECURE_NO_WARNINGS
      NOMINMAX
    )
  endif()

  message(STATUS "MuseRepl configured")
endif()

# =============================================================================
# muse — one-shot CLI for the Muse socket. Built unconditionally: unlike
# muse-agent it needs no libcurl and no provider adapters, only the socket
# client and JSON, so a terminal session is always available.
# =============================================================================

if(EXISTS "${CMAKE_SOURCE_DIR}/Source/MuseAgent/MuseCliMain.cpp")
  add_executable(muse
    Source/MuseAgent/MuseCliMain.cpp
    Source/MuseAgent/MuseSocketClient.cpp
  )
  if(MSVC)
    target_compile_options(muse PRIVATE /utf-8)
    target_compile_definitions(muse PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  endif()
  set_target_properties(muse PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  )
  target_include_directories(muse PRIVATE
    ${CMAKE_SOURCE_DIR}/Source/MuseAgent
    ${CMAKE_SOURCE_DIR}/AestraCore/include
  )
  target_link_libraries(muse PRIVATE AestraCore)
  if(WIN32)
    target_link_libraries(muse PRIVATE ws2_32)
  endif()
  message(STATUS "muse CLI configured")
endif()

# =============================================================================
# muse-agent — the standalone Muse driver (interchangeable intelligence).
# Deliberately OUTSIDE the Aestra binary: HTTP, TLS, and provider adapters
# never enter the DAW. Only built where libcurl is available.
# =============================================================================

find_package(CURL QUIET)
if(CURL_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/Source/MuseAgent/MuseAgentMain.cpp")
  add_library(MuseAgentCore STATIC
    Source/MuseAgent/AgentLoop.cpp
    Source/MuseAgent/AnthropicProvider.cpp
    Source/MuseAgent/HttpClient.cpp
    Source/MuseAgent/MuseSocketClient.cpp
    Source/MuseAgent/OpenAICompatProvider.cpp
  )
  set_target_properties(MuseAgentCore PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
  )
  target_include_directories(MuseAgentCore PUBLIC
    ${CMAKE_SOURCE_DIR}/Source/MuseAgent
    ${CMAKE_SOURCE_DIR}/AestraCore/include
  )
  target_link_libraries(MuseAgentCore PUBLIC AestraCore CURL::libcurl)
  if(WIN32)
    target_link_libraries(MuseAgentCore PUBLIC ws2_32)
    target_compile_definitions(MuseAgentCore PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  endif()

  add_executable(muse-agent Source/MuseAgent/MuseAgentMain.cpp)
  if(MSVC)
    target_compile_options(muse-agent PRIVATE /utf-8)
  endif()
  set_target_properties(muse-agent PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  )
  target_link_libraries(muse-agent PRIVATE MuseAgentCore)
  message(STATUS "muse-agent configured (libcurl ${CURL_VERSION_STRING})")
elseif(NOT CURL_FOUND)
  message(STATUS "muse-agent skipped: libcurl not found")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/Source/App/HeadlessExportMain.cpp")
  add_executable(AestraHeadlessExport
    Source/App/HeadlessExportMain.cpp
  )

  if(MSVC)
    target_compile_options(AestraHeadlessExport PRIVATE /utf-8)
  endif()

  set_target_properties(AestraHeadlessExport PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  )

  target_link_libraries(AestraHeadlessExport PRIVATE
    AestraAudioCore
    AestraCore
  )

  target_include_directories(AestraHeadlessExport PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
    ${CMAKE_SOURCE_DIR}/Source
  )

  if(WIN32)
    target_compile_definitions(AestraHeadlessExport PRIVATE
      _CRT_SECURE_NO_WARNINGS
      NOMINMAX
    )
    target_link_libraries(AestraHeadlessExport PRIVATE mfplat mfreadwrite mfuuid)
  endif()

  if(AESTRA_ENABLE_TESTS)
    add_test(NAME AestraHeadlessExportRejectsBadArgs
      COMMAND AestraHeadlessExport --duration nope)
    set_tests_properties(AestraHeadlessExportRejectsBadArgs PROPERTIES
      WILL_FAIL TRUE
      LABELS "cli;headless;contract:application")

    # Real end-to-end export invocation: arg parse -> template project -> export
    # attempt. Regression guard for the getInstance() abort (SIGABRT on develop;
    # passes once the export path owns its engine). Exit 0 must come with a real
    # non-empty WAV; exit 1 (graceful diagnosed failure) is tolerated only until
    # the HeadlessMusicGenerator commit layer is implemented — see the driver
    # script for the full contract.
    add_test(NAME AestraHeadlessExportEndToEnd
      COMMAND ${CMAKE_COMMAND}
        -DEXPORT_BIN=$<TARGET_FILE:AestraHeadlessExport>
        -DOUT_FILE=${CMAKE_BINARY_DIR}/headless_export_e2e.wav
        -P ${CMAKE_SOURCE_DIR}/Tests/Headless/run_headless_export_e2e.cmake)
    set_tests_properties(AestraHeadlessExportEndToEnd PROPERTIES
      LABELS "cli;headless;export;integrity;contract:audio"
      TIMEOUT 120)
  endif()

  message(STATUS "AestraHeadlessExport configured")
endif()

if(AESTRA_ENABLE_UI)
  # =============================================================================
  # AestraUI - UI Framework
  # =============================================================================
  add_subdirectory(AestraUI)
endif()

# =============================================================================
# Aestra - Main Application
# =============================================================================
if(AESTRA_ENABLE_UI)
  add_subdirectory(Source)
endif()

# =============================================================================
# Tests - Centralized in Tests/ directory
# =============================================================================
if(AESTRA_ENABLE_TESTS)
    add_subdirectory(Tests)
endif()

message(STATUS "")
message(STATUS "=================================")
message(STATUS "  Aestra - Pure Native")
message(STATUS "=================================")
message(STATUS "UI: AestraUI (OpenGL)")
message(STATUS "Audio: AestraAudio (RtAudio)")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "=================================")
message(STATUS "")
