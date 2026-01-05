cmake_minimum_required(VERSION 3.22)

# Use consistent CRT runtime across all targets (must be set before project())
if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Optional: auto core-mode helper
if(EXISTS "${CMAKE_SOURCE_DIR}/nomad-core/cmake/NomadCoreMode.cmake")
	include(${CMAKE_SOURCE_DIR}/nomad-core/cmake/NomadCoreMode.cmake)
endif()

project(NOMAD VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Build Options
# =============================================================================

# Headless-only mode is intended for CI/containers: configure & build without UI.
option(NOMAD_HEADLESS_ONLY "Configure/build headless targets only (skips UI + app)" OFF)
option(NOMAD_ENABLE_UI "Build the NOMAD_DAW UI application" ON)
option(NOMAD_ENABLE_TESTS "Build test executables" ON)

if(NOMAD_HEADLESS_ONLY)
  set(NOMAD_ENABLE_UI OFF CACHE BOOL "Build the NOMAD_DAW UI application" FORCE)
  set(NOMAD_ENABLE_TESTS OFF CACHE BOOL "Build test executables" FORCE)
endif()

# =============================================================================
# Platform Detection & Flags
# =============================================================================
if(UNIX AND NOT APPLE)
  set(NOMAD_PLATFORM_LINUX ON)
  add_compile_definitions(NOMAD_PLATFORM_LINUX=1)

  # Detect distribution for logging/diagnostics
  find_program(LSB_RELEASE lsb_release)
  if(LSB_RELEASE)
    execute_process(COMMAND ${LSB_RELEASE} -is OUTPUT_VARIABLE LINUX_DISTRO OUTPUT_STRIP_TRAILING_WHITESPACE)
  else()
    set(LINUX_DISTRO "Unknown")
  endif()
  message(STATUS "Detected Linux platform (${LINUX_DISTRO})")

  target_compile_options(NomadCore PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
  )
  
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
if(NOMAD_PLATFORM_LINUX)
  option(RTAUDIO_API_ALSA "Compile with ALSA support" ON)
  option(RTAUDIO_API_PULSE "Compile with PulseAudio support" ON)
  option(RTAUDIO_API_JACK "Compile with JACK support" OFF)
  
  if(EXISTS "${CMAKE_SOURCE_DIR}/external/rtaudio/CMakeLists.txt")
    add_subdirectory(external/rtaudio EXCLUDE_FROM_ALL)
    # create alias for consistency if needed, though target_link_libraries usually works with rtaudio directly
  else()
    message(STATUS "external/rtaudio missing. Audio backend will not build.")
  endif()
endif()

# =============================================================================
# NomadCore - Base Utilities
# =============================================================================
add_subdirectory(NomadCore)

# =============================================================================
# NomadAudio - Audio Engine
# =============================================================================
add_subdirectory(NomadAudio)

# =============================================================================
# NomadHeadless - Offline engine smoke test (no UI/OpenGL)
# =============================================================================

if(EXISTS "${CMAKE_SOURCE_DIR}/Source/HeadlessMain.cpp")
  add_executable(NomadHeadless
    Source/HeadlessMain.cpp
    Source/ProjectSerializer.cpp
  )

  if(MSVC)
    target_compile_options(NomadHeadless PRIVATE /utf-8)
  endif()

  set_target_properties(NomadHeadless PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  )

  target_link_libraries(NomadHeadless PRIVATE
    NomadAudioCore
    NomadCore
  )

  target_include_directories(NomadHeadless PRIVATE
    ${CMAKE_SOURCE_DIR}/NomadAudio/include
    ${CMAKE_SOURCE_DIR}/NomadCore/include
    ${CMAKE_SOURCE_DIR}/Source
  )

  if(WIN32)
    target_compile_definitions(NomadHeadless PRIVATE
      _CRT_SECURE_NO_WARNINGS
      NOMINMAX
    )
  endif()

  message(STATUS "NomadHeadless configured")
endif()

if(NOMAD_ENABLE_UI)
  # =============================================================================
  # NomadPlat - Platform Abstraction
  # =============================================================================
  add_subdirectory(NomadPlat)

  # =============================================================================
  # NomadUI - UI Framework
  # =============================================================================
  add_subdirectory(NomadUI)
endif()

# =============================================================================
# NomadPlugins - Premium Plugins (Optional Private Submodule)
# =============================================================================
if(EXISTS "${CMAKE_SOURCE_DIR}/NomadPlugins/CMakeLists.txt")
    message(STATUS "Found NomadPlugins - including premium plugins")
    add_subdirectory(NomadPlugins)
    set(NOMAD_HAS_PLUGINS ON)
else()
    message(STATUS "NomadPlugins not found - skipping premium plugins")
    set(NOMAD_HAS_PLUGINS OFF)
endif()

# =============================================================================
# NOMAD DAW - Main Application
# =============================================================================
if(NOMAD_ENABLE_UI)
  add_subdirectory(Source)
endif()

# =============================================================================
# Test Executables
# =============================================================================
if(NOMAD_ENABLE_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/Tests/VST3PluginTest.cpp")
    add_executable(VST3PluginTest Tests/VST3PluginTest.cpp)
    target_link_libraries(VST3PluginTest PRIVATE
        NomadAudioCore
        NomadCore
    )
    target_include_directories(VST3PluginTest PRIVATE
        ${CMAKE_SOURCE_DIR}/NomadAudio/include
        ${CMAKE_SOURCE_DIR}/NomadCore/include
    )
    # VST3 SDK integration
    set(VST3_SDK_PATH "${CMAKE_SOURCE_DIR}/NomadAudio/External/vst3sdk")
    if(EXISTS "${VST3_SDK_PATH}/pluginterfaces")
        target_compile_definitions(VST3PluginTest PRIVATE NOMAD_HAS_VST3)
        target_include_directories(VST3PluginTest PRIVATE
            ${VST3_SDK_PATH}
            ${VST3_SDK_PATH}/pluginterfaces
            ${VST3_SDK_PATH}/public.sdk
            ${VST3_SDK_PATH}/public.sdk/source
            ${VST3_SDK_PATH}/base
        )
    endif()
    if(WIN32)
        target_link_libraries(VST3PluginTest PRIVATE ole32)
    endif()
endif()

  # Project roundtrip smoke test (create -> save -> load -> verify)
  if(NOMAD_ENABLE_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/Tests/ProjectRoundTripTest.cpp")
    add_executable(ProjectRoundTripTest
      Tests/ProjectRoundTripTest.cpp
      Source/ProjectSerializer.cpp
    )
    target_link_libraries(ProjectRoundTripTest PRIVATE
      NomadAudio
      NomadCore
    )
    target_include_directories(ProjectRoundTripTest PRIVATE
      ${CMAKE_SOURCE_DIR}/NomadAudio/include
      ${CMAKE_SOURCE_DIR}/NomadCore/include
      ${CMAKE_SOURCE_DIR}/Source
    )

    # Optional load-stage tracing for debugging serializer issues.
    target_compile_definitions(ProjectRoundTripTest PRIVATE
      $<$<CONFIG:Debug>:NOMAD_ENABLE_PROJECT_LOAD_LOGS>
    )

    enable_testing()
    add_test(NAME ProjectRoundTripTest COMMAND ProjectRoundTripTest)
    # Alias name to make autosave-path coverage obvious in CI output.
    add_test(NAME AutosaveRoundTripTest COMMAND ProjectRoundTripTest)
  endif()

  # Plugin Scanner verification test
  if(NOMAD_ENABLE_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/Tests/PluginScanTest.cpp")
    add_executable(NomadPluginScanTest
      Tests/PluginScanTest.cpp
    )
    target_link_libraries(NomadPluginScanTest PRIVATE
      NomadAudio
      NomadCore
    )
    target_include_directories(NomadPluginScanTest PRIVATE
      ${CMAKE_SOURCE_DIR}/NomadAudio/include
      ${CMAKE_SOURCE_DIR}/NomadCore/include
    )
    if(WIN32)
        target_link_libraries(NomadPluginScanTest PRIVATE ole32)
    endif()
  endif()

message(STATUS "")
message(STATUS "=================================")
message(STATUS "  NOMAD DAW - Pure Native")
message(STATUS "=================================")
message(STATUS "UI: NomadUI (OpenGL)")
message(STATUS "Audio: NomadAudio (RtAudio)")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "=================================")
message(STATUS "")
