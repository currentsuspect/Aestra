# CMake helper: define NOMAD_CORE_ONLY when premium folders are absent

if(NOT DEFINED NOMAD_PREMIUM_DIR)
    set(NOMAD_PREMIUM_DIR "${CMAKE_SOURCE_DIR}/NomadMuse" CACHE PATH "Path to Nomad premium modules")
endif()

if(NOT EXISTS "${NOMAD_PREMIUM_DIR}")
    message(STATUS "Nomad premium directory not found; enabling NOMAD_CORE_ONLY fallback")
    add_compile_definitions(NOMAD_CORE_ONLY)
    # Point to mock assets so builds still succeed
    set(NOMAD_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets_mock" CACHE PATH "Path to mock assets used when premium missing")
else()
    message(STATUS "Nomad premium directory found: ${NOMAD_PREMIUM_DIR}")
endif()
