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

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# NomadCore - Base Utilities
# =============================================================================
add_subdirectory(NomadCore)

# =============================================================================
# NomadPlat - Platform Abstraction
# =============================================================================
add_subdirectory(NomadPlat)

# =============================================================================
# NomadUI - UI Framework
# =============================================================================
add_subdirectory(NomadUI)

# =============================================================================
# NomadAudio - Audio Engine
# =============================================================================
add_subdirectory(NomadAudio)

# =============================================================================
# NOMAD DAW - Main Application
# =============================================================================
add_subdirectory(Source)

message(STATUS "")
message(STATUS "=================================")
message(STATUS "  NOMAD DAW - Pure Native")
message(STATUS "=================================")
message(STATUS "UI: NomadUI (OpenGL)")
message(STATUS "Audio: NomadAudio (RtAudio)")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "=================================")
message(STATUS "")
