# NomadCoreMode.cmake
# Include from your root CMakeLists.txt early:
#   include(${CMAKE_SOURCE_DIR}/nomad-core/cmake/NomadCoreMode.cmake)

option(NOMAD_CORE_MODE "Build without premium modules and private assets" ON)

# Detect premium directory adjacent to core
set(NOMAD_PREMIUM_DIR "${CMAKE_SOURCE_DIR}/../nomad-premium")
if(EXISTS "${NOMAD_PREMIUM_DIR}/CMakeLists.txt")
	set(HAVE_NOMAD_PREMIUM ON)
else()
	set(HAVE_NOMAD_PREMIUM OFF)
endif()

# Auto-toggle: if premium not found, force core mode ON
if(NOT HAVE_NOMAD_PREMIUM)
	set(NOMAD_CORE_MODE ON CACHE BOOL "Build without premium" FORCE)
endif()

# Assets: prefer mock in core mode
set(NOMAD_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets_mock")
if(NOT EXISTS "${NOMAD_ASSETS_DIR}")
	file(MAKE_DIRECTORY "${NOMAD_ASSETS_DIR}")
endif()

# Consumers can use these:
#  NOMAD_CORE_MODE, HAVE_NOMAD_PREMIUM, NOMAD_PREMIUM_DIR, NOMAD_ASSETS_DIR
