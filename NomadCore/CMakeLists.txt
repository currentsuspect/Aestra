cmake_minimum_required(VERSION 3.22)

project(NomadCore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# NomadCore Library
# =============================================================================
add_library(NomadCore STATIC
    src/NomadProfiler.cpp
)

target_include_directories(NomadCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Enable profiling by default in Debug builds
target_compile_definitions(NomadCore PUBLIC
    $<$<CONFIG:Debug>:NOMAD_ENABLE_PROFILING>
    $<$<CONFIG:RelWithDebInfo>:NOMAD_ENABLE_PROFILING>
)

# =============================================================================
# Tests
# =============================================================================
option(NOMAD_BUILD_TESTS "Build NomadCore tests" ON)

if(NOMAD_BUILD_TESTS)
    # Math Tests
    add_executable(MathTests src/MathTests.cpp)
    target_link_libraries(MathTests PRIVATE NomadCore)
    
    # Threading Tests
    add_executable(ThreadingTests src/ThreadingTests.cpp)
    target_link_libraries(ThreadingTests PRIVATE NomadCore)
    
    # File I/O Tests
    add_executable(FileTests src/FileTests.cpp)
    target_link_libraries(FileTests PRIVATE NomadCore)
    
    # Logging Tests
    add_executable(LogTests src/LogTests.cpp)
    target_link_libraries(LogTests PRIVATE NomadCore)
    
    # Config & Assert Tests
    add_executable(ConfigAssertTests src/ConfigAssertTests.cpp)
    target_link_libraries(ConfigAssertTests PRIVATE NomadCore)
    
    enable_testing()
    add_test(NAME MathTests COMMAND MathTests)
    add_test(NAME ThreadingTests COMMAND ThreadingTests)
    add_test(NAME FileTests COMMAND FileTests)
    add_test(NAME LogTests COMMAND LogTests)
    add_test(NAME ConfigAssertTests COMMAND ConfigAssertTests)
endif()

message(STATUS "NomadCore configured")
