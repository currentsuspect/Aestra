cmake_minimum_required(VERSION 3.22)
project(NomadUI VERSION 0.1.0 LANGUAGES CXX)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Options
# =============================================================================

option(NOMADUI_BUILD_EXAMPLES "Build example applications" OFF)
option(NOMADUI_BUILD_OPENGL "Build OpenGL renderer" ON)
option(NOMADUI_BUILD_VULKAN "Build Vulkan renderer" OFF)
option(NOMADUI_BUILD_TESTS "Build unit tests" OFF)

# =============================================================================
# Core Library
# =============================================================================

set(NOMADUI_CORE_SOURCES
    Core/NUITypes.h
    Core/NUIComponent.h
    Core/NUIComponent.cpp
    Core/NUITheme.h
    Core/NUITheme.cpp
    Core/NUIAnimation.h
    Core/NUIAnimation.cpp
    Core/NUIThemeSystem.h
    Core/NUIThemeSystem.cpp
    Core/NUIAdaptiveFPS.h
    Core/NUIAdaptiveFPS.cpp
    Core/NUIFrameProfiler.h
    Core/NUIFrameProfiler.cpp
    Core/NUIButton.h
    Core/NUIButton.cpp
    Core/NUILabel.h
    Core/NUILabel.cpp
    Core/NUISlider.h
    Core/NUISlider.cpp
    Core/NUICheckbox.h
    Core/NUICheckbox.cpp
    Core/NUITextInput.h
    Core/NUITextInput.cpp
    Core/NUIProgressBar.h
    Core/NUIProgressBar.cpp
    Core/NUIScrollbar.h
    Core/NUIScrollbar.cpp
    Core/NUICustomTitleBar.h
    Core/NUICustomTitleBar.cpp
    Core/NUICustomWindow.h
    Core/NUICustomWindow.cpp
    Core/NUIIcon.h
    Core/NUIIcon.cpp
    # Core/NUIApp.h  # Requires renderer
    # Core/NUIApp.cpp
)

# Widget implementations (compile into core library)
list(APPEND NOMADUI_CORE_SOURCES
    Widgets/NUIDropdown.cpp
    Widgets/NUICoreWidgets.h
    Widgets/NUICoreWidgets.cpp
    Widgets/NUITransportWidgets.h
    Widgets/NUITransportWidgets.cpp
    Widgets/NUIMixerWidgets.h
    Widgets/NUIMixerWidgets.cpp
    Widgets/NUIArrangementWidgets.h
    Widgets/NUIArrangementWidgets.cpp
    Widgets/NUIPianoRollWidgets.h
    Widgets/NUIPianoRollWidgets.cpp
    Widgets/NUIVisualWidgets.h
    Widgets/NUIVisualWidgets.cpp
    Widgets/NUIUtilityWidgets.h
    Widgets/NUIUtilityWidgets.cpp
    Widgets/NUIThematicWidgets.h
    Widgets/NUIThematicWidgets.cpp
)

set(NOMADUI_GRAPHICS_SOURCES
    Graphics/NUIRenderer.h
    Graphics/NUIRenderer.cpp
    Graphics/NUISVGParser.h
    Graphics/NUISVGParser.cpp
    Graphics/NUISVGCache.h
    Graphics/NUISVGCache.cpp
)

# Text rendering removed - using external MSDF text renderer

# Create core library (without text rendering - that's OpenGL-specific)
add_library(NomadUI_Core STATIC
    ${NOMADUI_CORE_SOURCES}
    ${NOMADUI_GRAPHICS_SOURCES}
)

target_include_directories(NomadUI_Core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# FreeType dependency removed - handled by MSDF text renderer

# =============================================================================
# GLAD - OpenGL Loader
# =============================================================================

set(GLAD_SOURCES
    External/glad/src/glad.c
    External/glad/include/glad/glad.h
)

add_library(glad STATIC ${GLAD_SOURCES})
set_target_properties(glad PROPERTIES LINKER_LANGUAGE C)
target_include_directories(glad PUBLIC External/glad/include)

# =============================================================================
# FreeType - Text Rendering
# =============================================================================

include(FetchContent)

message(STATUS "Fetching FreeType...")

FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG VER-2-13-3  # Latest stable version with CMake 4.x support
    GIT_SHALLOW TRUE
)

# Disable unnecessary FreeType features
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(freetype)

message(STATUS "✓ FreeType loaded")

# ============================================================================
# OpenGL Renderer (Optional)
# ============================================================================

if(NOMADUI_BUILD_OPENGL)
    find_package(OpenGL REQUIRED)
    
    # SDL2 is optional - if not found, we'll use GDI fallback
    find_package(SDL2 QUIET)
    find_package(SDL2_ttf QUIET)
    
    set(NOMADUI_OPENGL_SOURCES
        Graphics/OpenGL/NUIRendererGL.h
        Graphics/OpenGL/NUIRendererGL.cpp
        Graphics/OpenGL/NUIRenderBatch.h
        Graphics/OpenGL/NUIRenderBatch.cpp
        Graphics/OpenGL/NUIDirtyRegion.h
        Graphics/OpenGL/NUIDirtyRegion.cpp
        Graphics/OpenGL/NUIRenderCache.h
        Graphics/OpenGL/NUIRenderCache.cpp
    )
    
    add_library(NomadUI_OpenGL STATIC
        ${NOMADUI_OPENGL_SOURCES}
    )
    
    # Link only GLAD for OpenGL - do NOT link OpenGL::GL (opengl32.lib)
    # GLAD provides dynamic loading of all OpenGL functions
    target_link_libraries(NomadUI_OpenGL PUBLIC
        NomadUI_Core
        glad
        freetype
    )
    
    # Add SDL2 libraries if available
    if(SDL2_FOUND AND SDL2_ttf_FOUND)
        target_link_libraries(NomadUI_OpenGL PUBLIC
            SDL2::SDL2
            SDL2_ttf::SDL2_ttf
        )
        target_compile_definitions(NomadUI_OpenGL PUBLIC
            NOMADUI_SDL2_AVAILABLE
        )
    endif()
    
    target_compile_definitions(NomadUI_OpenGL PUBLIC
        NOMADUI_OPENGL
    )
    
    target_include_directories(NomadUI_OpenGL PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# ============================================================================
# Vulkan Renderer (Optional)
# ============================================================================

if(NOMADUI_BUILD_VULKAN)
    find_package(Vulkan REQUIRED)
    
    set(NOMADUI_VULKAN_SOURCES
        Graphics/Vulkan/NUIRendererVK.h
        Graphics/Vulkan/NUIRendererVK.cpp
    )
    
    add_library(NomadUI_Vulkan STATIC
        ${NOMADUI_VULKAN_SOURCES}
    )
    
    target_link_libraries(NomadUI_Vulkan PUBLIC
        NomadUI_Core
        Vulkan::Vulkan
    )
    
    target_include_directories(NomadUI_Vulkan PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# ============================================================================
# Platform Layer (Now uses NomadPlat!)
# ============================================================================

# NomadPlat bridge - unified platform layer
set(NOMADUI_PLATFORM_SOURCES
    Platform/NUIPlatformBridge.h
    Platform/NUIPlatformBridge.cpp
)

# Platform name for logging
if(WIN32)
    set(PLATFORM_NAME "Windows x64")
elseif(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    set(PLATFORM_NAME "macOS Universal (ARM64 + x64)")
elseif(UNIX)
    set(PLATFORM_NAME "Linux x64")
endif()

add_library(NomadUI_Platform STATIC
    ${NOMADUI_PLATFORM_SOURCES}
)

target_include_directories(NomadUI_Platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/External/glad/include
)

target_link_libraries(NomadUI_Platform PUBLIC
    NomadUI_Core
    NomadPlat  # Now using unified platform layer!
)

# ============================================================================
# Main Library (Combines all modules)
# ============================================================================

add_library(NomadUI INTERFACE)

target_link_libraries(NomadUI INTERFACE
    NomadUI_Core
    NomadUI_Platform
)

if(NOMADUI_BUILD_OPENGL)
    target_link_libraries(NomadUI INTERFACE NomadUI_OpenGL)
endif()

if(NOMADUI_BUILD_VULKAN)
    target_link_libraries(NomadUI INTERFACE NomadUI_Vulkan)
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS NomadUI_Core NomadUI_Platform
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY Core/ Graphics/ Platform/
    DESTINATION include/NomadUI
    FILES_MATCHING PATTERN "*.h"
)

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "=================================")
message(STATUS "  Nomad UI Framework")
message(STATUS "=================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")
message(STATUS "Build Options:")
message(STATUS "  Examples: ${NOMADUI_BUILD_EXAMPLES}")
message(STATUS "  OpenGL:   ${NOMADUI_BUILD_OPENGL}")
message(STATUS "  Vulkan:   ${NOMADUI_BUILD_VULKAN}")
message(STATUS "")
message(STATUS "Platform: ${PLATFORM_NAME}")
# CMAKE_SIZEOF_VOID_P is in bytes, multiply by 8 for bits
math(EXPR ARCH_BITS "${CMAKE_SIZEOF_VOID_P} * 8")
message(STATUS "Architecture: ${ARCH_BITS}-bit")
message(STATUS "=================================")
message(STATUS "")
