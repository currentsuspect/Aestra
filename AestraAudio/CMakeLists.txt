cmake_minimum_required(VERSION 3.22)

project(AestraAudio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prevent Windows min/max macros from conflicting with std::min/std::max
if(WIN32)
    add_definitions(-DNOMINMAX)
endif()

# =============================================================================
# RtAudio Integration (MIT License)
# =============================================================================

# RtAudio source files
set(RTAUDIO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External/rtaudio)

set(RTAUDIO_SOURCES
    ${RTAUDIO_DIR}/RtAudio.cpp
)

set(RTAUDIO_HEADERS
    ${RTAUDIO_DIR}/RtAudio.h
)

# Platform-specific audio APIs
if(WIN32)
    # Windows: WASAPI only (stable, universally available)
    add_definitions(-D__WINDOWS_WASAPI__)
    set(RTAUDIO_LIBS dsound ole32 winmm ksuser mfplat mfuuid wmcodecdspuuid mfreadwrite)

elseif(APPLE)
    # macOS: CoreAudio
    add_definitions(-D__MACOSX_CORE__)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    set(RTAUDIO_LIBS ${COREAUDIO_LIBRARY} ${COREFOUNDATION_LIBRARY})

elseif(UNIX)
    # Linux: ALSA (primary), JACK (optional), PulseAudio (optional)
    add_definitions(-D__LINUX_ALSA__)
    find_package(ALSA QUIET)

    if(ALSA_FOUND)
        set(RTAUDIO_LIBS ${ALSA_LIBRARIES} pthread)
    else()
        message(WARNING "ALSA not found. Audio backend will not function.")
    endif()

    # Optional JACK support
    # add_definitions(-D__UNIX_JACK__)
    # find_package(Jack)
    # if(JACK_FOUND)
    #     set(RTAUDIO_LIBS ${RTAUDIO_LIBS} ${JACK_LIBRARIES})
    # endif()
endif()

# =============================================================================
# VST3 SDK Integration (MIT License)
# =============================================================================

set(VST3_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External/vst3sdk)

if(EXISTS ${VST3_SDK_DIR}/pluginterfaces AND EXISTS ${VST3_SDK_DIR}/public.sdk)
    set(AESTRA_HAS_VST3 ON)
    message(STATUS "VST3 SDK: Found at ${VST3_SDK_DIR}")
    
    # VST3 SDK include directories
    set(VST3_INCLUDE_DIRS
        ${VST3_SDK_DIR}
        ${VST3_SDK_DIR}/pluginterfaces
        ${VST3_SDK_DIR}/public.sdk
        ${VST3_SDK_DIR}/public.sdk/source
        ${VST3_SDK_DIR}/base
    )
    
    # VST3 hosting sources (minimal set for plugin hosting)
    set(VST3_HOST_SOURCES
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
        ${VST3_SDK_DIR}/public.sdk/source/common/memorystream.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/utility/stringconvert.cpp
        ${VST3_SDK_DIR}/public.sdk/source/common/commonstringconvert.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstinitiids.cpp
        ${VST3_SDK_DIR}/base/source/fdebug.cpp
        ${VST3_SDK_DIR}/base/source/fobject.cpp
        ${VST3_SDK_DIR}/base/source/fstring.cpp
        ${VST3_SDK_DIR}/pluginterfaces/base/funknown.cpp
        ${VST3_SDK_DIR}/pluginterfaces/base/ustring.cpp
        ${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp
    )
    
    if(WIN32)
        list(APPEND VST3_HOST_SOURCES
            ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
        )
    elseif(APPLE)
        list(APPEND VST3_HOST_SOURCES
            ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm
        )
    else()
        list(APPEND VST3_HOST_SOURCES
            ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_linux.cpp
        )
    endif()
else()
    set(AESTRA_HAS_VST3 OFF)
    message(STATUS "VST3 SDK: Not found - plugin hosting disabled")
endif()

# =============================================================================
# CLAP SDK Integration (MIT License)
# =============================================================================

set(CLAP_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External/clap)

if(EXISTS ${CLAP_SDK_DIR}/include/clap/clap.h)
    set(AESTRA_HAS_CLAP ON)
    message(STATUS "CLAP SDK: Found at ${CLAP_SDK_DIR}")
    
    # CLAP SDK is header-only, just need include path
    set(CLAP_INCLUDE_DIRS
        ${CLAP_SDK_DIR}/include
    )
else()
    set(AESTRA_HAS_CLAP OFF)
    message(STATUS "CLAP SDK: Not found")
endif()

# =============================================================================
# AestraAudio Library
# =============================================================================

# =============================================================================
# AestraAudioCore (Platform-Agnostic)
# =============================================================================

# Core sources (NO platform-specific code)
# Organized into subdirectories for maintainability
set(AESTRA_AUDIO_CORE_SOURCES
    # Core
    src/Core/AudioEngine.cpp
    src/Core/AudioDeviceManager.cpp
    src/Core/MixerBus.cpp
    src/Core/MixerChannel.cpp
    
    # Playback
    src/Playback/AuditionEngine.cpp
    src/Playback/PreviewEngine.cpp
    src/Playback/PatternPlaybackEngine.cpp
    src/Playback/MetronomeEngine.cpp
    src/Playback/TimelineClock.cpp
    
    # Models
    src/Models/TrackManager.cpp
    src/Models/PlaylistModel.cpp
    # src/Models/SelectionModel.cpp  # TODO: Rewrite for new PlaylistModel API (uses obsolete getAllClipsInRange, etc.)
    src/Models/UnitManager.cpp
    src/Models/PatternManager.cpp
    src/Models/ClipSource.cpp
    src/Models/AudioClip.cpp
    src/Models/PlaylistTrack.cpp
    
    # DSP
    src/DSP/AudioProcessor.cpp
    src/DSP/Filter.cpp
    src/DSP/Oscillator.cpp
    src/DSP/SincAVX512.cpp
    src/DSP/SampleRateConverter.cpp
    
    # IO
    src/IO/MiniAudioDecoder.cpp
    src/IO/StreamingDecoder.cpp
    src/IO/WaveformCache.cpp
    src/IO/PathUtils.cpp
    src/IO/MetadataParser.cpp
    
    # Drivers
    src/Drivers/DummyAudioDriver.cpp
    
    # Other (root-level for now)
    src/AudioGraphBuilder.cpp
    src/ChannelSlotMap.cpp
    src/SamplePool.cpp
    src/AudioRenderer.cpp
    
    # Commands
    src/Commands/CommandHistory.cpp
    src/Commands/SplitClipCommand.cpp
    src/Commands/AddClipCommand.cpp
    
    # Plugin hosting system
    src/Plugin/PluginScanner.cpp
    src/Plugin/PluginManager.cpp
    src/Plugin/PluginFactory.cpp
    src/Plugin/SamplerPlugin.cpp
    src/Plugin/EffectChain.cpp
)

# Set per-file compile flags for AVX-512 (x86_64 only, not ARM64)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686")
    if(MSVC)
        set_source_files_properties(src/DSP/SincAVX512.cpp PROPERTIES COMPILE_FLAGS "/arch:AVX512")
    else()
        set_source_files_properties(src/DSP/SincAVX512.cpp PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512dq")
    endif()
else()
    message(STATUS "Skipping AVX-512 optimizations for ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Add VST3 host sources if SDK is available
if(AESTRA_HAS_VST3)
    list(APPEND AESTRA_AUDIO_CORE_SOURCES
        src/Plugin/VST3Host.cpp
        ${VST3_HOST_SOURCES}
    )
endif()

# Add CLAP host sources if SDK is available
if(AESTRA_HAS_CLAP)
    list(APPEND AESTRA_AUDIO_CORE_SOURCES
        src/Plugin/CLAPHost.cpp
    )
endif()


# Core headers
set(AESTRA_AUDIO_CORE_HEADERS
    include/Core/AudioGraphBuilder.h
    include/Core/AudioEngine.h
    include/Core/AudioCommandQueue.h
    include/Core/AudioTelemetry.h
    include/Core/AudioGraph.h
    include/Core/ChannelSlotMap.h
    include/Core/EngineState.h
    include/Core/AestraAudio.h
    include/Core/AudioDeviceManager.h
    include/Drivers/IAudioDriver.h
    include/Drivers/AudioDriver.h           # Legacy/Base types
    include/Drivers/AudioDriverTypes.h
    include/Drivers/AudioDriverRegistry.h   # Registry interface
    include/DSP/AudioProcessor.h
    include/Models/MeterSnapshot.h
    include/Core/MixerBus.h
    include/DSP/Oscillator.h
    include/Playback/MetronomeEngine.h
    include/IO/PathUtils.h
    include/Playback/PreviewEngine.h
    include/IO/SamplePool.h
    include/DSP/SampleRateConverter.h
    include/Core/MixerChannel.h
    include/Models/TrackManager.h
    include/Models/PatternSource.h
    include/Models/ClipInstance.h
    include/Models/PatternManager.h
    include/Models/AudioClip.h
    include/IO/MiniAudioDecoder.h
    include/Models/PlaylistTrack.h
    # Multi-clip playlist system
    include/Models/TimeTypes.h
    include/Models/ClipSource.h
    include/Models/PlaylistModel.h
    include/Models/PlaylistRuntimeSnapshot.h
    include/Models/PlaylistMixer.h
    # include/Models/SelectionModel.h  # TODO: Fix - references non-existent PlaylistClip.h
    include/IO/WaveformCache.h
    include/Models/PlaylistGeometry.h
    include/Models/PlaylistSerializer.h
    include/Models/MultiClipPlaylist.h
    include/Models/ArsenalUnit.h
    include/Models/UnitManager.h

    include/IO/StreamingDecoder.h
    # Undo/Redo
    include/Commands/ICommand.h
    include/Commands/CommandHistory.h
    include/Commands/SplitClipCommand.h
    include/Commands/AddClipCommand.h
    include/Commands/MoveClipCommand.h
    include/Commands/RemoveClipCommand.h
    include/Commands/MacroCommand.h
    
    src/DummyAudioDriver.h
    
    src/DummyAudioDriver.h
    # Plugin hosting system
    include/Plugin/PluginHost.h
    include/Plugin/PluginScanner.h
    include/Plugin/PluginManager.h
    include/Plugin/PluginFactory.h
    include/Plugin/EffectChain.h
    include/Plugin/VST3Host.h
    include/Playback/AuditionEngine.h
)

# Create AestraAudioCore library
add_library(AestraAudioCore STATIC
    ${AESTRA_AUDIO_CORE_SOURCES}
    ${AESTRA_AUDIO_CORE_HEADERS}
)

# Enable miniaudio for fast audio decoding (MP3, FLAC, etc.)
set(AESTRA_USE_MINIAUDIO ON)

# Apply miniaudio compile definition
if (AESTRA_USE_MINIAUDIO)
    target_compile_definitions(AestraAudioCore PRIVATE AESTRA_USE_MINIAUDIO)
endif()

# AVX2/FMA: Enabled per-TU via #pragma in SincAVX2.h for safe runtime dispatch.
# IMPORTANT: Do NOT add global /arch:AVX2 here - it causes VEX-encoded scalar ops
# that crash on non-AVX2 CPUs even in the fallback path.

target_include_directories(AestraAudioCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        # Add all include subdirectories for reorganization-proof headers
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Core
        ${CMAKE_CURRENT_SOURCE_DIR}/include/DSP
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Models
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Playback
        ${CMAKE_CURRENT_SOURCE_DIR}/include/IO
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Drivers
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Plugin
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Commands
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../AestraCore/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../AestraPlat/include
        # Add all src subdirectories for reorganization-proof includes
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Playback
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Models
        ${CMAKE_CURRENT_SOURCE_DIR}/src/DSP
        ${CMAKE_CURRENT_SOURCE_DIR}/src/IO
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Drivers
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Plugin
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Commands
)

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/External/miniaudio)
    target_include_directories(AestraAudioCore PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/External/miniaudio
    )
endif()

# VST3 SDK includes (if available)
if(AESTRA_HAS_VST3)
    target_include_directories(AestraAudioCore PRIVATE
        ${VST3_INCLUDE_DIRS}
    )
    target_compile_definitions(AestraAudioCore PRIVATE AESTRA_HAS_VST3)
endif()

# CLAP SDK includes (if available)
if(AESTRA_HAS_CLAP)
    target_include_directories(AestraAudioCore PRIVATE
        ${CLAP_INCLUDE_DIRS}
    )
    target_compile_definitions(AestraAudioCore PRIVATE AESTRA_HAS_CLAP)
endif()

target_link_libraries(AestraAudioCore
    PUBLIC
)

if(TARGET AestraCore)
    target_link_libraries(AestraAudioCore PUBLIC AestraCore)
endif()

target_link_libraries(AestraAudioCore PUBLIC AestraPlat)

# =============================================================================
# Premium Plugins Integration (Optional)
# =============================================================================
if(EXISTS "${CMAKE_SOURCE_DIR}/AestraPlugins/AestraRumble/CMakeLists.txt")
    message(STATUS "AestraRumble: Linking premium 808 synthesizer")
    target_link_libraries(AestraAudioCore PRIVATE AestraRumble)
    target_compile_definitions(AestraAudioCore PRIVATE AESTRA_HAS_PLUGINS)
    target_include_directories(AestraAudioCore PRIVATE 
        ${CMAKE_SOURCE_DIR}/AestraPlugins/AestraRumble/include
    )
endif()

# ENFORCEMENT: AestraAudioCore MUST NOT link Windows libraries
# We can check this by property retrieval (not easy at config time for implicit libs)
# But we ensure we DO NOT call target_link_libraries with avrt, advapi32 etc.

# =============================================================================
# AestraAudioWin (Windows Backend)
# =============================================================================

if(WIN32)
    set(AESTRA_AUDIO_WIN_SOURCES
        src/Win32/WindowsDriverRegistry.cpp
        src/Win32/WASAPISharedDriver.cpp
        src/Win32/WASAPIExclusiveDriver.cpp
        src/Win32/ASIODriverInfo.cpp
        src/Win32/RtAudioBackend.cpp
        src/Drivers/ASIODriver.cpp  # Added Phase 6
    )
    
    set(AESTRA_AUDIO_WIN_HEADERS
        src/Win32/WASAPISharedDriver.h
        src/Win32/WASAPIExclusiveDriver.h
        src/Win32/ASIODriverInfo.h
        src/Win32/RtAudioBackend.h
        include/Drivers/NativeAudioDriver.h
        include/Drivers/ASIODriver.h     # Added Phase 6
        include/Drivers/ASIOInterface.h  # Added Phase 6
        ${RTAUDIO_HEADERS} # RtAudio headers needed for backend compilation
    )

    add_library(AestraAudioWin STATIC
        ${AESTRA_AUDIO_WIN_SOURCES}
        ${AESTRA_AUDIO_WIN_HEADERS}
        ${RTAUDIO_SOURCES}
    )

    target_include_directories(AestraAudioWin
        PUBLIC
            # Nothing public - backend is opaque
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include # Access to Core headers
            ${CMAKE_CURRENT_SOURCE_DIR}/src/Win32
            ${RTAUDIO_DIR}
    )

    target_link_libraries(AestraAudioWin
        PUBLIC
            AestraAudioCore
        PRIVATE
            ${RTAUDIO_LIBS}
            avrt        # MMCSS
            advapi32    # Registry
    )
    
    # Define "AestraAudio" as an alias to the platform library for main app linking
    add_library(AestraAudio ALIAS AestraAudioWin)

elseif(UNIX)
    # Non-Windows (Linux)
    if(ALSA_FOUND)
        set(AESTRA_AUDIO_LINUX_SOURCES
            src/Linux/LinuxDriverRegistry.cpp
            src/Linux/RtAudioDriver.cpp
        )

        set(AESTRA_AUDIO_LINUX_HEADERS
            src/Linux/RtAudioDriver.h
            include/Drivers/AudioPlatformRegistry.h
            ${RTAUDIO_HEADERS}
        )

        add_library(AestraAudioLinux STATIC
            ${AESTRA_AUDIO_LINUX_SOURCES}
            ${AESTRA_AUDIO_LINUX_HEADERS}
            ${RTAUDIO_SOURCES}
        )

        target_include_directories(AestraAudioLinux
            PUBLIC
                # Nothing public
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/src/Linux
                ${RTAUDIO_DIR}
        )

        target_link_libraries(AestraAudioLinux
            PUBLIC
                AestraAudioCore
            PRIVATE
                ${RTAUDIO_LIBS}
        )

        add_library(AestraAudio ALIAS AestraAudioLinux)
    else()
        # No ALSA - add the stub source directly to AestraAudioCore
        target_sources(AestraAudioCore PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/Core/DriverRegistryStub.cpp
        )
        
        # Create an interface library for compatibility
        add_library(AestraAudio INTERFACE)
        target_link_libraries(AestraAudio INTERFACE AestraAudioCore)
    endif()
endif()


# =============================================================================
# Test Applications
# =============================================================================
# ALL TESTS MOVED TO Tests/CMakeLists.txt FOR CLEAN SEPARATION
# Do not add test targets here - they are centralized in the root Tests/ directory

# Per-TU AVX2 for benchmark (only the SincAVX2.h code uses intrinsics)
# For now, rely on runtime dispatch; SIMD code is guarded by CPUDetection.

# =============================================================================
# Status
# =============================================================================

message(STATUS "")
message(STATUS "=================================")
message(STATUS "  AestraAudio v${PROJECT_VERSION}")
message(STATUS "=================================")
message(STATUS "RtAudio: Integrated (MIT)")
if(WIN32)
    message(STATUS "Audio API: WASAPI")
    message(STATUS "Backend: Windows Audio Session API")
elseif(APPLE)
    message(STATUS "Audio API: CoreAudio")
elseif(UNIX)
    message(STATUS "Audio API: ALSA")
endif()
message(STATUS "=================================")
message(STATUS "")
