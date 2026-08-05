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
    src/PointerRegistry.cpp
)

target_include_directories(AestraCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Enable profiling in all build types (lightweight enough for Release)
target_compile_definitions(AestraCore PUBLIC
    AESTRA_ENABLE_PROFILING
    AESTRA_ENABLE_MEMORY_PROFILING
)

# =============================================================================
# Tests
# =============================================================================
option(AESTRA_BUILD_TESTS "Build AestraCore tests" ON)

if(AESTRA_BUILD_TESTS)
    # Math Tests
    add_executable(MathTests src/MathTests.cpp)
    target_link_libraries(MathTests PRIVATE AestraCore)
    target_include_directories(MathTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    # Math Benchmark (micro-benchmarks with latency/deadline gates)
    add_executable(MathBenchmark src/MathBenchmark.cpp)
    target_link_libraries(MathBenchmark PRIVATE AestraCore)
    target_include_directories(MathBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    
    # Threading Tests
    add_executable(ThreadingTests src/ThreadingTests.cpp)
    target_link_libraries(ThreadingTests PRIVATE AestraCore)

    # Threading Benchmark (micro-benchmarks with latency/deadline gates)
    add_executable(ThreadingBenchmark src/ThreadingBenchmark.cpp)
    target_link_libraries(ThreadingBenchmark PRIVATE AestraCore)
    
    # File I/O Tests
    add_executable(FileTests src/FileTests.cpp)
    target_link_libraries(FileTests PRIVATE AestraCore)
    
    # Logging Tests
    add_executable(LogTests src/LogTests.cpp)
    target_link_libraries(LogTests PRIVATE AestraCore)
    
    # Config & Assert Tests
    add_executable(ConfigAssertTests src/ConfigAssertTests.cpp)
    target_link_libraries(ConfigAssertTests PRIVATE AestraCore)

    # Memory Profiling Tests
    add_executable(MemoryProfilingTest src/MemoryProfilingTest.cpp)
    target_link_libraries(MemoryProfilingTest PRIVATE AestraCore)
    target_include_directories(MemoryProfilingTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    # Memory Allocator Tests
    add_executable(MemoryAllocatorTest src/MemoryAllocatorTest.cpp)
    target_link_libraries(MemoryAllocatorTest PRIVATE AestraCore AestraAudioCore)
    target_include_directories(MemoryAllocatorTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/DSP
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Drivers
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    if(NOT WIN32)
        target_link_libraries(MemoryAllocatorTest PRIVATE pthread)
    endif()

    enable_testing()
    add_test(NAME MathTests COMMAND MathTests)
    add_test(NAME ThreadingTests COMMAND ThreadingTests)
    add_test(NAME FileTests COMMAND FileTests)
    add_test(NAME LogTests COMMAND LogTests)
    add_test(NAME ConfigAssertTests COMMAND ConfigAssertTests)
    add_test(NAME MemoryProfilingTest COMMAND MemoryProfilingTest)
    add_test(NAME MemoryAllocatorTest COMMAND MemoryAllocatorTest)

    # Contract labels. Foundational utilities: a failure here means configuration,
    # files, logging, threading or allocators may be broken. Exactly one
    # contract:* label per case — descriptive labels are added separately and
    # never satisfy coverage.
    set_tests_properties(
        MathTests
        ThreadingTests
        FileTests
        LogTests
        ConfigAssertTests
        MemoryProfilingTest
        MemoryAllocatorTest
        PROPERTIES LABELS "contract:core"
    )
endif()

message(STATUS "AestraCore configured")
