# =============================================================================
# AESTRA Tests - Centralized Test Configuration
# =============================================================================

cmake_minimum_required(VERSION 3.22)

enable_testing()

# =============================================================================
# Integration Tests (Cross-Module)
# =============================================================================

# VST3 Plugin Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/VST3PluginTest.cpp")
    add_executable(VST3PluginTest Integration/VST3PluginTest.cpp)
    target_link_libraries(VST3PluginTest PRIVATE AestraAudioCore AestraCore)
    target_include_directories(VST3PluginTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    # VST3 SDK integration
    set(VST3_SDK_PATH "${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk")
    if(EXISTS "${VST3_SDK_PATH}/pluginterfaces")
        target_compile_definitions(VST3PluginTest PRIVATE AESTRA_HAS_VST3)
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
    target_link_libraries(ProjectRoundTripTest PRIVATE AestraAudio AestraCore)
    target_include_directories(ProjectRoundTripTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_SOURCE_DIR}/Source
    )
    target_compile_definitions(ProjectRoundTripTest PRIVATE
        $<$<CONFIG:Debug>:AESTRA_ENABLE_PROJECT_LOAD_LOGS>
    )
    add_test(NAME ProjectRoundTripTest COMMAND ProjectRoundTripTest)
    add_test(NAME AutosaveRoundTripTest COMMAND ProjectRoundTripTest)
endif()

# Plugin Scanner Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/PluginScanTest.cpp")
    add_executable(AestraPluginScanTest Integration/PluginScanTest.cpp)
    target_link_libraries(AestraPluginScanTest PRIVATE AestraAudio AestraCore)
    target_include_directories(AestraPluginScanTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    if(WIN32)
        target_link_libraries(AestraPluginScanTest PRIVATE ole32)
    endif()
endif()

# Resampler Benchmark
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ResamplerBenchmark.cpp")
    add_executable(ResamplerBenchmark Integration/ResamplerBenchmark.cpp)
    target_link_libraries(ResamplerBenchmark PRIVATE AestraAudioCore AestraCore)
    target_include_directories(ResamplerBenchmark PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
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
# AestraAudio Tests
# =============================================================================

# Audio Test
add_executable(AestraAudioTest AestraAudio/AudioTest.cpp)
target_link_libraries(AestraAudioTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraAudioTest COMMAND AestraAudioTest)

# Soak Test
add_executable(AestraAudioSoakTest AestraAudio/AudioEngineSoakTest.cpp)
target_link_libraries(AestraAudioSoakTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraAudioSoakTest COMMAND AestraAudioSoakTest)

# Performance Stress Test
add_executable(AestraAudioPerformanceTest
    AestraAudio/PerformanceStressTest.cpp
    ${CMAKE_SOURCE_DIR}/AestraAudio/src/DSP/Filter.cpp
    ${CMAKE_SOURCE_DIR}/AestraAudio/src/DSP/Oscillator.cpp
)
target_link_libraries(AestraAudioPerformanceTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraAudioPerformanceTest COMMAND AestraAudioPerformanceTest)
if(WIN32)
    target_link_libraries(AestraAudioPerformanceTest PRIVATE avrt)
endif()

# Sinc Interpolator Benchmark
add_executable(AestraSincBenchmark AestraAudio/SincBenchmark.cpp)
target_link_libraries(AestraSincBenchmark PRIVATE AestraAudioCore AestraCore)

# Waveform Lock Test
add_executable(AestraWaveformLockTest
    AestraAudio/WaveformLockTest.cpp
    ${CMAKE_SOURCE_DIR}/AestraAudio/src/IO/WaveformCache.cpp
)
target_link_libraries(AestraWaveformLockTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraWaveformLockTest COMMAND AestraWaveformLockTest)

# Atomic Save Test - DISABLED (Missing Models)
# add_executable(AestraAtomicSaveTest
#    AestraAudio/AtomicSaveTest.cpp
#    ${CMAKE_SOURCE_DIR}/AestraAudio/src/Models/PlaylistModel.cpp
#    ${CMAKE_SOURCE_DIR}/AestraAudio/src/Models/ClipSource.cpp
# )
# target_link_libraries(AestraAtomicSaveTest PRIVATE AestraAudio AestraCore)
# add_test(NAME AestraAtomicSaveTest COMMAND AestraAtomicSaveTest)

# Watchdog Test
add_executable(AestraWatchdogTest AestraAudio/WatchdogTest.cpp)
target_link_libraries(AestraWatchdogTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraWatchdogTest COMMAND AestraWatchdogTest)
if(EXISTS "${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk/pluginterfaces")
    target_include_directories(AestraWatchdogTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk
    )
endif()

# Crash Test
add_executable(AestraCrashTest AestraAudio/CrashTest.cpp)
target_link_libraries(AestraCrashTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraCrashTest COMMAND AestraCrashTest)
if(EXISTS "${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk/pluginterfaces")
    target_include_directories(AestraCrashTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk
    )
endif()

# Wav Loader Test
add_executable(AestraWavLoaderTest AestraAudio/WavLoaderTest.cpp)
target_link_libraries(AestraWavLoaderTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraWavLoaderTest COMMAND AestraWavLoaderTest)

# Device Manager Test (Windows only)
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/DeviceManagerTest.cpp")
    add_executable(AestraDeviceManagerTest AestraAudio/DeviceManagerTest.cpp)
    target_link_libraries(AestraDeviceManagerTest PRIVATE AestraAudio AestraCore)
    target_link_libraries(AestraDeviceManagerTest PRIVATE avrt)
    add_test(NAME AestraDeviceManagerTest COMMAND AestraDeviceManagerTest)
endif()

# Audio Callback Test
add_executable(AestraAudioCallbackTest AestraAudio/AudioCallbackTest.cpp)
target_link_libraries(AestraAudioCallbackTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraAudioCallbackTest COMMAND AestraAudioCallbackTest)

# Filter Test
add_executable(AestraFilterTest AestraAudio/FilterTest.cpp)
target_link_libraries(AestraFilterTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraFilterTest COMMAND AestraFilterTest)

# Oscillator Test
add_executable(AestraOscillatorTest AestraAudio/OscillatorTest.cpp)
target_link_libraries(AestraOscillatorTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraOscillatorTest COMMAND AestraOscillatorTest)

# Mixer Bus Test
add_executable(AestraMixerBusTest AestraAudio/MixerBusTest.cpp)
target_link_libraries(AestraMixerBusTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraMixerBusTest COMMAND AestraMixerBusTest)

# Sample Rate Converter Test
add_executable(AestraSampleRateConverterTest AestraAudio/SampleRateConverterTest.cpp)
target_link_libraries(AestraSampleRateConverterTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraSampleRateConverterTest COMMAND AestraSampleRateConverterTest)

# Serialization Compatibility Test (Requires GTest)
# add_executable(AestraSerializationCompatibilityTest AestraAudio/SerializationCompatibilityTest.cpp)
# target_link_libraries(AestraSerializationCompatibilityTest PRIVATE AestraAudio AestraCore)
# add_test(NAME AestraSerializationCompatibilityTest COMMAND AestraSerializationCompatibilityTest)

# =============================================================================
# AestraPlat Tests
# =============================================================================

# Platform DPI Test
add_executable(PlatformDPITest AestraPlat/PlatformDPITest.cpp)
target_link_libraries(PlatformDPITest PRIVATE AestraPlat AestraCore)

# Platform Window Test  
add_executable(PlatformWindowTest AestraPlat/PlatformWindowTest.cpp)
target_link_libraries(PlatformWindowTest PRIVATE AestraPlat AestraCore)

message(STATUS "Tests configured - see Tests/ directory")
