# NomadMuse - Local AI Integration Module
cmake_minimum_required(VERSION 3.15)

project(NomadMuse VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(NOMAD_MUSE_SOURCES
    # src/AIEngine.cpp
    # Add implementation files as they are created
)

# Header files
set(NOMAD_MUSE_HEADERS
    # include/NomadMuse/AIEngine.h
    # Add header files as they are created
)

# Create library (currently empty, will be populated)
# add_library(NomadMuse ${NOMAD_MUSE_SOURCES} ${NOMAD_MUSE_HEADERS})

# Include directories
# target_include_directories(NomadMuse
#     PUBLIC
#         $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
#         $<INSTALL_INTERFACE:include>
#     PRIVATE
#         ${CMAKE_CURRENT_SOURCE_DIR}/src
# )

# Dependencies will be added here as needed
# Examples:
# - ONNX Runtime for model inference
# - TensorFlow Lite
# - PyTorch C++ API
# - Custom AI framework

# Testing
# enable_testing()
# add_subdirectory(test)

# Installation rules
# install(TARGETS NomadMuse
#     LIBRARY DESTINATION lib
#     ARCHIVE DESTINATION lib
#     RUNTIME DESTINATION bin
# )

# install(DIRECTORY include/
#     DESTINATION include
# )
