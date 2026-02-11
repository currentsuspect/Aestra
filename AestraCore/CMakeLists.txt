cmake_minimum_required(VERSION 3.22)

project(AestraCore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# AestraCore Library
# =============================================================================
add_library(AestraCore STATIC
    src/AestraProfiler.cpp
    src/AestraUnifiedProfiler.cpp
    src/AestraThreading.cpp
    src/PointerRegistry.cpp
)

target_include_directories(AestraCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Enable profiling in all build types (lightweight enough for Release)
target_compile_definitions(AestraCore PUBLIC
    Aestra_ENABLE_PROFILING
)

# =============================================================================
# Tests
# =============================================================================
option(AESTRA_BUILD_TESTS "Build AestraCore tests" ON)

if(AESTRA_BUILD_TESTS)
    # Math Tests
    add_executable(MathTests src/MathTests.cpp)
    target_link_libraries(MathTests PRIVATE AestraCore)
    
    # Threading Tests
    add_executable(ThreadingTests src/ThreadingTests.cpp)
    target_link_libraries(ThreadingTests PRIVATE AestraCore)
    
    # File I/O Tests
    add_executable(FileTests src/FileTests.cpp)
    target_link_libraries(FileTests PRIVATE AestraCore)
    
    # Logging Tests
    add_executable(LogTests src/LogTests.cpp)
    target_link_libraries(LogTests PRIVATE AestraCore)
    
    # Config & Assert Tests
    add_executable(ConfigAssertTests src/ConfigAssertTests.cpp)
    target_link_libraries(ConfigAssertTests PRIVATE AestraCore)
    
    enable_testing()
    add_test(NAME MathTests COMMAND MathTests)
    add_test(NAME ThreadingTests COMMAND ThreadingTests)
    add_test(NAME FileTests COMMAND FileTests)
    add_test(NAME LogTests COMMAND LogTests)
    add_test(NAME ConfigAssertTests COMMAND ConfigAssertTests)
endif()

message(STATUS "AestraCore configured")
