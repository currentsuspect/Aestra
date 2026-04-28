# =============================================================================
# AESTRA Tests - Centralized Test Configuration
# =============================================================================

cmake_minimum_required(VERSION 3.22)

enable_testing()

# Runtime/integration tests can be noisy or environment-dependent in CI.
# Keep them opt-in for clean default CI signal.
option(AESTRA_ENABLE_RUNTIME_TESTS "Enable runtime/integration tests that require host audio/devices or full project semantics" OFF)
option(AESTRA_ENABLE_EXPERIMENTAL_TESTS "Enable currently unstable/under-investigation tests in default CTest run" OFF)

# Test targets must behave the same in Release as they do in Debug. Several
# legacy tests still use assert(), so keep assertions live for all test binaries.
if(MSVC)
    add_compile_options(/UNDEBUG)
else()
    add_compile_options(-UNDEBUG)
endif()

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
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/DSP
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Playback
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/IO
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Drivers
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Commands
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Headless
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_SOURCE_DIR}/Source
    )
    target_compile_definitions(ProjectRoundTripTest PRIVATE
        $<$<CONFIG:Debug>:AESTRA_ENABLE_PROJECT_LOAD_LOGS>
    )
    if(AESTRA_ENABLE_RUNTIME_TESTS)
        add_test(NAME ProjectRoundTripTest COMMAND ProjectRoundTripTest)
        add_test(NAME AutosaveRoundTripTest COMMAND ProjectRoundTripTest)
        set_tests_properties(ProjectRoundTripTest AutosaveRoundTripTest PROPERTIES LABELS "integration;runtime")
    else()
message(STATUS "ProjectRoundTrip tests built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
    endif()
endif()

# Project Round-Trip Integrity Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ProjectRoundTripIntegrityTest.cpp")
    add_executable(ProjectRoundTripIntegrityTest
        Integration/ProjectRoundTripIntegrityTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
    )
    target_link_libraries(ProjectRoundTripIntegrityTest PRIVATE AestraAudio AestraCore)
    target_include_directories(ProjectRoundTripIntegrityTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/DSP
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Playback
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/IO
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Drivers
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Commands
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Headless
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_SOURCE_DIR}/Source
    )
    target_compile_definitions(ProjectRoundTripIntegrityTest PRIVATE
        $<$<CONFIG:Debug>:AESTRA_ENABLE_PROJECT_LOAD_LOGS>
    )
    if(AESTRA_ENABLE_RUNTIME_TESTS)
        add_test(NAME ProjectRoundTripIntegrityTest COMMAND ProjectRoundTripIntegrityTest)
        set_tests_properties(ProjectRoundTripIntegrityTest PROPERTIES LABELS "integration;runtime")
    else()
        message(STATUS "ProjectRoundTripIntegrityTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
    endif()
endif()

# Project Load Regression Tests (routing safety, missing references, non-destructive load)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ProjectLoadRegressionTest.cpp")
    add_executable(ProjectLoadRegressionTest
        Integration/ProjectLoadRegressionTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
    )
    target_link_libraries(ProjectLoadRegressionTest PRIVATE AestraAudio AestraCore)
    target_include_directories(ProjectLoadRegressionTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/DSP
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Playback
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/IO
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Drivers
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Commands
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Headless
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_SOURCE_DIR}/Source
    )
    target_compile_definitions(ProjectLoadRegressionTest PRIVATE
        $<$<CONFIG:Debug>:AESTRA_ENABLE_PROJECT_LOAD_LOGS>
    )
    add_test(NAME ProjectLoadRegressionTest COMMAND ProjectLoadRegressionTest)
    set_tests_properties(ProjectLoadRegressionTest PROPERTIES LABELS "integration;regression;routing")
endif()

# Plugin Scanner Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/PluginScanTest.cpp")
    add_executable(AestraPluginScanTest Integration/PluginScanTest.cpp)
    target_link_libraries(AestraPluginScanTest PRIVATE AestraAudio AestraCore)
    target_include_directories(AestraPluginScanTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    if(WIN32)
        target_link_libraries(AestraPluginScanTest PRIVATE ole32)
    endif()
endif()

# Rumble Plugin Factory Test (requires runtime plugin system)
if(TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/RumblePluginFactoryTest.cpp")
    add_executable(RumblePluginFactoryTest Integration/RumblePluginFactoryTest.cpp)
    target_link_libraries(RumblePluginFactoryTest PRIVATE AestraAudioCore AestraPremiumPlugins AestraCore)
    target_include_directories(RumblePluginFactoryTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    if(AESTRA_ENABLE_RUNTIME_TESTS)
        add_test(NAME RumblePluginFactoryTest COMMAND RumblePluginFactoryTest)
        set_tests_properties(RumblePluginFactoryTest PROPERTIES LABELS "plugins;rumble;factory;runtime")
    else()
        message(STATUS "RumblePluginFactoryTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
    endif()
endif()

# Rumble Usage Path Test (requires runtime plugin system)
if(TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/RumbleUsagePathTest.cpp")
    add_executable(RumbleUsagePathTest Integration/RumbleUsagePathTest.cpp)
    target_link_libraries(RumbleUsagePathTest PRIVATE AestraAudioCore AestraPremiumPlugins AestraCore)
    target_include_directories(RumbleUsagePathTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    if(AESTRA_ENABLE_RUNTIME_TESTS)
        add_test(NAME RumbleUsagePathTest COMMAND RumbleUsagePathTest)
        set_tests_properties(RumbleUsagePathTest PROPERTIES LABELS "plugins;rumble;usage;runtime")
    else()
        message(STATUS "RumbleUsagePathTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
    endif()
endif()

# Rumble Discovery Test (requires runtime plugin system)
if(TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/RumbleDiscoveryTest.cpp")
    add_executable(RumbleDiscoveryTest Integration/RumbleDiscoveryTest.cpp)
    target_link_libraries(RumbleDiscoveryTest PRIVATE AestraAudioCore AestraPremiumPlugins AestraCore)
    target_include_directories(RumbleDiscoveryTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    if(AESTRA_ENABLE_RUNTIME_TESTS)
        add_test(NAME RumbleDiscoveryTest COMMAND RumbleDiscoveryTest)
        set_tests_properties(RumbleDiscoveryTest PROPERTIES LABELS "plugins;rumble;discovery;runtime")
    else()
        message(STATUS "RumbleDiscoveryTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
    endif()
endif()

# Internal Plugin Project Round-Trip Test (requires runtime plugin system)
if(TARGET AestraAudio AND TARGET AestraPremiumPlugins AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/InternalPluginProjectRoundTripTest.cpp")
    add_executable(InternalPluginProjectRoundTripTest
        Integration/InternalPluginProjectRoundTripTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
    )
    target_link_libraries(InternalPluginProjectRoundTripTest PRIVATE AestraAudio AestraCore)
    target_include_directories(InternalPluginProjectRoundTripTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/DSP
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Playback
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/IO
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Drivers
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Commands
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Headless
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_SOURCE_DIR}/Source
    )
    target_compile_definitions(InternalPluginProjectRoundTripTest PRIVATE
        $<$<CONFIG:Debug>:AESTRA_ENABLE_PROJECT_LOAD_LOGS>
    )
    if(AESTRA_ENABLE_RUNTIME_TESTS)
        add_test(NAME InternalPluginProjectRoundTripTest COMMAND InternalPluginProjectRoundTripTest)
        set_tests_properties(InternalPluginProjectRoundTripTest PROPERTIES LABELS "integration;plugins;project;runtime")
    else()
        message(STATUS "InternalPluginProjectRoundTripTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
    endif()
endif()

# Arsenal Instrument Attachment Test (requires runtime plugin system)
if(TARGET AestraAudio AND TARGET AestraPremiumPlugins AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ArsenalInstrumentAttachmentTest.cpp")
    add_executable(ArsenalInstrumentAttachmentTest Integration/ArsenalInstrumentAttachmentTest.cpp)
    target_link_libraries(ArsenalInstrumentAttachmentTest PRIVATE AestraAudio AestraCore)
    target_include_directories(ArsenalInstrumentAttachmentTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    if(AESTRA_ENABLE_RUNTIME_TESTS)
        add_test(NAME ArsenalInstrumentAttachmentTest COMMAND ArsenalInstrumentAttachmentTest)
        set_tests_properties(ArsenalInstrumentAttachmentTest PROPERTIES LABELS "integration;arsenal;plugins;runtime")
    else()
        message(STATUS "ArsenalInstrumentAttachmentTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
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
    # NOTE: No forced AVX flags here. SampleRateConverter uses runtime SIMD dispatch
    # (#pragma / hasAVX() checks). Forcing -mavx2 breaks on CPUs without AVX2 support.
endif()

# =============================================================================
# AestraAudio Tests
# =============================================================================

# Audio Test (runtime/device-dependent)
add_executable(AestraAudioTest AestraAudio/AudioTest.cpp)
target_link_libraries(AestraAudioTest PRIVATE AestraAudio AestraCore)
if(AESTRA_ENABLE_RUNTIME_TESTS)
    add_test(NAME AestraAudioTest COMMAND AestraAudioTest)
    set_tests_properties(AestraAudioTest PROPERTIES LABELS "audio;runtime;device")
else()
    message(STATUS "AestraAudioTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
endif()

# Soak Test (runtime/device-dependent)
add_executable(AestraAudioSoakTest AestraAudio/AudioEngineSoakTest.cpp)
target_link_libraries(AestraAudioSoakTest PRIVATE AestraAudio AestraCore)
if(AESTRA_ENABLE_RUNTIME_TESTS)
    add_test(NAME AestraAudioSoakTest COMMAND AestraAudioSoakTest)
    set_tests_properties(AestraAudioSoakTest PROPERTIES LABELS "audio;runtime;soak;long")
else()
    message(STATUS "AestraAudioSoakTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
endif()

# K-005: Driver-level Soak Test (runtime/device-dependent, exercises real audio path)
add_executable(AestraAudioDriverSoakTest AestraAudio/AudioDriverSoakTest.cpp)
target_link_libraries(AestraAudioDriverSoakTest PRIVATE AestraAudio AestraAudioCore AestraCore)
if(AESTRA_ENABLE_RUNTIME_TESTS)
    add_test(NAME AestraAudioDriverSoakTest COMMAND AestraAudioDriverSoakTest --duration-sec 60)
    set_tests_properties(AestraAudioDriverSoakTest PROPERTIES LABELS "audio;runtime;soak;driver;long")
else()
    message(STATUS "AestraAudioDriverSoakTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
endif()

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

# Reverb SIMD Benchmark
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbBenchmark.cpp")
    add_executable(AestraReverbBenchmark AestraAudio/ReverbBenchmark.cpp)
    target_link_libraries(AestraReverbBenchmark PRIVATE AestraAudio AestraCore)
    target_include_directories(AestraReverbBenchmark PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

# Reverb Quality Measurement Lab
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbQualityLab.cpp")
    add_executable(AestraReverbQualityLab AestraAudio/ReverbQualityLab.cpp)
    target_link_libraries(AestraReverbQualityLab PRIVATE AestraAudio AestraCore)
    target_include_directories(AestraReverbQualityLab PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

# Reverb Material Validation Lab
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbMaterialLab.cpp")
    add_executable(AestraReverbMaterialLab AestraAudio/ReverbMaterialLab.cpp)
    target_link_libraries(AestraReverbMaterialLab PRIVATE AestraAudio AestraCore)
    target_include_directories(AestraReverbMaterialLab PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME ReverbMaterialLabTest COMMAND AestraReverbMaterialLab)
endif()

# Stress benchmark companion used by dsp-benchmark workflow
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/AestraStressTest.cpp")
    add_executable(AestraStressTest Integration/AestraStressTest.cpp)
    target_link_libraries(AestraStressTest PRIVATE AestraAudioCore AestraCore)
    target_include_directories(AestraStressTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

# Waveform Lock Test
add_executable(AestraWaveformLockTest
    AestraAudio/WaveformLockTest.cpp
    ${CMAKE_SOURCE_DIR}/AestraAudio/src/IO/WaveformCache.cpp
)
target_link_libraries(AestraWaveformLockTest PRIVATE AestraAudio AestraCore)
if(AESTRA_ENABLE_EXPERIMENTAL_TESTS)
    add_test(NAME AestraWaveformLockTest COMMAND AestraWaveformLockTest)
    set_tests_properties(AestraWaveformLockTest PROPERTIES LABELS "experimental;unstable")
else()
    message(STATUS "AestraWaveformLockTest built but not registered (set AESTRA_ENABLE_EXPERIMENTAL_TESTS=ON to enable)")
endif()

# Waveform Cache Correctness Test
add_executable(AestraWaveformCacheTest
    AestraAudio/WaveformCacheTest.cpp
    ${CMAKE_SOURCE_DIR}/AestraAudio/src/IO/WaveformCache.cpp
)
target_link_libraries(AestraWaveformCacheTest PRIVATE AestraAudio AestraCore)
if(AESTRA_ENABLE_EXPERIMENTAL_TESTS)
    add_test(NAME AestraWaveformCacheTest COMMAND AestraWaveformCacheTest)
    set_tests_properties(AestraWaveformCacheTest PROPERTIES LABELS "experimental;unstable")
else()
    message(STATUS "AestraWaveformCacheTest built but not registered (set AESTRA_ENABLE_EXPERIMENTAL_TESTS=ON to enable)")
endif()

# Atomic Save Test (self-contained filesystem atomic-write smoke test)
add_executable(AestraAtomicSaveTest AestraAudio/AtomicSaveTest.cpp)
target_link_libraries(AestraAtomicSaveTest PRIVATE AestraCore)
add_test(NAME AestraAtomicSaveTest COMMAND AestraAtomicSaveTest)

# Watchdog Test (requires VST3 SDK headers)
if(EXISTS "${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk/pluginterfaces/vst/ivstaudioprocessor.h")
    add_executable(AestraWatchdogTest AestraAudio/WatchdogTest.cpp)
    target_link_libraries(AestraWatchdogTest PRIVATE AestraAudio AestraCore)
    add_test(NAME AestraWatchdogTest COMMAND AestraWatchdogTest)
    target_include_directories(AestraWatchdogTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk
        ${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk/pluginterfaces
        ${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk/base
        ${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk/public.sdk
    )
else()
    message(STATUS "AestraWatchdogTest skipped (VST3 SDK headers not found)")
endif()

# Crash Test (requires VST3 SDK headers)
if(EXISTS "${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk/pluginterfaces/vst/ivstaudioprocessor.h")
    add_executable(AestraCrashTest AestraAudio/CrashTest.cpp)
    target_link_libraries(AestraCrashTest PRIVATE AestraAudio AestraCore)
    add_test(NAME AestraCrashTest COMMAND AestraCrashTest)
    target_include_directories(AestraCrashTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk
        ${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk/pluginterfaces
        ${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk/base
        ${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk/public.sdk
    )
else()
    message(STATUS "AestraCrashTest skipped (VST3 SDK headers not found)")
endif()

# Wav Loader Test
add_executable(AestraWavLoaderTest AestraAudio/WavLoaderTest.cpp)
target_link_libraries(AestraWavLoaderTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraWavLoaderTest COMMAND AestraWavLoaderTest)

# Device Manager Test (Windows only)
if(WIN32 AND AESTRA_ENABLE_RUNTIME_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/DeviceManagerTest.cpp")
    add_executable(AestraDeviceManagerTest AestraAudio/DeviceManagerTest.cpp)
    target_link_libraries(AestraDeviceManagerTest PRIVATE AestraAudio AestraCore)
    target_link_libraries(AestraDeviceManagerTest PRIVATE avrt)
    add_test(NAME RuntimeAestraDeviceManagerTest COMMAND AestraDeviceManagerTest)
    set_tests_properties(RuntimeAestraDeviceManagerTest PROPERTIES LABELS "Runtime;audio;device")
elseif(WIN32)
    message(STATUS "AestraDeviceManagerTest skipped in deterministic test mode (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
endif()

# Audio Callback Test
add_executable(AestraAudioCallbackTest AestraAudio/AudioCallbackTest.cpp)
target_link_libraries(AestraAudioCallbackTest PRIVATE AestraAudio AestraCore)
if(AESTRA_ENABLE_EXPERIMENTAL_TESTS)
    add_test(NAME AestraAudioCallbackTest COMMAND AestraAudioCallbackTest)
    set_tests_properties(AestraAudioCallbackTest PROPERTIES LABELS "experimental;unstable")
else()
    message(STATUS "AestraAudioCallbackTest built but not registered (set AESTRA_ENABLE_EXPERIMENTAL_TESTS=ON to enable)")
endif()

# Filter Test
add_executable(AestraFilterTest AestraAudio/FilterTest.cpp)
target_link_libraries(AestraFilterTest PRIVATE AestraAudio AestraCore)
if(AESTRA_ENABLE_EXPERIMENTAL_TESTS)
    add_test(NAME AestraFilterTest COMMAND AestraFilterTest)
    set_tests_properties(AestraFilterTest PROPERTIES LABELS "experimental;unstable")
else()
    message(STATUS "AestraFilterTest built but not registered (set AESTRA_ENABLE_EXPERIMENTAL_TESTS=ON to enable)")
endif()

# Oscillator Test
add_executable(AestraOscillatorTest AestraAudio/OscillatorTest.cpp)
target_link_libraries(AestraOscillatorTest PRIVATE AestraAudio AestraCore)
target_compile_definitions(AestraOscillatorTest PRIVATE AESTRA_HEADLESS)
add_test(NAME AestraOscillatorTest COMMAND AestraOscillatorTest)

# Automation Stress Test
add_executable(AutomationStressTest AestraAudio/AutomationStressTest.cpp)
target_link_libraries(AutomationStressTest PRIVATE AestraAudio AestraCore)
target_compile_definitions(AutomationStressTest PRIVATE AESTRA_HEADLESS)
add_test(NAME AutomationStressTest COMMAND AutomationStressTest)

# Arsenal Parameter Stress Test (requires runtime plugin system)
if(TARGET AestraAudioCore AND TARGET AestraPremiumPlugins)
    add_executable(ArsenalParameterStressTest AestraAudio/ArsenalParameterStressTest.cpp)
    target_link_libraries(ArsenalParameterStressTest PRIVATE AestraPremiumPlugins AestraAudioCore AestraCore)
    target_compile_definitions(ArsenalParameterStressTest PRIVATE AESTRA_HEADLESS)
    if(AESTRA_ENABLE_RUNTIME_TESTS)
        add_test(NAME ArsenalParameterStressTest COMMAND ArsenalParameterStressTest)
    else()
        message(STATUS "ArsenalParameterStressTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
    endif()
endif()

# Mixer Bus Stress Test
add_executable(MixerBusStressTest AestraAudio/MixerBusStressTest.cpp)
target_link_libraries(MixerBusStressTest PRIVATE AestraAudio AestraCore)
target_compile_definitions(MixerBusStressTest PRIVATE AESTRA_HEADLESS)
add_test(NAME MixerBusStressTest COMMAND MixerBusStressTest)

# Mixer Bus Test
add_executable(AestraMixerBusTest AestraAudio/MixerBusTest.cpp)
target_link_libraries(AestraMixerBusTest PRIVATE AestraAudio AestraCore)
target_compile_definitions(AestraMixerBusTest PRIVATE AESTRA_HEADLESS)
add_test(NAME AestraMixerBusTest COMMAND AestraMixerBusTest)

# AestraUUID Test (header-only, no audio hardware required)
add_executable(AestraUUIDTest AestraAudio/AestraUUIDTest.cpp)
target_link_libraries(AestraUUIDTest PRIVATE AestraAudioCore AestraCore)
target_include_directories(AestraUUIDTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraUUIDTest COMMAND AestraUUIDTest)
set_tests_properties(AestraUUIDTest PROPERTIES LABELS "audio;uuid")

# Arsenal route-mode compatibility test
add_executable(ArsenalRouteModeCompatibilityTest AestraAudio/ArsenalRouteModeCompatibilityTest.cpp)
target_link_libraries(ArsenalRouteModeCompatibilityTest PRIVATE AestraAudioCore AestraCore)
target_include_directories(ArsenalRouteModeCompatibilityTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ArsenalRouteModeCompatibilityTest COMMAND ArsenalRouteModeCompatibilityTest)
set_tests_properties(ArsenalRouteModeCompatibilityTest PROPERTIES LABELS "audio;arsenal")

# Arsenal processing context routing test
add_executable(ArsenalProcessingContextRoutingTest AestraAudio/ArsenalProcessingContextRoutingTest.cpp)
target_link_libraries(ArsenalProcessingContextRoutingTest PRIVATE AestraAudioCore AestraCore)
target_include_directories(ArsenalProcessingContextRoutingTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ArsenalProcessingContextRoutingTest COMMAND ArsenalProcessingContextRoutingTest)
set_tests_properties(ArsenalProcessingContextRoutingTest PROPERTIES LABELS "audio;arsenal")

# Arsenal route-mode project round-trip compatibility test
add_executable(ArsenalRouteModeRoundTripTest
    AestraAudio/ArsenalRouteModeRoundTripTest.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
)
target_link_libraries(ArsenalRouteModeRoundTripTest PRIVATE AestraAudio AestraCore)
target_include_directories(ArsenalRouteModeRoundTripTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/DSP
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Playback
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/IO
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Drivers
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Commands
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Headless
    ${CMAKE_SOURCE_DIR}/AestraCore/include
    ${CMAKE_SOURCE_DIR}/Source
)
add_test(NAME ArsenalRouteModeRoundTripTest COMMAND ArsenalRouteModeRoundTripTest)
set_tests_properties(ArsenalRouteModeRoundTripTest PROPERTIES LABELS "audio;arsenal;roundtrip")

# Arsenal route-mode policy contract test (Phase 2B)
add_executable(ArsenalRouteModePolicyTest AestraAudio/ArsenalRouteModePolicyTest.cpp)
target_link_libraries(ArsenalRouteModePolicyTest PRIVATE AestraAudioCore AestraCore)
target_include_directories(ArsenalRouteModePolicyTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ArsenalRouteModePolicyTest COMMAND ArsenalRouteModePolicyTest)
set_tests_properties(ArsenalRouteModePolicyTest PROPERTIES LABELS "audio;arsenal;policy")

# Arsenal bridge-mode terminology contract test (Phase 2B policy-only)
add_executable(ArsenalBridgeContractTest AestraAudio/ArsenalBridgeContractTest.cpp)
target_link_libraries(ArsenalBridgeContractTest PRIVATE AestraCore)
target_include_directories(ArsenalBridgeContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ArsenalBridgeContractTest COMMAND ArsenalBridgeContractTest)
set_tests_properties(ArsenalBridgeContractTest PROPERTIES LABELS "arsenal;policy")

# Arsenal export current-policy parity guard (Phase 2C)
add_executable(ArsenalExportCurrentPolicyTest AestraAudio/ArsenalExportCurrentPolicyTest.cpp)
target_link_libraries(ArsenalExportCurrentPolicyTest PRIVATE AestraAudioCore AestraCore)
target_include_directories(ArsenalExportCurrentPolicyTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ArsenalExportCurrentPolicyTest COMMAND ArsenalExportCurrentPolicyTest)
set_tests_properties(ArsenalExportCurrentPolicyTest PROPERTIES LABELS "arsenal;policy;offline")

# Arsenal live/export parity hardening test (Phase 3)
add_executable(ArsenalExportLiveParityTest AestraAudio/ArsenalExportLiveParityTest.cpp)
target_link_libraries(ArsenalExportLiveParityTest PRIVATE AestraAudio AestraCore)
target_include_directories(ArsenalExportLiveParityTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/DSP
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Playback
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/IO
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Drivers
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Commands
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Headless
    ${CMAKE_SOURCE_DIR}/AestraCore/include
    ${CMAKE_SOURCE_DIR}/Source
)
add_test(NAME ArsenalExportLiveParityTest COMMAND ArsenalExportLiveParityTest)
set_tests_properties(ArsenalExportLiveParityTest PROPERTIES LABELS "arsenal;offline;policy;regression")

# Arsenal bridge metadata round-trip persistence guard (Phase 2D)
add_executable(ArsenalBridgeModeRoundTripTest AestraAudio/ArsenalBridgeModeRoundTripTest.cpp)
target_link_libraries(ArsenalBridgeModeRoundTripTest PRIVATE AestraAudioCore AestraCore)
target_include_directories(ArsenalBridgeModeRoundTripTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ArsenalBridgeModeRoundTripTest COMMAND ArsenalBridgeModeRoundTripTest)
set_tests_properties(ArsenalBridgeModeRoundTripTest PROPERTIES LABELS "arsenal;policy;roundtrip")

# Sample Rate Converter Test
add_executable(AestraSampleRateConverterTest AestraAudio/SampleRateConverterTest.cpp)
target_link_libraries(AestraSampleRateConverterTest PRIVATE AestraAudio AestraCore)
if(AESTRA_ENABLE_EXPERIMENTAL_TESTS)
    add_test(NAME AestraSampleRateConverterTest COMMAND AestraSampleRateConverterTest)
    set_tests_properties(AestraSampleRateConverterTest PROPERTIES LABELS "experimental;unstable")
else()
    message(STATUS "AestraSampleRateConverterTest built but not registered (set AESTRA_ENABLE_EXPERIMENTAL_TESTS=ON to enable)")
endif()

# Recording Path Test (headless, no audio hardware required)
add_executable(RecordingPathTest
    AestraAudio/RecordingPathTest.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
)
target_link_libraries(RecordingPathTest PRIVATE AestraAudio AestraCore)
target_include_directories(RecordingPathTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/DSP
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Playback
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/IO
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Drivers
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Commands
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Headless
    ${CMAKE_SOURCE_DIR}/AestraCore/include
    ${CMAKE_SOURCE_DIR}/Source
)
add_test(NAME RecordingPathTest COMMAND RecordingPathTest)
set_tests_properties(RecordingPathTest PROPERTIES LABELS "audio;recording;headless")

# Aestra EQ Plugin Test
add_executable(AestraEQTest AestraAudio/AestraEQTest.cpp)
target_link_libraries(AestraEQTest PRIVATE AestraAudio AestraCore)
target_include_directories(AestraEQTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraEQTest COMMAND AestraEQTest)
set_tests_properties(AestraEQTest PROPERTIES LABELS "audio;plugins;eq")

# Aestra Comp Phase 0 Test
add_executable(AestraCompPhase0Test AestraAudio/AestraCompPhase0Test.cpp)
target_link_libraries(AestraCompPhase0Test PRIVATE AestraAudio AestraCore)
target_include_directories(AestraCompPhase0Test PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraCompPhase0Test COMMAND AestraCompPhase0Test)
set_tests_properties(AestraCompPhase0Test PROPERTIES LABELS "audio;plugins;comp;phase0")

# Aestra Comp Phase 1 Test
add_executable(AestraCompPhase1Test AestraAudio/AestraCompPhase1Test.cpp)
target_link_libraries(AestraCompPhase1Test PRIVATE AestraAudio AestraCore)
target_include_directories(AestraCompPhase1Test PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraCompPhase1Test COMMAND AestraCompPhase1Test)
set_tests_properties(AestraCompPhase1Test PROPERTIES LABELS "audio;plugins;comp;phase1")

# Aestra Comp Upgrade Test
add_executable(AestraCompUpgradeTest AestraAudio/AestraCompUpgradeTest.cpp)
target_link_libraries(AestraCompUpgradeTest PRIVATE AestraAudio AestraCore)
target_include_directories(AestraCompUpgradeTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraCompUpgradeTest COMMAND AestraCompUpgradeTest)
set_tests_properties(AestraCompUpgradeTest PROPERTIES LABELS "audio;plugins;comp;upgrade")

# Aestra Delay Upgrade Test
add_executable(AestraDelayUpgradeTest AestraAudio/AestraDelayUpgradeTest.cpp)
target_link_libraries(AestraDelayUpgradeTest PRIVATE AestraAudio AestraCore)
target_include_directories(AestraDelayUpgradeTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraDelayUpgradeTest COMMAND AestraDelayUpgradeTest)
set_tests_properties(AestraDelayUpgradeTest PROPERTIES LABELS "audio;plugins;delay;upgrade")

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

# =============================================================================
# AestraUI Tests
# =============================================================================

# Text Renderer STB Space Glyph Test (Issue #121 regression test)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraUI/TextRendererSTBSpaceTest.cpp")
    add_executable(TextRendererSTBSpaceTest AestraUI/TextRendererSTBSpaceTest.cpp)
    add_test(NAME TextRendererSTBSpaceTest COMMAND TextRendererSTBSpaceTest)
    set_tests_properties(TextRendererSTBSpaceTest PROPERTIES LABELS "ui;regression")
    message(STATUS "TextRendererSTBSpaceTest added - Issue #121 regression test")
endif()

# =============================================================================
# Commands Tests (Phase 2 - Undo/Redo System)
# =============================================================================

# CommandHistory Test
add_executable(CommandHistoryTest Commands/CommandHistoryTest.cpp)
target_link_libraries(CommandHistoryTest PRIVATE AestraAudioCore AestraCore)
target_include_directories(CommandHistoryTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME CommandHistoryTest COMMAND CommandHistoryTest)
set_tests_properties(CommandHistoryTest PROPERTIES LABELS "commands;phase2")

# MoveClipCommand Test
add_executable(MoveClipCommandTest Commands/MoveClipCommandTest.cpp)
target_link_libraries(MoveClipCommandTest PRIVATE AestraAudioCore AestraCore)
target_include_directories(MoveClipCommandTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME MoveClipCommandTest COMMAND MoveClipCommandTest)
set_tests_properties(MoveClipCommandTest PROPERTIES LABELS "commands;phase2")

# MacroCommand Test
add_executable(MacroCommandTest Commands/MacroCommandTest.cpp)
target_link_libraries(MacroCommandTest PRIVATE AestraAudioCore AestraCore)
target_include_directories(MacroCommandTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME MacroCommandTest COMMAND MacroCommandTest)
set_tests_properties(MacroCommandTest PROPERTIES LABELS "commands;phase2")

# Mixer Commands Test (Volume, Pan, Mute, Solo)
add_executable(MixerCommandsTest Commands/MixerCommandsTest.cpp)
target_link_libraries(MixerCommandsTest PRIVATE AestraAudioCore AestraCore)
target_include_directories(MixerCommandsTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME MixerCommandsTest COMMAND MixerCommandsTest)
set_tests_properties(MixerCommandsTest PROPERTIES LABELS "commands;phase2;mixer")

# Clip Commands Test (Trim, Duplicate)
add_executable(ClipCommandsTest Commands/ClipCommandsTest.cpp)
target_link_libraries(ClipCommandsTest PRIVATE AestraAudioCore AestraCore)
target_include_directories(ClipCommandsTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ClipCommandsTest COMMAND ClipCommandsTest)
set_tests_properties(ClipCommandsTest PROPERTIES LABELS "commands;phase2;clip")

# CommandTransaction Test
add_executable(CommandTransactionTest Commands/CommandTransactionTest.cpp)
target_link_libraries(CommandTransactionTest PRIVATE AestraAudioCore AestraCore)
target_include_directories(CommandTransactionTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME CommandTransactionTest COMMAND CommandTransactionTest)
set_tests_properties(CommandTransactionTest PROPERTIES LABELS "commands;phase2")

# Rumble State Test (requires runtime plugin system)
if(TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Commands/RumbleStateTest.cpp")
    add_executable(RumbleStateTest Commands/RumbleStateTest.cpp)
    target_link_libraries(RumbleStateTest PRIVATE AestraPremiumPlugins AestraAudioCore AestraCore)
    target_include_directories(RumbleStateTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    if(AESTRA_ENABLE_RUNTIME_TESTS)
        add_test(NAME RumbleStateTest COMMAND RumbleStateTest)
        set_tests_properties(RumbleStateTest PROPERTIES LABELS "plugins;rumble;state;runtime")
    else()
        message(STATUS "RumbleStateTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
    endif()
endif()

message(STATUS "Commands tests added - Phase 2 undo/redo system")

# =============================================================================
# Headless Tests (Deterministic, no audio hardware required)
# =============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Headless/CMakeLists.txt")
    add_subdirectory(Headless)
endif()

# Security regression tests
if(AESTRA_ENABLE_TESTS AND EXISTS ${CMAKE_SOURCE_DIR}/tests/security/CMakeLists.txt)
    add_subdirectory(${CMAKE_SOURCE_DIR}/tests/security security_tests)
endif()

message(STATUS "Tests configured - see Tests/ directory")
