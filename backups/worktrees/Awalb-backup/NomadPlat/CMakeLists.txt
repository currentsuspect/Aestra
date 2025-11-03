cmake_minimum_required(VERSION 3.22)

project(NomadPlat VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# NomadPlat Library
# =============================================================================

# Common sources
set(NOMAD_PLAT_SOURCES
    src/Platform.cpp
)

set(NOMAD_PLAT_HEADERS
    include/NomadPlatform.h
)

# Platform-specific sources
if(WIN32)
    list(APPEND NOMAD_PLAT_SOURCES
        src/Win32/PlatformWindowWin32.cpp
        src/Win32/PlatformUtilsWin32.cpp
        src/Win32/PlatformDPIWin32.cpp
    )
    list(APPEND NOMAD_PLAT_HEADERS
        src/Win32/PlatformWindowWin32.h
        src/Win32/PlatformUtilsWin32.h
        src/Win32/PlatformDPIWin32.h
    )
elseif(UNIX AND NOT APPLE)
    # TODO: Linux X11 implementation
    message(WARNING "Linux platform not yet implemented")
elseif(APPLE)
    # TODO: macOS Cocoa implementation
    message(WARNING "macOS platform not yet implemented")
endif()

# Create library
add_library(NomadPlat STATIC ${NOMAD_PLAT_SOURCES} ${NOMAD_PLAT_HEADERS})

target_include_directories(NomadPlat PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link dependencies
target_link_libraries(NomadPlat PUBLIC NomadCore)

# Platform-specific libraries
if(WIN32)
    # Link opengl32 as PRIVATE for WGL functions (wglCreateContext, wglMakeCurrent, etc.)
    # This is needed for OpenGL context creation on Windows
    # Applications using GLAD will get GL functions from GLAD, not from this
    target_link_libraries(NomadPlat PRIVATE
        opengl32
        gdi32
        user32
        comdlg32
        ole32
        shell32
    )
elseif(UNIX AND NOT APPLE)
    # TODO: Linux libraries (X11, GL, etc.)
elseif(APPLE)
    # TODO: macOS frameworks (Cocoa, OpenGL, etc.)
endif()

# =============================================================================
# Tests
# =============================================================================
option(NOMAD_BUILD_PLAT_TESTS "Build NomadPlat tests" ON)

if(NOMAD_BUILD_PLAT_TESTS)
    # Platform Window Test
    add_executable(PlatformWindowTest src/Tests/PlatformWindowTest.cpp)
    target_link_libraries(PlatformWindowTest PRIVATE NomadPlat)
    
    # Platform DPI Test
    add_executable(PlatformDPITest src/Tests/PlatformDPITest.cpp)
    target_link_libraries(PlatformDPITest PRIVATE NomadPlat)
    
    enable_testing()
    add_test(NAME PlatformWindowTest COMMAND PlatformWindowTest)
    add_test(NAME PlatformDPITest COMMAND PlatformDPITest)
endif()

message(STATUS "NomadPlat configured for ${CMAKE_SYSTEM_NAME}")
