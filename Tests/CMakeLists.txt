# =============================================================================
# AESTRA Tests - Centralized Test Configuration
# =============================================================================

cmake_minimum_required(VERSION 3.22)

enable_testing()

# Runtime/integration tests can be noisy or environment-dependent in CI.
# Keep them opt-in for clean default CI signal.
option(AESTRA_ENABLE_RUNTIME_TESTS "Enable runtime/integration tests that require host audio/devices or full project semantics" OFF)
option(AESTRA_ENABLE_EXPERIMENTAL_TESTS "Enable currently unstable/under-investigation tests in default CTest run" OFF)

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
    if(AESTRA_ENABLE_RUNTIME_TESTS)
        add_test(NAME ProjectRoundTripTest COMMAND ProjectRoundTripTest)
        add_test(NAME AutosaveRoundTripTest COMMAND ProjectRoundTripTest)
        set_tests_properties(ProjectRoundTripTest AutosaveRoundTripTest PROPERTIES LABELS "integration;runtime")
    else()
        message(STATUS "ProjectRoundTrip tests built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
    endif()
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

# Rumble Plugin Factory Test
if(TARGET AestraAudioCore AND TARGET AestraRumble AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/RumblePluginFactoryTest.cpp")
    add_executable(RumblePluginFactoryTest Integration/RumblePluginFactoryTest.cpp)
    target_link_libraries(RumblePluginFactoryTest PRIVATE AestraAudioCore AestraRumble AestraCore)
    target_include_directories(RumblePluginFactoryTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME RumblePluginFactoryTest COMMAND RumblePluginFactoryTest)
    set_tests_properties(RumblePluginFactoryTest PROPERTIES LABELS "plugins;rumble;factory")
endif()

# Rumble Usage Path Test
if(TARGET AestraAudioCore AND TARGET AestraRumble AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/RumbleUsagePathTest.cpp")
    add_executable(RumbleUsagePathTest Integration/RumbleUsagePathTest.cpp)
    target_link_libraries(RumbleUsagePathTest PRIVATE AestraAudioCore AestraRumble AestraCore)
    target_include_directories(RumbleUsagePathTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME RumbleUsagePathTest COMMAND RumbleUsagePathTest)
    set_tests_properties(RumbleUsagePathTest PROPERTIES LABELS "plugins;rumble;usage")
endif()

# Rumble Discovery Test
if(TARGET AestraAudioCore AND TARGET AestraRumble AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/RumbleDiscoveryTest.cpp")
    add_executable(RumbleDiscoveryTest Integration/RumbleDiscoveryTest.cpp)
    target_link_libraries(RumbleDiscoveryTest PRIVATE AestraAudioCore AestraRumble AestraCore)
    target_include_directories(RumbleDiscoveryTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME RumbleDiscoveryTest COMMAND RumbleDiscoveryTest)
    set_tests_properties(RumbleDiscoveryTest PROPERTIES LABELS "plugins;rumble;discovery")
endif()

# Internal Plugin Project Round-Trip Test
if(TARGET AestraAudio AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/InternalPluginProjectRoundTripTest.cpp")
    add_executable(InternalPluginProjectRoundTripTest
        Integration/InternalPluginProjectRoundTripTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
    )
    target_link_libraries(InternalPluginProjectRoundTripTest PRIVATE AestraAudio AestraCore)
    target_include_directories(InternalPluginProjectRoundTripTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_SOURCE_DIR}/Source
    )
    target_compile_definitions(InternalPluginProjectRoundTripTest PRIVATE
        $<$<CONFIG:Debug>:AESTRA_ENABLE_PROJECT_LOAD_LOGS>
    )
    add_test(NAME InternalPluginProjectRoundTripTest COMMAND InternalPluginProjectRoundTripTest)
    set_tests_properties(InternalPluginProjectRoundTripTest PROPERTIES LABELS "integration;plugins;project")
endif()

# Arsenal Instrument Attachment Test
if(TARGET AestraAudio AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ArsenalInstrumentAttachmentTest.cpp")
    add_executable(ArsenalInstrumentAttachmentTest Integration/ArsenalInstrumentAttachmentTest.cpp)
    target_link_libraries(ArsenalInstrumentAttachmentTest PRIVATE AestraAudio AestraCore)
    target_include_directories(ArsenalInstrumentAttachmentTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME ArsenalInstrumentAttachmentTest COMMAND ArsenalInstrumentAttachmentTest)
    set_tests_properties(ArsenalInstrumentAttachmentTest PROPERTIES LABELS "integration;arsenal;plugins")
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
    # Enable AVX2 if available (x86 only, skip on ARM/Apple Silicon)
    message(STATUS "ResamplerBenchmark: CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686" AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
        if(MSVC)
            target_compile_options(ResamplerBenchmark PRIVATE /arch:AVX2)
        else()
            target_compile_options(ResamplerBenchmark PRIVATE -mavx2 -mfma)
        endif()
    endif()
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
if(AESTRA_ENABLE_EXPERIMENTAL_TESTS)
    add_test(NAME AestraWaveformLockTest COMMAND AestraWaveformLockTest)
    set_tests_properties(AestraWaveformLockTest PROPERTIES LABELS "experimental;unstable")
else()
    message(STATUS "AestraWaveformLockTest built but not registered (set AESTRA_ENABLE_EXPERIMENTAL_TESTS=ON to enable)")
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
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/DeviceManagerTest.cpp")
    add_executable(AestraDeviceManagerTest AestraAudio/DeviceManagerTest.cpp)
    target_link_libraries(AestraDeviceManagerTest PRIVATE AestraAudio AestraCore)
    target_link_libraries(AestraDeviceManagerTest PRIVATE avrt)
    add_test(NAME AestraDeviceManagerTest COMMAND AestraDeviceManagerTest)
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
add_test(NAME AestraOscillatorTest COMMAND AestraOscillatorTest)

# Mixer Bus Test
add_executable(AestraMixerBusTest AestraAudio/MixerBusTest.cpp)
target_link_libraries(AestraMixerBusTest PRIVATE AestraAudio AestraCore)
add_test(NAME AestraMixerBusTest COMMAND AestraMixerBusTest)

# Sample Rate Converter Test
add_executable(AestraSampleRateConverterTest AestraAudio/SampleRateConverterTest.cpp)
target_link_libraries(AestraSampleRateConverterTest PRIVATE AestraAudio AestraCore)
if(AESTRA_ENABLE_EXPERIMENTAL_TESTS)
    add_test(NAME AestraSampleRateConverterTest COMMAND AestraSampleRateConverterTest)
    set_tests_properties(AestraSampleRateConverterTest PROPERTIES LABELS "experimental;unstable")
else()
    message(STATUS "AestraSampleRateConverterTest built but not registered (set AESTRA_ENABLE_EXPERIMENTAL_TESTS=ON to enable)")
endif()

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

# Rumble State Test
if(TARGET AestraAudioCore AND TARGET AestraRumble AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Commands/RumbleStateTest.cpp")
    add_executable(RumbleStateTest Commands/RumbleStateTest.cpp)
    target_link_libraries(RumbleStateTest PRIVATE AestraRumble AestraAudioCore AestraCore)
    target_include_directories(RumbleStateTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME RumbleStateTest COMMAND RumbleStateTest)
    set_tests_properties(RumbleStateTest PROPERTIES LABELS "plugins;rumble;state")
endif()

message(STATUS "Commands tests added - Phase 2 undo/redo system")

# =============================================================================
# Headless Tests (Deterministic, no audio hardware required)
# =============================================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Headless/CMakeLists.txt")
    add_subdirectory(Headless)
endif()

message(STATUS "Tests configured - see Tests/ directory")
