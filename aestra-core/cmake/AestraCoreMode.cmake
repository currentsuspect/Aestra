# AestraCoreMode.cmake
# Include from your root CMakeLists.txt early:
#   include(${CMAKE_SOURCE_DIR}/Aestra-core/cmake/AestraCoreMode.cmake)

option(Aestra_CORE_MODE "Build without premium modules and private assets" ON)

# Detect premium directory adjacent to core
set(Aestra_PREMIUM_DIR "${CMAKE_SOURCE_DIR}/../Aestra-premium")
if(EXISTS "${Aestra_PREMIUM_DIR}/CMakeLists.txt")
	set(HAVE_Aestra_PREMIUM ON)
else()
	set(HAVE_Aestra_PREMIUM OFF)
endif()

# Auto-toggle: if premium not found, force core mode ON
if(NOT HAVE_Aestra_PREMIUM)
	set(Aestra_CORE_MODE ON CACHE BOOL "Build without premium" FORCE)
endif()

# Assets: prefer mock in core mode
set(Aestra_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets_mock")
if(NOT EXISTS "${Aestra_ASSETS_DIR}")
	file(MAKE_DIRECTORY "${Aestra_ASSETS_DIR}")
endif()

# Consumers can use these:
#  Aestra_CORE_MODE, HAVE_Aestra_PREMIUM, Aestra_PREMIUM_DIR, Aestra_ASSETS_DIR
