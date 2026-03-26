cmake_minimum_required(VERSION 3.22)

project(AestraPlat VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# AestraPlat Library
# =============================================================================

# Common sources
set(AESTRA_PLAT_SOURCES
    src/Platform.cpp
)

set(AESTRA_PLAT_HEADERS
    include/AestraPlatform.h
)

set(AESTRA_PLAT_PRIVATE_DEFINITIONS)

# Platform-specific sources
if(WIN32)
    list(APPEND AESTRA_PLAT_SOURCES
        src/Win32/PlatformWindowWin32.cpp
        src/Win32/PlatformUtilsWin32.cpp
        src/Win32/PlatformDPIWin32.cpp
        src/Win32/PlatformThreadWin32.cpp
    )
    list(APPEND AESTRA_PLAT_HEADERS
        src/Win32/PlatformWindowWin32.h
        src/Win32/PlatformUtilsWin32.h
        src/Win32/PlatformDPIWin32.h
    )
elseif(UNIX AND NOT APPLE)
    if(SDL2_FOUND OR TARGET SDL2::SDL2)
        list(APPEND AESTRA_PLAT_SOURCES
            src/Linux/PlatformWindowLinux.cpp
            src/Linux/PlatformUtilsLinux.cpp
            src/Linux/PlatformThreadLinux.cpp
        )
        list(APPEND AESTRA_PLAT_HEADERS
            src/Linux/PlatformWindowLinux.h
        )
        list(APPEND AESTRA_PLAT_PRIVATE_DEFINITIONS AESTRA_HAS_SDL2)
    else()
        message(WARNING "SDL2 not found. AestraPlat Linux windowing disabled.")
    endif()
endif()

# Create library
add_library(AestraPlat STATIC ${AESTRA_PLAT_SOURCES} ${AESTRA_PLAT_HEADERS})

if(AESTRA_PLAT_PRIVATE_DEFINITIONS)
    target_compile_definitions(AestraPlat PRIVATE ${AESTRA_PLAT_PRIVATE_DEFINITIONS})
endif()

target_include_directories(AestraPlat PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(AestraPlat PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../AestraUI/External/glad/include
)

# Link dependencies
target_link_libraries(AestraPlat PUBLIC AestraCore)

# Platform-specific libraries
if(WIN32)
    # Link opengl32 as PRIVATE for WGL functions (wglCreateContext, wglMakeCurrent, etc.)
    # This is needed for OpenGL context creation on Windows
    # Applications using GLAD will get GL functions from GLAD, not from this
    target_link_libraries(AestraPlat PRIVATE
        opengl32
        gdi32
        user32
        comdlg32
        ole32
        shell32
    )
elseif(UNIX AND NOT APPLE)
    if(SDL2_FOUND OR TARGET SDL2::SDL2)
        target_link_libraries(AestraPlat PRIVATE
            SDL2::SDL2
            ${CMAKE_DL_LIBS}
            pthread
        )
    else()
        target_link_libraries(AestraPlat PRIVATE
            ${CMAKE_DL_LIBS}
        )
    endif()
elseif(APPLE)
    # TODO: macOS frameworks (Cocoa, OpenGL, etc.)
endif()

# =============================================================================
# Tests
# =============================================================================
# Tests moved to Tests/ directory - see Tests/CMakeLists.txt
if(FALSE)  # Legacy tests - now centralized in Tests/
option(AESTRA_BUILD_PLAT_TESTS "Build AestraPlat tests" ON)

if(AESTRA_BUILD_PLAT_TESTS)
    # Platform Window Test
    add_executable(PlatformWindowTest src/Tests/PlatformWindowTest.cpp)
    target_link_libraries(PlatformWindowTest PRIVATE AestraPlat)
    
    # Platform DPI Test
    add_executable(PlatformDPITest src/Tests/PlatformDPITest.cpp)
    target_link_libraries(PlatformDPITest PRIVATE AestraPlat)
    
    enable_testing()
    add_test(NAME PlatformWindowTest COMMAND PlatformWindowTest)
    add_test(NAME PlatformDPITest COMMAND PlatformDPITest)
endif()
endif()  # End legacy tests

message(STATUS "AestraPlat configured for ${CMAKE_SYSTEM_NAME}")
