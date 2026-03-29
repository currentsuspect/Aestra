# CMake helper: define Aestra_CORE_ONLY when premium folders are absent

if(NOT DEFINED Aestra_PREMIUM_DIR)
    set(Aestra_PREMIUM_DIR "${CMAKE_SOURCE_DIR}/AestraMuse" CACHE PATH "Path to Aestra premium modules")
endif()

if(NOT EXISTS "${Aestra_PREMIUM_DIR}")
    message(STATUS "Aestra premium directory not found; enabling Aestra_CORE_ONLY fallback")
    add_compile_definitions(Aestra_CORE_ONLY)
    # Point to mock assets so builds still succeed
    set(Aestra_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets_mock" CACHE PATH "Path to mock assets used when premium missing")
else()
    message(STATUS "Aestra premium directory found: ${Aestra_PREMIUM_DIR}")
endif()
