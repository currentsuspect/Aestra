cmake_minimum_required(VERSION 3.22)

project(AestraPlugins LANGUAGES CXX)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraRumble/CMakeLists.txt")
    add_subdirectory(AestraRumble)
endif()
