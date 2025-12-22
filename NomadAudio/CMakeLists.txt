cmake_minimum_required(VERSION 3.22)

project(NomadAudio VERSION 1.0.0 LANGUAGES CXX)

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
    find_package(ALSA REQUIRED)
    set(RTAUDIO_LIBS ${ALSA_LIBRARIES} pthread)

    # Optional JACK support
    # add_definitions(-D__UNIX_JACK__)
    # find_package(Jack)
    # if(JACK_FOUND)
    #     set(RTAUDIO_LIBS ${RTAUDIO_LIBS} ${JACK_LIBRARIES})
    # endif()
endif()

# =============================================================================
# NomadAudio Library
# =============================================================================

# =============================================================================
# NomadAudioCore (Platform-Agnostic)
# =============================================================================

# Core sources (NO platform-specific code)
set(NOMAD_AUDIO_CORE_SOURCES
    src/Filter.cpp
    src/AudioGraphBuilder.cpp
    src/PathUtils.cpp
    src/AudioEngine.cpp
    src/AudioDeviceManager.cpp
    src/AudioProcessor.cpp
    src/ChannelSlotMap.cpp
    src/MixerBus.cpp
    src/Oscillator.cpp
    src/PreviewEngine.cpp
    src/SamplePool.cpp
    src/SampleRateConverter.cpp
    src/MixerChannel.cpp
    src/TrackManager.cpp
    src/AudioClip.cpp
    src/MiniAudioDecoder.cpp
    src/PlaylistTrack.cpp
    # Multi-clip playlist system
    src/ClipSource.cpp
    src/PlaylistModel.cpp
    src/SelectionModel.cpp
    src/WaveformCache.cpp
    src/PatternManager.cpp
    src/UnitManager.cpp
    src/PatternPlaybackEngine.cpp
    src/TimelineClock.cpp
)


# Core headers
set(NOMAD_AUDIO_CORE_HEADERS
    include/AudioGraphBuilder.h
    include/AudioEngine.h
    include/AudioCommandQueue.h
    include/AudioTelemetry.h
    include/AudioGraph.h
    include/ChannelSlotMap.h
    include/EngineState.h
    include/NomadAudio.h
    include/AudioDeviceManager.h
    include/IAudioDriver.h
    include/AudioDriver.h           # Legacy/Base types
    include/AudioDriverTypes.h
    include/AudioDriverRegistry.h   # Registry interface
    include/AudioProcessor.h
    include/MeterSnapshot.h
    include/MixerBus.h
    include/Oscillator.h
    include/PathUtils.h
    include/PreviewEngine.h
    include/SamplePool.h
    include/SampleRateConverter.h
    include/MixerChannel.h
    include/TrackManager.h
    include/PatternSource.h
    include/ClipInstance.h
    include/PatternManager.h
    include/AudioClip.h
    include/MiniAudioDecoder.h
    include/PlaylistTrack.h
    # Multi-clip playlist system
    include/TimeTypes.h
    include/ClipSource.h
    include/PlaylistModel.h
    include/PlaylistRuntimeSnapshot.h
    include/PlaylistMixer.h
    include/SelectionModel.h
    include/WaveformCache.h
    include/PlaylistGeometry.h
    include/PlaylistSerializer.h
    include/MultiClipPlaylist.h
    include/ArsenalUnit.h
    include/UnitManager.h
)

# Create NomadAudioCore library
add_library(NomadAudioCore STATIC
    ${NOMAD_AUDIO_CORE_SOURCES}
    ${NOMAD_AUDIO_CORE_HEADERS}
)

# Optional miniaudio
if (NOMAD_USE_MINIAUDIO)
    target_compile_definitions(NomadAudioCore PRIVATE NOMAD_USE_MINIAUDIO)
endif()

target_include_directories(NomadAudioCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/NomadCore/include
)

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/External/miniaudio)
    target_include_directories(NomadAudioCore PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/External/miniaudio
    )
endif()

target_link_libraries(NomadAudioCore
    PUBLIC
        NomadCore
        NomadPlat # For strict thread priority API (Platform::setCurrentThreadPriority)
)

# ENFORCEMENT: NomadAudioCore MUST NOT link Windows libraries
# We can check this by property retrieval (not easy at config time for implicit libs)
# But we ensure we DO NOT call target_link_libraries with avrt, advapi32 etc.

# =============================================================================
# NomadAudioWin (Windows Backend)
# =============================================================================

if(WIN32)
    set(NOMAD_AUDIO_WIN_SOURCES
        src/Win32/WindowsDriverRegistry.cpp
        src/Win32/WASAPISharedDriver.cpp
        src/Win32/WASAPIExclusiveDriver.cpp
        src/Win32/ASIODriverInfo.cpp
        src/Win32/RtAudioBackend.cpp
    )
    
    set(NOMAD_AUDIO_WIN_HEADERS
        src/Win32/WASAPISharedDriver.h
        src/Win32/WASAPIExclusiveDriver.h
        src/Win32/ASIODriverInfo.h
        src/Win32/RtAudioBackend.h
        include/NativeAudioDriver.h
        ${RTAUDIO_HEADERS} # RtAudio headers needed for backend compilation
    )

    add_library(NomadAudioWin STATIC
        ${NOMAD_AUDIO_WIN_SOURCES}
        ${NOMAD_AUDIO_WIN_HEADERS}
        ${RTAUDIO_SOURCES}
    )

    target_include_directories(NomadAudioWin
        PUBLIC
            # Nothing public - backend is opaque
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include # Access to Core headers
            ${CMAKE_CURRENT_SOURCE_DIR}/src/Win32
            ${RTAUDIO_DIR}
    )

    target_link_libraries(NomadAudioWin
        PUBLIC
            NomadAudioCore
        PRIVATE
            ${RTAUDIO_LIBS}
            avrt        # MMCSS
            advapi32    # Registry
    )
    
    # Define "NomadAudio" as an alias to the platform library for main app linking
    add_library(NomadAudio ALIAS NomadAudioWin)

elseif(UNIX)
    # Non-Windows (Linux)
    set(NOMAD_AUDIO_LINUX_SOURCES
        src/Linux/LinuxDriverRegistry.cpp
        src/Linux/RtAudioDriver.cpp
    )
    
    set(NOMAD_AUDIO_LINUX_HEADERS
        src/Linux/RtAudioDriver.h
        include/AudioPlatformRegistry.h
        ${RTAUDIO_HEADERS}
    )

    add_library(NomadAudioLinux STATIC
        ${NOMAD_AUDIO_LINUX_SOURCES}
        ${NOMAD_AUDIO_LINUX_HEADERS}
        ${RTAUDIO_SOURCES}
    )
    
    target_include_directories(NomadAudioLinux
        PUBLIC
            # Nothing public
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src/Linux
            ${RTAUDIO_DIR}
    )
    
    target_link_libraries(NomadAudioLinux
        PUBLIC
            NomadAudioCore
        PRIVATE
            ${RTAUDIO_LIBS}
    )
    
    add_library(NomadAudio ALIAS NomadAudioLinux)
endif()


# =============================================================================
# Test Applications
# =============================================================================

# Basic audio test
add_executable(NomadAudioTest
    test/AudioTest.cpp
)

target_link_libraries(NomadAudioTest
    PRIVATE
        NomadAudio
        NomadCore
)

# Device manager test
add_executable(NomadDeviceManagerTest
    test/DeviceManagerTest.cpp
)

target_link_libraries(NomadDeviceManagerTest
    PRIVATE
        NomadAudio
        NomadCore
)

# Audio callback test
add_executable(NomadAudioCallbackTest
    test/AudioCallbackTest.cpp
)

target_link_libraries(NomadAudioCallbackTest
    PRIVATE
        NomadAudio
        NomadCore
)

# Mixer bus test
add_executable(NomadMixerBusTest
    test/MixerBusTest.cpp
)

target_link_libraries(NomadMixerBusTest
    PRIVATE
        NomadAudio
        NomadCore
)

# Oscillator test
add_executable(NomadOscillatorTest
    test/OscillatorTest.cpp
)

target_link_libraries(NomadOscillatorTest
    PRIVATE
        NomadAudio
        NomadCore
)

# WAV loader regression test
add_executable(NomadWavLoaderTest
    test/WavLoaderTest.cpp
)

target_link_libraries(NomadWavLoaderTest
    PRIVATE
        NomadAudio
        NomadCore
)

# Sample Rate Converter test
add_executable(NomadSampleRateConverterTest
    test/SampleRateConverterTest.cpp
)

target_link_libraries(NomadSampleRateConverterTest
    PRIVATE
        NomadAudio
        NomadCore
)

# Audio engine long-session soak test (no device required)
add_executable(NomadAudioSoakTest
    test/AudioEngineSoakTest.cpp
)

target_link_libraries(NomadAudioSoakTest
    PRIVATE
        NomadAudio
        NomadCore
)

# Performance stress test suite
add_executable(NomadAudioPerformanceTest
    test/PerformanceStressTest.cpp
    src/Filter.cpp  # Need to compile Filter directly if not exposed via library public headers effectively or just to be safe
    src/Oscillator.cpp
)

target_link_libraries(NomadAudioPerformanceTest
    PRIVATE
        NomadAudio
        NomadCore
)

if(WIN32)
    target_link_libraries(NomadAudioPerformanceTest PRIVATE avrt)
endif()

# =============================================================================
# Status
# =============================================================================

message(STATUS "")
message(STATUS "=================================")
message(STATUS "  NomadAudio v${PROJECT_VERSION}")
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
