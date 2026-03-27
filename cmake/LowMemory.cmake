# Low-memory build optimizations for CMake
# Usage: set(AESTRA_LOW_MEMORY_BUILD ON) before including this
# Or use -DAESTRA_LOW_MEMORY_BUILD=ON

if(NOT DEFINED CMAKE_CXX_COMPILER_ID)
    return()
endif()

message(STATUS "Low-memory build optimizations enabled")

# GCC/Clang memory-saving flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Reduce memory usage during compilation
    add_compile_options(
        -fno-ipa-icf
    )
    
    # For GCC specifically - disable vectorization to save memory
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        add_compile_options(-fno-tree-vectorize)
    endif()
    
    # For Clang specifically  
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-fno-vectorize)
    endif()
endif()

# Use -O2 instead of -O3 to reduce memory during compilation
# This only affects the CMAKE_CXX_FLAGS_RELEASE if nothing else sets it
if(NOT CMAKE_CXX_FLAGS_RELEASE MATCHES "-O3")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")
endif()
