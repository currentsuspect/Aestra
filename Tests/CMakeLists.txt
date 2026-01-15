# =============================================================================
# NOMAD Tests - Centralized Test Configuration
# =============================================================================

cmake_minimum_required(VERSION 3.22)

enable_testing()

# =============================================================================
# Integration Tests (Cross-Module)
# =============================================================================

# VST3 Plugin Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/VST3PluginTest.cpp")
    add_executable(VST3PluginTest Integration/VST3PluginTest.cpp)
    target_link_libraries(VST3PluginTest PRIVATE NomadAudioCore NomadCore)
    target_include_directories(VST3PluginTest PRIVATE
        ${CMAKE_SOURCE_DIR}/NomadAudio/include
        ${CMAKE_SOURCE_DIR}/NomadCore/include
    )
    # VST3 SDK integration
    set(VST3_SDK_PATH "${CMAKE_SOURCE_DIR}/NomadAudio/External/vst3sdk")
    if(EXISTS "${VST3_SDK_PATH}/pluginterfaces")
        target_compile_definitions(VST3PluginTest PRIVATE NOMAD_HAS_VST3)
        target_include_directories(VST3PluginTest PRIVATE
            ${VST3_SDK_PATH}
            ${VST3_SDK_PATH}/pluginterfaces
            ${VST3_SDK_PATH}/public.sdk
            ${VST3_SDK_PATH}/public.sdk/source
            ${VST3_SDK_PATH}/base
        )
    endif()
    if(WIN32)
        target_link_libraries(VST3PluginTest PRIVATE ole32)
    endif()
endif()

# Project RoundTrip Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ProjectRoundTripTest.cpp")
    add_executable(ProjectRoundTripTest
        Integration/ProjectRoundTripTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
    )
    target_link_libraries(ProjectRoundTripTest PRIVATE NomadAudio NomadCore)
    target_include_directories(ProjectRoundTripTest PRIVATE
        ${CMAKE_SOURCE_DIR}/NomadAudio/include
        ${CMAKE_SOURCE_DIR}/NomadCore/include
        ${CMAKE_SOURCE_DIR}/Source
    )
    target_compile_definitions(ProjectRoundTripTest PRIVATE
        $<$<CONFIG:Debug>:NOMAD_ENABLE_PROJECT_LOAD_LOGS>
    )
    add_test(NAME ProjectRoundTripTest COMMAND ProjectRoundTripTest)
    add_test(NAME AutosaveRoundTripTest COMMAND ProjectRoundTripTest)
endif()

# Plugin Scanner Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/PluginScanTest.cpp")
    add_executable(NomadPluginScanTest Integration/PluginScanTest.cpp)
    target_link_libraries(NomadPluginScanTest PRIVATE NomadAudio NomadCore)
    target_include_directories(NomadPluginScanTest PRIVATE
        ${CMAKE_SOURCE_DIR}/NomadAudio/include
        ${CMAKE_SOURCE_DIR}/NomadCore/include
    )
    if(WIN32)
        target_link_libraries(NomadPluginScanTest PRIVATE ole32)
    endif()
endif()

# Resampler Benchmark
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ResamplerBenchmark.cpp")
    add_executable(ResamplerBenchmark Integration/ResamplerBenchmark.cpp)
    target_link_libraries(ResamplerBenchmark PRIVATE NomadAudioCore NomadCore)
    target_include_directories(ResamplerBenchmark PRIVATE
        ${CMAKE_SOURCE_DIR}/NomadAudio/include
        ${CMAKE_SOURCE_DIR}/NomadCore/include
    )
    if(WIN32)
        target_link_libraries(ResamplerBenchmark PRIVATE ole32)
    endif()
    # Enable AVX2 if available
    if(MSVC)
        target_compile_options(ResamplerBenchmark PRIVATE /arch:AVX2)
    else()
        target_compile_options(ResamplerBenchmark PRIVATE -mavx2 -mfma)
    endif()
endif()

# =============================================================================
# NomadAudio Tests
# =============================================================================

# Audio Test
add_executable(NomadAudioTest NomadAudio/AudioTest.cpp)
target_link_libraries(NomadAudioTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadAudioTest COMMAND NomadAudioTest)

# Soak Test
add_executable(NomadAudioSoakTest NomadAudio/AudioEngineSoakTest.cpp)
target_link_libraries(NomadAudioSoakTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadAudioSoakTest COMMAND NomadAudioSoakTest)

# Performance Stress Test
add_executable(NomadAudioPerformanceTest
    NomadAudio/PerformanceStressTest.cpp
    ${CMAKE_SOURCE_DIR}/NomadAudio/src/DSP/Filter.cpp
    ${CMAKE_SOURCE_DIR}/NomadAudio/src/DSP/Oscillator.cpp
)
target_link_libraries(NomadAudioPerformanceTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadAudioPerformanceTest COMMAND NomadAudioPerformanceTest)
if(WIN32)
    target_link_libraries(NomadAudioPerformanceTest PRIVATE avrt)
endif()

# Sinc Interpolator Benchmark
add_executable(NomadSincBenchmark NomadAudio/SincBenchmark.cpp)
target_link_libraries(NomadSincBenchmark PRIVATE NomadAudioCore NomadCore)

# Waveform Lock Test
add_executable(NomadWaveformLockTest
    NomadAudio/WaveformLockTest.cpp
    ${CMAKE_SOURCE_DIR}/NomadAudio/src/IO/WaveformCache.cpp
)
target_link_libraries(NomadWaveformLockTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadWaveformLockTest COMMAND NomadWaveformLockTest)

# Atomic Save Test
add_executable(NomadAtomicSaveTest
    NomadAudio/AtomicSaveTest.cpp
    ${CMAKE_SOURCE_DIR}/NomadAudio/src/Models/PlaylistModel.cpp
    ${CMAKE_SOURCE_DIR}/NomadAudio/src/Models/ClipSource.cpp
)
target_link_libraries(NomadAtomicSaveTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadAtomicSaveTest COMMAND NomadAtomicSaveTest)

# Watchdog Test
add_executable(NomadWatchdogTest NomadAudio/WatchdogTest.cpp)
target_link_libraries(NomadWatchdogTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadWatchdogTest COMMAND NomadWatchdogTest)

# Crash Test
add_executable(NomadCrashTest NomadAudio/CrashTest.cpp)
target_link_libraries(NomadCrashTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadCrashTest COMMAND NomadCrashTest)

# Wav Loader Test
add_executable(NomadWavLoaderTest NomadAudio/WavLoaderTest.cpp)
target_link_libraries(NomadWavLoaderTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadWavLoaderTest COMMAND NomadWavLoaderTest)

# Device Manager Test (Windows only)
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/NomadAudio/DeviceManagerTest.cpp")
    add_executable(NomadDeviceManagerTest NomadAudio/DeviceManagerTest.cpp)
    target_link_libraries(NomadDeviceManagerTest PRIVATE NomadAudio NomadCore)
    target_link_libraries(NomadDeviceManagerTest PRIVATE avrt)
    add_test(NAME NomadDeviceManagerTest COMMAND NomadDeviceManagerTest)
endif()

# Audio Callback Test
add_executable(NomadAudioCallbackTest NomadAudio/AudioCallbackTest.cpp)
target_link_libraries(NomadAudioCallbackTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadAudioCallbackTest COMMAND NomadAudioCallbackTest)

# Filter Test
add_executable(NomadFilterTest NomadAudio/FilterTest.cpp)
target_link_libraries(NomadFilterTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadFilterTest COMMAND NomadFilterTest)

# Oscillator Test
add_executable(NomadOscillatorTest NomadAudio/OscillatorTest.cpp)
target_link_libraries(NomadOscillatorTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadOscillatorTest COMMAND NomadOscillatorTest)

# Mixer Bus Test
add_executable(NomadMixerBusTest NomadAudio/MixerBusTest.cpp)
target_link_libraries(NomadMixerBusTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadMixerBusTest COMMAND NomadMixerBusTest)

# Sample Rate Converter Test
add_executable(NomadSampleRateConverterTest NomadAudio/SampleRateConverterTest.cpp)
target_link_libraries(NomadSampleRateConverterTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadSampleRateConverterTest COMMAND NomadSampleRateConverterTest)

# Serialization Compatibility Test
add_executable(NomadSerializationCompatibilityTest NomadAudio/SerializationCompatibilityTest.cpp)
target_link_libraries(NomadSerializationCompatibilityTest PRIVATE NomadAudio NomadCore)
add_test(NAME NomadSerializationCompatibilityTest COMMAND NomadSerializationCompatibilityTest)

# =============================================================================
# NomadPlat Tests
# =============================================================================

# Platform DPI Test
add_executable(PlatformDPITest NomadPlat/PlatformDPITest.cpp)
target_link_libraries(PlatformDPITest PRIVATE NomadPlat NomadCore)

# Platform Window Test  
add_executable(PlatformWindowTest NomadPlat/PlatformWindowTest.cpp)
target_link_libraries(PlatformWindowTest PRIVATE NomadPlat NomadCore)

message(STATUS "Tests configured - see Tests/ directory")
