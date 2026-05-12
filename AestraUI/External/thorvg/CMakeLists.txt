# ThorVG — vendored build for Aestra
# CPU-only, SVG-only, no threads, no file IO

cmake_minimum_required(VERSION 3.22)

add_library(thorvg STATIC)

target_compile_definitions(thorvg PRIVATE TVG_STATIC)

target_include_directories(thorvg
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/common>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/renderer>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/cpu_engine>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/loaders/svg>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/loaders/raw>
)

target_sources(thorvg PRIVATE
    # Public header
    inc/thorvg.h

    # Common
    src/common/tvgColor.cpp
    src/common/tvgCompressor.cpp
    src/common/tvgMath.cpp
    src/common/tvgStr.cpp

    # Renderer core
    src/renderer/tvgAccessor.cpp
    src/renderer/tvgAnimation.cpp
    src/renderer/tvgCanvas.cpp
    src/renderer/tvgFill.cpp
    src/renderer/tvgInitializer.cpp
    src/renderer/tvgLoaderMgr.cpp
    src/renderer/tvgPaint.cpp
    src/renderer/tvgPicture.cpp
    src/renderer/tvgRender.cpp
    src/renderer/tvgSaver.cpp
    src/renderer/tvgScene.cpp
    src/renderer/tvgShape.cpp
    src/renderer/tvgTaskScheduler.cpp
    src/renderer/tvgText.cpp

    # CPU engine (software rasterizer)
    src/renderer/cpu_engine/tvgSwFill.cpp
    src/renderer/cpu_engine/tvgSwImage.cpp
    src/renderer/cpu_engine/tvgSwMath.cpp
    src/renderer/cpu_engine/tvgSwMemPool.cpp
    src/renderer/cpu_engine/tvgSwPostEffect.cpp
    src/renderer/cpu_engine/tvgSwRaster.cpp
    src/renderer/cpu_engine/tvgSwRenderer.cpp
    src/renderer/cpu_engine/tvgSwRle.cpp
    src/renderer/cpu_engine/tvgSwShape.cpp
    src/renderer/cpu_engine/tvgSwStroke.cpp

    # SVG loader
    src/loaders/svg/tvgSvgBuilder.cpp
    src/loaders/svg/tvgSvgCssStyle.cpp
    src/loaders/svg/tvgSvgLoader.cpp
    src/loaders/svg/tvgSvgPath.cpp
    src/loaders/svg/tvgSvgUtil.cpp
    src/loaders/svg/tvgXmlParser.cpp

    # Raw loader
    src/loaders/raw/tvgRawLoader.cpp
)

# Compiler flags aligned with ThorVG upstream expectations
if(NOT MSVC)
    target_compile_options(thorvg PRIVATE
        -Wno-unknown-pragmas
        -Wno-sized-deallocation
        -fno-exceptions
        -fno-rtti
    )
endif()

# Threading — ThorVG may still reference pthread symbols for mutex stubs
# even when THORVG_THREAD_SUPPORT is disabled.
find_package(Threads)
if(Threads_FOUND)
    target_link_libraries(thorvg PRIVATE Threads::Threads)
endif()

# C++14 minimum (ThorVG upstream requirement)
target_compile_features(thorvg PUBLIC cxx_std_14)
set_target_properties(thorvg PROPERTIES CXX_EXTENSIONS OFF)
