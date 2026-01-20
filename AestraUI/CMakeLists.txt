if(${CMAKE_VERSION} VERSION_LESS "3.22")
    message(FATAL_ERROR "AestraUI requires CMake >= 3.22, but found ${CMAKE_VERSION}. Please upgrade your CMake or adjust the top-level cmake_minimum_required.")
endif()
cmake_minimum_required(VERSION 3.22)
project(AestraUI VERSION 0.1.0 LANGUAGES CXX)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Options
# =============================================================================

option(AESTRAUI_BUILD_EXAMPLES "Build example applications" OFF)
option(AESTRAUI_BUILD_OPENGL "Build OpenGL renderer" ON)
option(AESTRAUI_BUILD_VULKAN "Build Vulkan renderer" OFF)
option(AESTRAUI_BUILD_TESTS "Build unit tests" OFF)

# =============================================================================
# Core Library
# =============================================================================

set(AESTRAUI_CORE_SOURCES
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
    Core/NUIDragDrop.h
    Core/NUIDragDrop.cpp
    Core/NUIConfigLoader.h
    Core/NUIConfigLoader.cpp
    Core/NUITitleBarConfig.h
    Core/NUITitleBarConfig.cpp
    Core/NUIApp.h
    Core/NUIApp.cpp
    Core/stb_image_impl.cpp
    Core/NUISliderConfig.cpp
    Common/MusicHelpers.h
    Common/MusicHelpers.cpp

    # Base generic widgets
    Base/NUIButton.h
    Base/NUIButton.cpp
    Base/NUICheckbox.h
    Base/NUICheckbox.cpp
    Base/NUIDropdown.h
    Base/NUIDropdown.cpp
    Base/NUISlider.h
    Base/NUISlider.cpp
    Base/NUITextInput.h
    Base/NUITextInput.cpp
    Base/NUILabel.h
    Base/NUILabel.cpp
    Base/NUIProgressBar.h
    Base/NUIProgressBar.cpp
    Base/NUIScrollbar.h
    Base/NUIScrollbar.cpp
    Base/NUIIcon.h
    Base/NUIIcon.cpp
    Base/NUIContextMenu.h
    Base/NUIContextMenu.cpp
    Base/NUICustomWindow.h
    Base/NUICustomWindow.cpp
    Base/NUICustomTitleBar.h
    Base/NUICustomTitleBar.cpp
    Base/NUIMenuBar.h
    Base/NUISegmentedControl.h
)

# Widget implementations (compile into core library)
list(APPEND AESTRAUI_CORE_SOURCES
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
    Widgets/NUIStepSequencer.h
    Widgets/NUIStepSequencer.cpp
    Widgets/UIMixerMeter.h
    Widgets/UIMixerMeter.cpp
    Widgets/UIMixerPanel.h
    Widgets/UIMixerPanel.cpp
    Widgets/UIMixerFader.h
    Widgets/UIMixerFader.cpp
    Widgets/UIMixerButtonRow.h
    Widgets/UIMixerButtonRow.cpp
    Widgets/UIMixerHeader.h
    Widgets/UIMixerHeader.cpp
    Widgets/UIMixerKnob.h
    Widgets/UIMixerKnob.cpp
    Widgets/UIMixerFXSummary.h
    Widgets/UIMixerFXSummary.cpp
    Widgets/UIMixerStrip.h
    Widgets/UIMixerStrip.cpp
    Widgets/UIMixerFooter.h
    Widgets/UIMixerFooter.cpp
    Widgets/UIMixerInspector.h
    Widgets/UIMixerInspector.cpp
    Widgets/UnitRow.h
    Widgets/UnitRow.cpp
    Widgets/UnitColorPicker.h
    Widgets/UnitColorPicker.cpp
    Widgets/UIItemSelector.h
    Widgets/UIItemSelector.cpp
    Widgets/PluginBrowserPanel.h
    Widgets/PluginBrowserPanel.cpp
    Widgets/PluginUIController.h
    Widgets/PluginUIController.cpp
    Widgets/PluginSelectorMenu.h
    Widgets/PluginSelectorMenu.cpp
    Widgets/GenericPluginEditor.h
    Widgets/GenericPluginEditor.cpp
)

set(AESTRAUI_GRAPHICS_SOURCES
    Graphics/NUIRenderer.h
    Graphics/NUIRenderer.cpp
    Graphics/NUISVGParser.h
    Graphics/NUISVGParser.cpp
    Graphics/NUISVGCache.h
    Graphics/NUISVGCache.cpp
)

# Text rendering removed - using external MSDF text renderer

# Create core library (without text rendering - that's OpenGL-specific)
add_library(AestraUI_Core STATIC
    ${AESTRAUI_CORE_SOURCES}
    ${AESTRAUI_GRAPHICS_SOURCES}
)

target_include_directories(AestraUI_Core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/Base
    ${CMAKE_CURRENT_SOURCE_DIR}/Widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/Graphics
    ${CMAKE_CURRENT_SOURCE_DIR}/Common
)

# The main application mixer widgets depend on AestraAudio + AestraCore headers.
# Keep this PRIVATE so AestraUI can still be consumed as a standalone library
# when these widgets are not used.
target_include_directories(AestraUI_Core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../AestraCore/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../AestraAudio/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../AestraAudio/include/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/../AestraAudio/include/DSP
    ${CMAKE_CURRENT_SOURCE_DIR}/../AestraAudio/include/Models
    ${CMAKE_CURRENT_SOURCE_DIR}/../AestraAudio/include/Playback
    ${CMAKE_CURRENT_SOURCE_DIR}/../AestraAudio/include/IO
    ${CMAKE_CURRENT_SOURCE_DIR}/../AestraAudio/include/Drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/../AestraAudio/include/Plugin
    ${CMAKE_CURRENT_SOURCE_DIR}/../AestraAudio/include/Commands
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/App
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/Panels
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/Settings
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/Components
    ${CMAKE_CURRENT_SOURCE_DIR}/../Source/HUD
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

# Use local freetype (manual download fallback)
message(STATUS "Using local FreeType from External/freetype_local...")

# Disable unnecessary FreeType features
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)

# Suppress deprecation warnings from the dependency's CMakeLists
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
add_subdirectory(External/freetype_local)
set(CMAKE_WARN_DEPRECATED ON CACHE BOOL "" FORCE)

message(STATUS "✓ FreeType loaded")

# ============================================================================
# OpenGL Renderer (Optional)
# ============================================================================

if(AESTRAUI_BUILD_OPENGL)
    find_package(OpenGL REQUIRED)
    
    # SDL2 is optional - if not found, we'll use GDI fallback
    find_package(SDL2 QUIET)
    find_package(SDL2_ttf QUIET)
    
    set(AESTRAUI_OPENGL_SOURCES
        Graphics/OpenGL/NUIRendererGL.h
        Graphics/OpenGL/NUIRendererGL.cpp
        Graphics/OpenGL/NUIRenderBatch.h
        Graphics/OpenGL/NUIRenderBatch.cpp
        Graphics/OpenGL/NUIDirtyRegion.h
        Graphics/OpenGL/NUIDirtyRegion.cpp
        Graphics/OpenGL/NUIRenderCache.h
        Graphics/OpenGL/NUIRenderCache.cpp
        Graphics/NUITextRendererSDF.h
        Graphics/NUITextRendererSDF.cpp
        Graphics/GlassmorphismPass.h
        Graphics/GlassmorphismPass.cpp
    )
    
    add_library(AestraUI_OpenGL STATIC
        ${AESTRAUI_OPENGL_SOURCES}
    )
    
    # Link only GLAD for OpenGL - do NOT link OpenGL::GL (opengl32.lib)
    # GLAD provides dynamic loading of all OpenGL functions
    target_link_libraries(AestraUI_OpenGL PUBLIC
        AestraUI_Core
        glad
        freetype
    )
    
    # Add SDL2 libraries if available
    if(SDL2_FOUND AND SDL2_ttf_FOUND)
        target_link_libraries(AestraUI_OpenGL PUBLIC
            SDL2::SDL2
            SDL2_ttf::SDL2_ttf
        )
        target_compile_definitions(AestraUI_OpenGL PUBLIC
            AestraUI_SDL2_AVAILABLE
        )
    endif()
    
    target_compile_definitions(AestraUI_OpenGL PUBLIC
        AestraUI_OPENGL
    )
    
    target_include_directories(AestraUI_OpenGL PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# ============================================================================
# Vulkan Renderer (Optional)
# ============================================================================

if(AESTRAUI_BUILD_VULKAN)
    find_package(Vulkan REQUIRED)
    
    set(AESTRAUI_VULKAN_SOURCES
        Graphics/Vulkan/NUIRendererVK.h
        Graphics/Vulkan/NUIRendererVK.cpp
    )
    
    add_library(AestraUI_Vulkan STATIC
        ${AESTRAUI_VULKAN_SOURCES}
    )
    
    target_link_libraries(AestraUI_Vulkan PUBLIC
        AestraUI_Core
        Vulkan::Vulkan
    )
    
    target_include_directories(AestraUI_Vulkan PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# ============================================================================
# Platform Layer (Now uses AestraPlat!)
# ============================================================================

# AestraPlat bridge - unified platform layer
set(AESTRAUI_PLATFORM_SOURCES
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

add_library(AestraUI_Platform STATIC
    ${AESTRAUI_PLATFORM_SOURCES}
)

target_include_directories(AestraUI_Platform PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/External/glad/include
)

target_link_libraries(AestraUI_Platform PUBLIC
    AestraUI_Core
    AestraPlat  # Now using unified platform layer!
)

# ============================================================================
# Main Library (Combines all modules)
# ============================================================================

add_library(AestraUI INTERFACE)

target_link_libraries(AestraUI INTERFACE
    AestraUI_Core
    AestraUI_Platform
)

if(AESTRAUI_BUILD_OPENGL)
    target_link_libraries(AestraUI INTERFACE AestraUI_OpenGL)
endif()

if(AESTRAUI_BUILD_VULKAN)
    target_link_libraries(AestraUI INTERFACE AestraUI_Vulkan)
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS AestraUI_Core AestraUI_Platform
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY Core/ Graphics/ Platform/
    DESTINATION include/AestraUI
    FILES_MATCHING PATTERN "*.h"
)

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "=================================")
message(STATUS "  Aestra UI Framework")
message(STATUS "=================================")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")
message(STATUS "Build Options:")
message(STATUS "  Examples: ${AESTRAUI_BUILD_EXAMPLES}")
message(STATUS "  OpenGL:   ${AESTRAUI_BUILD_OPENGL}")
message(STATUS "  Vulkan:   ${AESTRAUI_BUILD_VULKAN}")
message(STATUS "")
message(STATUS "Platform: ${PLATFORM_NAME}")
# CMAKE_SIZEOF_VOID_P is in bytes, multiply by 8 for bits
math(EXPR ARCH_BITS "${CMAKE_SIZEOF_VOID_P} * 8")
message(STATUS "Architecture: ${ARCH_BITS}-bit")
message(STATUS "=================================")
message(STATUS "")
