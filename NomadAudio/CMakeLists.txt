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
# VST3 SDK Integration (MIT License)
# =============================================================================

set(VST3_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External/vst3sdk)

if(EXISTS ${VST3_SDK_DIR}/pluginterfaces AND EXISTS ${VST3_SDK_DIR}/public.sdk)
    set(NOMAD_HAS_VST3 ON)
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
    set(NOMAD_HAS_VST3 OFF)
    message(STATUS "VST3 SDK: Not found - plugin hosting disabled")
endif()

# =============================================================================
# CLAP SDK Integration (MIT License)
# =============================================================================

set(CLAP_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External/clap)

if(EXISTS ${CLAP_SDK_DIR}/include/clap/clap.h)
    set(NOMAD_HAS_CLAP ON)
    message(STATUS "CLAP SDK: Found at ${CLAP_SDK_DIR}")
    
    # CLAP SDK is header-only, just need include path
    set(CLAP_INCLUDE_DIRS
        ${CLAP_SDK_DIR}/include
    )
else()
    set(NOMAD_HAS_CLAP OFF)
    message(STATUS "CLAP SDK: Not found")
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
    # src/SelectionModel.cpp  # TODO: Fix API mismatches (PlaylistClip.h, etc)
    src/WaveformCache.cpp
    src/PatternManager.cpp

    src/UnitManager.cpp
    src/PatternPlaybackEngine.cpp
    src/TimelineClock.cpp
    src/StreamingDecoder.cpp
    src/DummyAudioDriver.cpp
    # Undo/Redo
    # Undo/Redo
    src/Commands/CommandHistory.cpp
    src/Commands/SplitClipCommand.cpp
    src/Commands/AddClipCommand.cpp
    # Plugin hosting system
    # Plugin hosting system
    src/Plugin/PluginScanner.cpp
    src/Plugin/PluginManager.cpp
    src/Plugin/PluginFactory.cpp
    src/Plugin/SamplerPlugin.cpp
    src/Plugin/EffectChain.cpp
    src/SincAVX512.cpp
    src/AudioRenderer.cpp
    src/AuditionEngine.cpp
    src/MetadataParser.cpp
)

# Set per-file compile flags for AVX-512
if(MSVC)
    set_source_files_properties(src/SincAVX512.cpp PROPERTIES COMPILE_FLAGS "/arch:AVX512")
else()
    set_source_files_properties(src/SincAVX512.cpp PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512dq")
endif()

# Add VST3 host sources if SDK is available
if(NOMAD_HAS_VST3)
    list(APPEND NOMAD_AUDIO_CORE_SOURCES
        src/Plugin/VST3Host.cpp
        ${VST3_HOST_SOURCES}
    )
endif()

# Add CLAP host sources if SDK is available
if(NOMAD_HAS_CLAP)
    list(APPEND NOMAD_AUDIO_CORE_SOURCES
        src/Plugin/CLAPHost.cpp
    )
endif()


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
    # include/SelectionModel.h  # TODO: Fix - references non-existent PlaylistClip.h
    include/WaveformCache.h
    include/PlaylistGeometry.h
    include/PlaylistSerializer.h
    include/MultiClipPlaylist.h
    include/ArsenalUnit.h
    include/UnitManager.h

    include/StreamingDecoder.h
    # Undo/Redo
    include/Commands/ICommand.h
    include/Commands/CommandHistory.h
    include/Commands/SplitClipCommand.h
    include/Commands/AddClipCommand.h
    
    src/DummyAudioDriver.h
    
    src/DummyAudioDriver.h
    # Plugin hosting system
    include/PluginHost.h
    include/PluginScanner.h
    include/PluginManager.h
    include/PluginFactory.h
    include/EffectChain.h
    include/Plugin/VST3Host.h
    include/AuditionEngine.h
)

# Create NomadAudioCore library
add_library(NomadAudioCore STATIC
    ${NOMAD_AUDIO_CORE_SOURCES}
    ${NOMAD_AUDIO_CORE_HEADERS}
)

# Enable miniaudio for fast audio decoding (MP3, FLAC, etc.)
set(NOMAD_USE_MINIAUDIO ON)

# Apply miniaudio compile definition
if (NOMAD_USE_MINIAUDIO)
    target_compile_definitions(NomadAudioCore PRIVATE NOMAD_USE_MINIAUDIO)
endif()

# AVX2/FMA: Enabled per-TU via #pragma in SincAVX2.h for safe runtime dispatch.
# IMPORTANT: Do NOT add global /arch:AVX2 here - it causes VEX-encoded scalar ops
# that crash on non-AVX2 CPUs even in the fallback path.

target_include_directories(NomadAudioCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../NomadCore/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../NomadPlat/include
)

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/External/miniaudio)
    target_include_directories(NomadAudioCore PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/External/miniaudio
    )
endif()

# VST3 SDK includes (if available)
if(NOMAD_HAS_VST3)
    target_include_directories(NomadAudioCore PRIVATE
        ${VST3_INCLUDE_DIRS}
    )
    target_compile_definitions(NomadAudioCore PRIVATE NOMAD_HAS_VST3)
endif()

# CLAP SDK includes (if available)
if(NOMAD_HAS_CLAP)
    target_include_directories(NomadAudioCore PRIVATE
        ${CLAP_INCLUDE_DIRS}
    )
    target_compile_definitions(NomadAudioCore PRIVATE NOMAD_HAS_CLAP)
endif()

target_link_libraries(NomadAudioCore
    PUBLIC
)

if(TARGET NomadCore)
    target_link_libraries(NomadAudioCore PUBLIC NomadCore)
endif()

target_link_libraries(NomadAudioCore PUBLIC NomadPlat)

# =============================================================================
# Premium Plugins Integration (Optional)
# =============================================================================
if(EXISTS "${CMAKE_SOURCE_DIR}/NomadPlugins/NomadRumble/CMakeLists.txt")
    message(STATUS "NomadRumble: Linking premium 808 synthesizer")
    target_link_libraries(NomadAudioCore PRIVATE NomadRumble)
    target_compile_definitions(NomadAudioCore PRIVATE NOMAD_HAS_PLUGINS)
    target_include_directories(NomadAudioCore PRIVATE 
        ${CMAKE_SOURCE_DIR}/NomadPlugins/NomadRumble/include
    )
endif()

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
        src/ASIODriver.cpp  # Added Phase 6
    )
    
    set(NOMAD_AUDIO_WIN_HEADERS
        src/Win32/WASAPISharedDriver.h
        src/Win32/WASAPIExclusiveDriver.h
        src/Win32/ASIODriverInfo.h
        src/Win32/RtAudioBackend.h
        include/NativeAudioDriver.h
        include/ASIODriver.h     # Added Phase 6
        include/ASIOInterface.h  # Added Phase 6
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

if(NOT NOMAD_ENABLE_TESTS)
    message(STATUS "NomadAudio tests: disabled (NOMAD_ENABLE_TESTS=OFF)")
else()
    include(CTest)
    enable_testing()

# Basic audio test
add_executable(NomadAudioTest
    test/AudioTest.cpp
)

target_link_libraries(NomadAudioTest
    PRIVATE
        NomadAudio
        NomadCore
)
add_test(NAME NomadAudioTest COMMAND NomadAudioTest)

# Device manager test
add_executable(NomadDeviceManagerTest
    test/DeviceManagerTest.cpp
)

target_link_libraries(NomadDeviceManagerTest
    PRIVATE
        NomadAudio
        NomadCore
)
add_test(NAME NomadDeviceManagerTest COMMAND NomadDeviceManagerTest)

# Audio callback test
add_executable(NomadAudioCallbackTest
    test/AudioCallbackTest.cpp
)

target_link_libraries(NomadAudioCallbackTest
    PRIVATE
        NomadAudio
        NomadCore
)
add_test(NAME NomadAudioCallbackTest COMMAND NomadAudioCallbackTest)

# Mixer bus test
add_executable(NomadMixerBusTest
    test/MixerBusTest.cpp
)

target_link_libraries(NomadMixerBusTest
    PRIVATE
        NomadAudio
        NomadCore
)
add_test(NAME NomadMixerBusTest COMMAND NomadMixerBusTest)

# Oscillator test
add_executable(NomadOscillatorTest
    test/OscillatorTest.cpp
)

target_link_libraries(NomadOscillatorTest
    PRIVATE
        NomadAudio
        NomadCore
)
add_test(NAME NomadOscillatorTest COMMAND NomadOscillatorTest)

# WAV loader regression test
add_executable(NomadWavLoaderTest
    test/WavLoaderTest.cpp
)

target_link_libraries(NomadWavLoaderTest
    PRIVATE
        NomadAudio
        NomadCore
)
add_test(NAME NomadWavLoaderTest COMMAND NomadWavLoaderTest)

# Sample Rate Converter test
add_executable(NomadSampleRateConverterTest
    test/SampleRateConverterTest.cpp
)

target_link_libraries(NomadSampleRateConverterTest
    PRIVATE
        NomadAudio
        NomadCore
)
add_test(NAME NomadSampleRateConverterTest COMMAND NomadSampleRateConverterTest)

# Audio engine long-session soak test (no device required)
add_executable(NomadAudioSoakTest
    test/AudioEngineSoakTest.cpp
)

target_link_libraries(NomadAudioSoakTest
    PRIVATE
        NomadAudio
        NomadCore
)
add_test(NAME NomadAudioSoakTest COMMAND NomadAudioSoakTest)

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
add_test(NAME NomadAudioPerformanceTest COMMAND NomadAudioPerformanceTest)

if(WIN32)
    target_link_libraries(NomadAudioPerformanceTest PRIVATE avrt)
endif()

# Sinc Interpolator Benchmark
add_executable(NomadSincBenchmark
    test/SincBenchmark.cpp
)

target_link_libraries(NomadSincBenchmark
    PRIVATE
        NomadAudioCore
)

if(TARGET NomadCore)
    target_link_libraries(NomadSincBenchmark PRIVATE NomadCore)
endif()

target_include_directories(NomadSincBenchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../NomadCore/include
)
add_test(NAME NomadSincBenchmark COMMAND NomadSincBenchmark)

# Watchdog Verification Test
add_executable(NomadWatchdogTest
    test/WatchdogTest.cpp
)
target_include_directories(NomadWatchdogTest PRIVATE ${VST3_INCLUDE_DIRS})
target_link_libraries(NomadWatchdogTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadWatchdogTest COMMAND NomadWatchdogTest)

# Crash Protection Verification Test (SEH)
add_executable(NomadCrashTest
    test/CrashTest.cpp
)
target_include_directories(NomadCrashTest PRIVATE ${VST3_INCLUDE_DIRS})
target_link_libraries(NomadCrashTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadCrashTest COMMAND NomadCrashTest)

# Waveform Isolation Verification Test
add_executable(NomadWaveformLockTest
    test/WaveformLockTest.cpp
    src/WaveformCache.cpp
)
target_link_libraries(NomadWaveformLockTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadWaveformLockTest COMMAND NomadWaveformLockTest)

# Atomic Autosave Verification Test
add_executable(NomadAtomicSaveTest
    test/AtomicSaveTest.cpp
    src/PlaylistModel.cpp
    src/ClipSource.cpp
)
target_link_libraries(NomadAtomicSaveTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadAtomicSaveTest COMMAND NomadAtomicSaveTest)

endif()

# Per-TU AVX2 for benchmark (only the SincAVX2.h code uses intrinsics)
# For now, rely on runtime dispatch; SIMD code is guarded by CPUDetection.

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
