# Include core-mode helper if available
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/cmake/NomadCoreMode.cmake")
	include(${CMAKE_CURRENT_LIST_DIR}/cmake/NomadCoreMode.cmake)
endif()
