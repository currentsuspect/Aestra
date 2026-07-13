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
# License Tests
# =============================================================================

if(TARGET AestraLicense)
    add_executable(LocalAccountCacheTest AestraLicense/LocalAccountCacheTest.cpp)
    target_link_libraries(LocalAccountCacheTest PRIVATE AestraLicense)
    target_include_directories(LocalAccountCacheTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraLicense/include
    )
    add_test(NAME LocalAccountCacheTest COMMAND LocalAccountCacheTest)
    set_tests_properties(LocalAccountCacheTest PROPERTIES LABELS "license;account;security")
else()
    message(STATUS "LocalAccountCacheTest skipped (AestraLicense target unavailable)")
endif()

# =============================================================================
# Integration Tests (Cross-Module)
# =============================================================================

# VST3 Plugin Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/VST3PluginTest.cpp")
    add_executable(VST3PluginTest Integration/VST3PluginTest.cpp)
    target_link_libraries(VST3PluginTest PRIVATE AestraAudioCore)
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

# Test temp-directory reliability stress test — proves makeUniqueTempDirectory
# never hands two callers the same path under heavy concurrency (regression
# guard for the ctest -j temp-dir collision).
add_executable(TestTempDirectoryStressTest Support/TestTempDirectoryStressTest.cpp)
find_package(Threads REQUIRED)
target_link_libraries(TestTempDirectoryStressTest PRIVATE Threads::Threads)
add_test(NAME TestTempDirectoryStressTest COMMAND TestTempDirectoryStressTest)
set_tests_properties(TestTempDirectoryStressTest PROPERTIES LABELS "support;infrastructure")

# Project RoundTrip Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ProjectRoundTripTest.cpp")
    add_executable(ProjectRoundTripTest
        Integration/ProjectRoundTripTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
    )
    target_link_libraries(ProjectRoundTripTest PRIVATE AestraAudio)
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
    # Registered unconditionally: exercises ProjectSerializer + TrackManager only —
    # no audio device or host runtime needed (verified headless). Serialization is
    # AGENTS.md §12 high-risk; roundtrip coverage must run on every CI build.
    add_test(NAME ProjectRoundTripTest COMMAND ProjectRoundTripTest)
    add_test(NAME AutosaveRoundTripTest COMMAND ProjectRoundTripTest)
    set_tests_properties(ProjectRoundTripTest AutosaveRoundTripTest PROPERTIES LABELS "integration;serialization")
endif()

# Session Recovery Test (autosave + crash flag + recovery flow)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/SessionRecoveryTest.cpp")
    add_executable(SessionRecoveryTest
        Integration/SessionRecoveryTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
    )
    target_link_libraries(SessionRecoveryTest PRIVATE AestraAudio)
    target_include_directories(SessionRecoveryTest PRIVATE
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
    target_compile_definitions(SessionRecoveryTest PRIVATE
        $<$<CONFIG:Debug>:AESTRA_ENABLE_PROJECT_LOAD_LOGS>
    )
    add_test(NAME SessionRecoveryTest COMMAND SessionRecoveryTest)
    set_tests_properties(SessionRecoveryTest PROPERTIES LABELS "integration;recovery;autosave")
endif()

# AutosaveManager unit test (background thread, dirty tracking, backup rotation)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/AutosaveManagerTest.cpp")
    add_executable(AutosaveManagerTest AestraAudio/AutosaveManagerTest.cpp)
    target_link_libraries(AutosaveManagerTest PRIVATE AestraAudioCore)
    target_include_directories(AutosaveManagerTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME AutosaveManagerTest COMMAND AutosaveManagerTest)
    set_tests_properties(AutosaveManagerTest PROPERTIES LABELS "unit;autosave;recovery")
endif()

# Project Round-Trip Integrity Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ProjectRoundTripIntegrityTest.cpp")
    add_executable(ProjectRoundTripIntegrityTest
        Integration/ProjectRoundTripIntegrityTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
    )
    target_link_libraries(ProjectRoundTripIntegrityTest PRIVATE AestraAudio)
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
    # Registered unconditionally: pure serializer/model roundtrip, no device
    # dependency (verified headless). See ProjectRoundTripTest note above.
    add_test(NAME ProjectRoundTripIntegrityTest COMMAND ProjectRoundTripIntegrityTest)
    set_tests_properties(ProjectRoundTripIntegrityTest PROPERTIES LABELS "integration;serialization")
endif()

# Project Integrity Check Test — corrupt files must not load silently (#263)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ProjectIntegrityCheckTest.cpp")
    add_executable(ProjectIntegrityCheckTest
        Integration/ProjectIntegrityCheckTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
    )
    target_link_libraries(ProjectIntegrityCheckTest PRIVATE AestraAudio)
    target_include_directories(ProjectIntegrityCheckTest PRIVATE
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
    target_compile_definitions(ProjectIntegrityCheckTest PRIVATE
        AESTRA_PROJECT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/ProjectFormat"
        $<$<CONFIG:Debug>:AESTRA_ENABLE_PROJECT_LOAD_LOGS>
    )
    add_test(NAME ProjectIntegrityCheckTest COMMAND ProjectIntegrityCheckTest)
    set_tests_properties(ProjectIntegrityCheckTest PROPERTIES LABELS "integration;serialization")
endif()

# Project Fixture Corpus Test — checked-in v1 files are the compatibility contract (#249)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ProjectFixtureCorpusTest.cpp")
    add_executable(ProjectFixtureCorpusTest
        Integration/ProjectFixtureCorpusTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
    )
    target_link_libraries(ProjectFixtureCorpusTest PRIVATE AestraAudio)
    target_include_directories(ProjectFixtureCorpusTest PRIVATE
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
    target_compile_definitions(ProjectFixtureCorpusTest PRIVATE
        AESTRA_PROJECT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/ProjectFormat"
        $<$<CONFIG:Debug>:AESTRA_ENABLE_PROJECT_LOAD_LOGS>
    )
    add_test(NAME ProjectFixtureCorpusTest COMMAND ProjectFixtureCorpusTest)
    set_tests_properties(ProjectFixtureCorpusTest PROPERTIES LABELS "integration;serialization")
endif()

# Project Value Fidelity Test — numeric fields must roundtrip bit-exactly (#273)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ProjectValueFidelityTest.cpp")
    add_executable(ProjectValueFidelityTest
        Integration/ProjectValueFidelityTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
    )
    target_link_libraries(ProjectValueFidelityTest PRIVATE AestraAudio)
    target_include_directories(ProjectValueFidelityTest PRIVATE
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
    add_test(NAME ProjectValueFidelityTest COMMAND ProjectValueFidelityTest)
    set_tests_properties(ProjectValueFidelityTest PROPERTIES LABELS "integration;serialization")
endif()

# Project Load Regression Tests (routing safety, missing references, non-destructive load)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ProjectLoadRegressionTest.cpp")
    add_executable(ProjectLoadRegressionTest
        Integration/ProjectLoadRegressionTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
    )
    target_link_libraries(ProjectLoadRegressionTest PRIVATE AestraAudio)
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
        AESTRA_PROJECT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/ProjectFormat"
        $<$<CONFIG:Debug>:AESTRA_ENABLE_PROJECT_LOAD_LOGS>
    )
    add_test(NAME ProjectLoadRegressionTest COMMAND ProjectLoadRegressionTest)
    set_tests_properties(ProjectLoadRegressionTest PROPERTIES LABELS "integration;regression;routing")
endif()

# Takes manifest/snapshot regression test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/TakeManagerTest.cpp")
    add_executable(TakeManagerTest
        Integration/TakeManagerTest.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
        ${CMAKE_SOURCE_DIR}/Source/Core/TakeManager.cpp
    )
    target_link_libraries(TakeManagerTest PRIVATE AestraAudio)
    target_include_directories(TakeManagerTest PRIVATE
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
        ${CMAKE_SOURCE_DIR}/Source/Core
    )
    add_test(NAME TakeManagerTest COMMAND TakeManagerTest)
    set_tests_properties(TakeManagerTest PROPERTIES LABELS "integration;takes;project")
endif()

# Plugin Scanner Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/PluginScanTest.cpp")
    add_executable(AestraPluginScanTest Integration/PluginScanTest.cpp)
    target_link_libraries(AestraPluginScanTest PRIVATE AestraAudio)
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
    target_link_libraries(RumblePluginFactoryTest PRIVATE AestraAudioCore AestraPremiumPlugins)
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
    target_link_libraries(RumbleUsagePathTest PRIVATE AestraAudioCore AestraPremiumPlugins)
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
    target_link_libraries(RumbleDiscoveryTest PRIVATE AestraAudioCore AestraPremiumPlugins)
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
    target_link_libraries(InternalPluginProjectRoundTripTest PRIVATE AestraAudio)
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
    target_link_libraries(ArsenalInstrumentAttachmentTest PRIVATE AestraAudio)
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
    target_link_libraries(ResamplerBenchmark PRIVATE AestraAudioCore)
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

# Resampler War Test: deterministic SRC quality gate with optional references.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SAMPLERATE QUIET samplerate)
    pkg_check_modules(SOXR QUIET soxr)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/ResamplerWarTest.cpp")
    add_executable(ResamplerWarTest Integration/ResamplerWarTest.cpp)
    target_link_libraries(ResamplerWarTest PRIVATE AestraAudioCore)
    target_include_directories(ResamplerWarTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    target_compile_definitions(ResamplerWarTest PRIVATE AESTRA_SRC_TEST_ACCESS=1)
    if(SAMPLERATE_FOUND)
        target_link_libraries(ResamplerWarTest PRIVATE ${SAMPLERATE_LIBRARIES})
        target_include_directories(ResamplerWarTest PRIVATE ${SAMPLERATE_INCLUDE_DIRS})
        target_compile_options(ResamplerWarTest PRIVATE ${SAMPLERATE_CFLAGS_OTHER})
        target_compile_definitions(ResamplerWarTest PRIVATE AESTRA_HAS_LIBSAMPLERATE=1)
    endif()
    if(SOXR_FOUND)
        target_link_libraries(ResamplerWarTest PRIVATE ${SOXR_LIBRARIES})
        target_include_directories(ResamplerWarTest PRIVATE ${SOXR_INCLUDE_DIRS})
        target_compile_options(ResamplerWarTest PRIVATE ${SOXR_CFLAGS_OTHER})
        target_compile_definitions(ResamplerWarTest PRIVATE AESTRA_HAS_SOXR=1)
    endif()
    add_test(NAME ResamplerWarTest COMMAND ResamplerWarTest)
    set_tests_properties(ResamplerWarTest PROPERTIES LABELS "audio;dsp;resampler;quality")
endif()

# =============================================================================
# AestraAudio Tests
# =============================================================================

# Audio Test (runtime/device-dependent)
add_executable(AestraAudioTest AestraAudio/AudioTest.cpp)
target_link_libraries(AestraAudioTest PRIVATE AestraAudio)
if(AESTRA_ENABLE_RUNTIME_TESTS)
    add_test(NAME AestraAudioTest COMMAND AestraAudioTest)
    set_tests_properties(AestraAudioTest PROPERTIES LABELS "audio;runtime;device")
else()
    message(STATUS "AestraAudioTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
endif()

# Soak Test (runtime/device-dependent)
add_executable(AestraAudioSoakTest AestraAudio/AudioEngineSoakTest.cpp)
target_link_libraries(AestraAudioSoakTest PRIVATE AestraAudio)
if(AESTRA_ENABLE_RUNTIME_TESTS)
    add_test(NAME AestraAudioSoakTest COMMAND AestraAudioSoakTest)
    set_tests_properties(AestraAudioSoakTest PROPERTIES LABELS "audio;runtime;soak;long")
else()
    message(STATUS "AestraAudioSoakTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
endif()

# K-005: Driver-level Soak Test (runtime/device-dependent, exercises real audio path)
add_executable(AestraAudioDriverSoakTest AestraAudio/AudioDriverSoakTest.cpp)
target_link_libraries(AestraAudioDriverSoakTest PRIVATE AestraAudio AestraAudioCore)
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
target_link_libraries(AestraAudioPerformanceTest PRIVATE AestraAudio)
add_test(NAME AestraAudioPerformanceTest COMMAND AestraAudioPerformanceTest)
if(WIN32)
    target_link_libraries(AestraAudioPerformanceTest PRIVATE avrt)
endif()

add_executable(AestraRealtimePathStressTest AestraAudio/RealtimePathStressTest.cpp)
target_link_libraries(AestraRealtimePathStressTest PRIVATE AestraAudio)
target_include_directories(AestraRealtimePathStressTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Playback
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
if(AESTRA_ENABLE_RUNTIME_TESTS)
    add_test(NAME AestraRealtimePathStressTest COMMAND AestraRealtimePathStressTest)
    set_tests_properties(AestraRealtimePathStressTest PROPERTIES LABELS "audio;realtime;stress")
else()
    message(STATUS "AestraRealtimePathStressTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/SamplerStereoParityTest.cpp")
    add_executable(SamplerStereoParityTest AestraAudio/SamplerStereoParityTest.cpp)
    target_link_libraries(SamplerStereoParityTest PRIVATE AestraAudio)
    target_include_directories(SamplerStereoParityTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME SamplerStereoParityTest COMMAND SamplerStereoParityTest)
    set_tests_properties(SamplerStereoParityTest PROPERTIES LABELS "audio;sampler;regression")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/PatternPlaybackTempoChangeTest.cpp")
    add_executable(PatternPlaybackTempoChangeTest AestraAudio/PatternPlaybackTempoChangeTest.cpp)
    target_link_libraries(PatternPlaybackTempoChangeTest PRIVATE AestraAudio)
    target_include_directories(PatternPlaybackTempoChangeTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Playback
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME PatternPlaybackTempoChangeTest COMMAND PatternPlaybackTempoChangeTest)
    set_tests_properties(PatternPlaybackTempoChangeTest PROPERTIES LABELS "audio;transport;pattern;regression")
endif()

# Sinc Interpolator Benchmark
add_executable(AestraSincBenchmark AestraAudio/SincBenchmark.cpp)
target_link_libraries(AestraSincBenchmark PRIVATE AestraAudioCore)

# Reverb SIMD Benchmark
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbBenchmark.cpp")
    add_executable(AestraReverbBenchmark AestraAudio/ReverbBenchmark.cpp)
    target_link_libraries(AestraReverbBenchmark PRIVATE AestraAudio)
    target_include_directories(AestraReverbBenchmark PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbSIMDParityTest.cpp")
    add_executable(ReverbSIMDParityTest AestraAudio/ReverbSIMDParityTest.cpp)
    target_link_libraries(ReverbSIMDParityTest PRIVATE AestraAudio)
    target_include_directories(ReverbSIMDParityTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    # The test TU gates its real body on AESTRA_ENABLE_RUNTIME_TESTS, but this
    # check is pure computation (SSE vs scalar FDN kernels — no devices, no
    # hardware I/O), and the test is registered on the always-on tier. Without
    # this definition the body compiles out and ctest reports a "pass" that is
    # actually a silent skip on every SSE machine.
    target_compile_definitions(ReverbSIMDParityTest PRIVATE AESTRA_ENABLE_RUNTIME_TESTS=1)
    add_test(NAME ReverbSIMDParityTest COMMAND ReverbSIMDParityTest)
    set_tests_properties(ReverbSIMDParityTest PROPERTIES LABELS "audio;dsp;reverb")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbSafetyRegressionTest.cpp")
    add_executable(ReverbSafetyRegressionTest AestraAudio/ReverbSafetyRegressionTest.cpp)
    target_link_libraries(ReverbSafetyRegressionTest PRIVATE AestraAudio)
    target_include_directories(ReverbSafetyRegressionTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME ReverbSafetyRegressionTest COMMAND ReverbSafetyRegressionTest)
    set_tests_properties(ReverbSafetyRegressionTest PROPERTIES LABELS "audio;dsp;reverb;safety")
endif()

# Reverb Consistency Probe — hunts for inconsistency/offness (bypass parity,
# NaN rejection, silence tail, state roundtrip are hard invariants; Low Cut
# mapping, freeze stability, sample-rate consistency, mode-switch clicks, and
# stereo correlation are printed diagnostics).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbConsistencyProbe.cpp")
    add_executable(ReverbConsistencyProbe AestraAudio/ReverbConsistencyProbe.cpp)
    target_link_libraries(ReverbConsistencyProbe PRIVATE AestraAudio)
    target_include_directories(ReverbConsistencyProbe PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME ReverbConsistencyProbe COMMAND ReverbConsistencyProbe)
    set_tests_properties(ReverbConsistencyProbe PROPERTIES LABELS "audio;dsp;reverb;probe")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbDecayMetricTest.cpp")
    add_executable(ReverbDecayMetricTest AestraAudio/ReverbDecayMetricTest.cpp)
    target_include_directories(ReverbDecayMetricTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio
    )
    add_test(NAME ReverbDecayMetricTest COMMAND ReverbDecayMetricTest)
    set_tests_properties(ReverbDecayMetricTest PROPERTIES LABELS "audio;dsp;reverb;measurement")
endif()

# Reverb Quality Measurement Lab
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbQualityLab.cpp")
    add_executable(AestraReverbQualityLab AestraAudio/ReverbQualityLab.cpp)
    target_link_libraries(AestraReverbQualityLab PRIVATE AestraAudio)
    target_include_directories(AestraReverbQualityLab PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

# Reverb Material Validation Lab
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ReverbMaterialLab.cpp")
    add_executable(AestraReverbMaterialLab AestraAudio/ReverbMaterialLab.cpp)
    target_link_libraries(AestraReverbMaterialLab PRIVATE AestraAudio)
    target_include_directories(AestraReverbMaterialLab PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    # Regression thresholds are calibrated for Linux/GCC floating-point behavior.
    # Register test only on Linux to avoid false failures from cross-platform FP drift.
    if(AESTRA_ENABLE_RUNTIME_TESTS AND UNIX AND NOT APPLE)
        add_test(NAME ReverbMaterialLabTest COMMAND AestraReverbMaterialLab)
    elseif(NOT AESTRA_ENABLE_RUNTIME_TESTS)
        message(STATUS "ReverbMaterialLabTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
    endif()
endif()

# MiMoClaw listening pack generators
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ListeningPack002.cpp")
    add_executable(AestraListeningPack002 AestraAudio/ListeningPack002.cpp)
    target_link_libraries(AestraListeningPack002 PRIVATE AestraAudio)
    target_include_directories(AestraListeningPack002 PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ListeningPack003.cpp")
    add_executable(AestraListeningPack003 AestraAudio/ListeningPack003.cpp)
    target_link_libraries(AestraListeningPack003 PRIVATE AestraAudio)
    target_include_directories(AestraListeningPack003 PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ListeningPack004.cpp")
    add_executable(AestraListeningPack004 AestraAudio/ListeningPack004.cpp)
    target_link_libraries(AestraListeningPack004 PRIVATE AestraAudio)
    target_include_directories(AestraListeningPack004 PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

# Stress benchmark companion used by dsp-benchmark workflow
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/AestraStressTest.cpp")
    add_executable(AestraStressTest Integration/AestraStressTest.cpp)
    target_link_libraries(AestraStressTest PRIVATE AestraAudioCore)
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
target_link_libraries(AestraWaveformLockTest PRIVATE AestraAudio)
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
target_link_libraries(AestraWaveformCacheTest PRIVATE AestraAudio)
if(AESTRA_ENABLE_EXPERIMENTAL_TESTS)
    add_test(NAME AestraWaveformCacheTest COMMAND AestraWaveformCacheTest)
    set_tests_properties(AestraWaveformCacheTest PROPERTIES LABELS "experimental;unstable")
else()
    message(STATUS "AestraWaveformCacheTest built but not registered (set AESTRA_ENABLE_EXPERIMENTAL_TESTS=ON to enable)")
endif()

# Atomic Save Test (self-contained filesystem atomic-write smoke test)
add_executable(AestraAtomicSaveTest AestraAudio/AtomicSaveTest.cpp)
target_link_libraries(AestraAtomicSaveTest PRIVATE)
add_test(NAME AestraAtomicSaveTest COMMAND AestraAtomicSaveTest)

# Deferred destruction / resource reclamation test
add_executable(AestraGarbageCollectorTest AestraAudio/GarbageCollectorTest.cpp)
target_link_libraries(AestraGarbageCollectorTest PRIVATE AestraAudio)
target_include_directories(AestraGarbageCollectorTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraGarbageCollectorTest COMMAND AestraGarbageCollectorTest)
set_tests_properties(AestraGarbageCollectorTest PROPERTIES LABELS "audio;memory;reclamation")

# Connection/ScopedCallback regression tests
add_executable(AestraConnectionTest AestraAudio/ConnectionTest.cpp)
target_link_libraries(AestraConnectionTest PRIVATE)
target_include_directories(AestraConnectionTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraCore/include
    ${CMAKE_SOURCE_DIR}
)
add_test(NAME AestraConnectionTest COMMAND AestraConnectionTest)
set_tests_properties(AestraConnectionTest PROPERTIES LABELS "audio;regression")

# EffectChain Snapshot Test (Pass 1: immutable snapshot type)
add_executable(AestraEffectChainSnapshotTest AestraAudio/EffectChainSnapshotTest.cpp)
target_link_libraries(AestraEffectChainSnapshotTest PRIVATE AestraAudio)
target_include_directories(AestraEffectChainSnapshotTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraEffectChainSnapshotTest COMMAND AestraEffectChainSnapshotTest)
set_tests_properties(AestraEffectChainSnapshotTest PROPERTIES LABELS "audio;plugin")

# AudioEngine EffectChain prepare regression test
add_executable(AestraAudioEngineEffectChainPrepareTest AestraAudio/AudioEngineEffectChainPrepareTest.cpp)
target_link_libraries(AestraAudioEngineEffectChainPrepareTest PRIVATE AestraAudio)
target_include_directories(AestraAudioEngineEffectChainPrepareTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraAudioEngineEffectChainPrepareTest COMMAND AestraAudioEngineEffectChainPrepareTest)
set_tests_properties(AestraAudioEngineEffectChainPrepareTest PROPERTIES LABELS "audio;plugin;regression")

# Plugin insert/remove graph invalidation regression tests
add_executable(AestraPluginGraphInvalidationTest AestraAudio/PluginGraphInvalidationTest.cpp)
target_link_libraries(AestraPluginGraphInvalidationTest PRIVATE AestraAudio)
target_include_directories(AestraPluginGraphInvalidationTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME PluginInsertTakesEffectWithoutClipMoveTest COMMAND AestraPluginGraphInvalidationTest)
add_test(NAME PluginRemoveTakesEffectWithoutClipMoveTest COMMAND AestraPluginGraphInvalidationTest)
set_tests_properties(PluginInsertTakesEffectWithoutClipMoveTest PluginRemoveTakesEffectWithoutClipMoveTest
    PROPERTIES LABELS "audio;plugin;regression")

# MixerChannel scratch buffer regression tests
add_executable(AestraMixerChannelScratchBufferTest AestraAudio/MixerChannelScratchBufferTest.cpp)
target_link_libraries(AestraMixerChannelScratchBufferTest PRIVATE AestraAudio)
target_include_directories(AestraMixerChannelScratchBufferTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraMixerChannelScratchBufferTest COMMAND AestraMixerChannelScratchBufferTest)
set_tests_properties(AestraMixerChannelScratchBufferTest PROPERTIES LABELS "audio;plugin;regression")

# AudioEngineDiagnostics Test
add_executable(AestraAudioEngineDiagnosticsTest AestraAudio/AudioEngineDiagnosticsTest.cpp)
target_link_libraries(AestraAudioEngineDiagnosticsTest PRIVATE AestraAudio)
target_include_directories(AestraAudioEngineDiagnosticsTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
)
add_test(NAME AestraAudioEngineDiagnosticsTest COMMAND AestraAudioEngineDiagnosticsTest)
set_tests_properties(AestraAudioEngineDiagnosticsTest PROPERTIES LABELS "audio;diagnostics;rt-safety")

# PathologicalChaosStressTest
if(EXISTS "${CMAKE_SOURCE_DIR}/AestraAudio/PathologicalChaosStressTest.cpp")
    add_executable(AestraPathologicalChaosStressTest AestraAudio/PathologicalChaosStressTest.cpp)
    target_link_libraries(AestraPathologicalChaosStressTest PRIVATE AestraAudio)
    target_include_directories(AestraPathologicalChaosStressTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
    )
    add_test(NAME AestraPathologicalChaosStressTest COMMAND AestraPathologicalChaosStressTest)
    set_tests_properties(AestraPathologicalChaosStressTest PROPERTIES LABELS "audio;stress;rt-safety")
else()
    message(STATUS "AestraPathologicalChaosStressTest skipped (source file not found)")
endif()

# AudioQualityRegressionTest
add_executable(AestraAudioQualityRegressionTest AestraAudio/AudioQualityRegressionTest.cpp)
target_link_libraries(AestraAudioQualityRegressionTest PRIVATE AestraAudioCore)
target_include_directories(AestraAudioQualityRegressionTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraAudioQualityRegressionTest COMMAND AestraAudioQualityRegressionTest)
set_tests_properties(AestraAudioQualityRegressionTest PROPERTIES LABELS "audio;quality;regression")

# Playback path signal-integrity diagnostics
add_executable(AestraPlaybackPathSignalIntegrityTest AestraAudio/PlaybackPathSignalIntegrityTest.cpp)
target_link_libraries(AestraPlaybackPathSignalIntegrityTest PRIVATE AestraAudio)
add_test(NAME AestraPlaybackPathSignalIntegrityTest COMMAND AestraPlaybackPathSignalIntegrityTest)
set_tests_properties(AestraPlaybackPathSignalIntegrityTest PROPERTIES LABELS "audio;quality;regression;playback")

add_executable(TrackManagerMasterDuckingAuditTest AestraAudio/TrackManagerMasterDuckingAuditTest.cpp)
target_link_libraries(TrackManagerMasterDuckingAuditTest PRIVATE AestraAudio)
add_test(NAME TrackManagerMasterDuckingAuditTest COMMAND TrackManagerMasterDuckingAuditTest)
set_tests_properties(TrackManagerMasterDuckingAuditTest PROPERTIES LABELS "audio;quality;regression;playback;metering")

# AudioEngine output channel safety regression test
add_executable(AestraAudioEngineOutputChannelTest AestraAudio/AudioEngineOutputChannelTest.cpp)
target_link_libraries(AestraAudioEngineOutputChannelTest PRIVATE AestraAudioCore)
target_include_directories(AestraAudioEngineOutputChannelTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraAudioEngineOutputChannelTest COMMAND AestraAudioEngineOutputChannelTest)
set_tests_properties(AestraAudioEngineOutputChannelTest PROPERTIES LABELS "audio;rt-safety;regression")

# Watchdog Test (requires VST3 SDK headers)
if(EXISTS "${CMAKE_SOURCE_DIR}/AestraAudio/External/vst3sdk/pluginterfaces/vst/ivstaudioprocessor.h")
    add_executable(AestraWatchdogTest AestraAudio/WatchdogTest.cpp)
    target_link_libraries(AestraWatchdogTest PRIVATE AestraAudio)
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
    target_link_libraries(AestraCrashTest PRIVATE AestraAudio)
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
target_link_libraries(AestraWavLoaderTest PRIVATE AestraAudio)
add_test(NAME AestraWavLoaderTest COMMAND AestraWavLoaderTest)

# Audition Engine lifecycle regression
add_executable(AestraAuditionEngineLifecycleTest AestraAudio/AuditionEngineLifecycleTest.cpp)
target_link_libraries(AestraAuditionEngineLifecycleTest PRIVATE AestraAudio)
add_test(NAME AestraAuditionEngineLifecycleTest COMMAND AestraAuditionEngineLifecycleTest)

add_executable(PreviewAuditionGainParityTest AestraAudio/PreviewAuditionGainParityTest.cpp)
target_link_libraries(PreviewAuditionGainParityTest PRIVATE AestraAudio)
add_test(NAME PreviewAuditionGainParityTest COMMAND PreviewAuditionGainParityTest)
set_tests_properties(PreviewAuditionGainParityTest PROPERTIES LABELS "audio;preview;audition;gain;regression")

# SamplePool Test
add_executable(SamplePoolTest AestraAudio/SamplePoolTest.cpp)
target_link_libraries(SamplePoolTest PRIVATE AestraAudio)
add_test(NAME SamplePoolTest COMMAND SamplePoolTest)

# Device Manager Test (Windows only)
if(WIN32 AND AESTRA_ENABLE_RUNTIME_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/DeviceManagerTest.cpp")
    add_executable(AestraDeviceManagerTest AestraAudio/DeviceManagerTest.cpp)
    target_link_libraries(AestraDeviceManagerTest PRIVATE AestraAudio)
    target_link_libraries(AestraDeviceManagerTest PRIVATE avrt)
    add_test(NAME RuntimeAestraDeviceManagerTest COMMAND AestraDeviceManagerTest)
    set_tests_properties(RuntimeAestraDeviceManagerTest PROPERTIES LABELS "Runtime;audio;device")
elseif(WIN32)
    message(STATUS "AestraDeviceManagerTest skipped in deterministic test mode (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
endif()

# Audio Callback Test
add_executable(AestraAudioCallbackTest AestraAudio/AudioCallbackTest.cpp)
target_link_libraries(AestraAudioCallbackTest PRIVATE AestraAudio)
if(AESTRA_ENABLE_EXPERIMENTAL_TESTS)
    add_test(NAME AestraAudioCallbackTest COMMAND AestraAudioCallbackTest)
    set_tests_properties(AestraAudioCallbackTest PROPERTIES LABELS "experimental;unstable")
else()
    message(STATUS "AestraAudioCallbackTest built but not registered (set AESTRA_ENABLE_EXPERIMENTAL_TESTS=ON to enable)")
endif()

# Filter Test
add_executable(AestraFilterTest AestraAudio/FilterTest.cpp)
target_link_libraries(AestraFilterTest PRIVATE AestraAudio)
if(AESTRA_ENABLE_EXPERIMENTAL_TESTS)
    add_test(NAME AestraFilterTest COMMAND AestraFilterTest)
    set_tests_properties(AestraFilterTest PROPERTIES LABELS "experimental;unstable")
else()
    message(STATUS "AestraFilterTest built but not registered (set AESTRA_ENABLE_EXPERIMENTAL_TESTS=ON to enable)")
endif()

# Oscillator Test
add_executable(AestraOscillatorTest AestraAudio/OscillatorTest.cpp)
target_link_libraries(AestraOscillatorTest PRIVATE AestraAudio)
target_compile_definitions(AestraOscillatorTest PRIVATE AESTRA_HEADLESS)
add_test(NAME AestraOscillatorTest COMMAND AestraOscillatorTest)

# Automation Stress Test
add_executable(AutomationStressTest AestraAudio/AutomationStressTest.cpp)
target_link_libraries(AutomationStressTest PRIVATE AestraAudio)
target_compile_definitions(AutomationStressTest PRIVATE AESTRA_HEADLESS)
add_test(NAME AutomationStressTest COMMAND AutomationStressTest)

# Arsenal Parameter Stress Test (requires runtime plugin system)
if(TARGET AestraAudioCore AND TARGET AestraPremiumPlugins)
    add_executable(ArsenalParameterStressTest AestraAudio/ArsenalParameterStressTest.cpp)
    target_link_libraries(ArsenalParameterStressTest PRIVATE AestraPremiumPlugins AestraAudioCore)
    target_compile_definitions(ArsenalParameterStressTest PRIVATE AESTRA_HEADLESS)
    if(AESTRA_ENABLE_RUNTIME_TESTS)
        add_test(NAME ArsenalParameterStressTest COMMAND ArsenalParameterStressTest)
    else()
        message(STATUS "ArsenalParameterStressTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
    endif()
endif()

# Mixer Bus Stress Test
add_executable(MixerBusStressTest AestraAudio/MixerBusStressTest.cpp)
target_link_libraries(MixerBusStressTest PRIVATE AestraAudio)
target_compile_definitions(MixerBusStressTest PRIVATE AESTRA_HEADLESS)
add_test(NAME MixerBusStressTest COMMAND MixerBusStressTest)

# Mixer Bus Test
add_executable(AestraMixerBusTest AestraAudio/MixerBusTest.cpp)
target_link_libraries(AestraMixerBusTest PRIVATE AestraAudio)
target_compile_definitions(AestraMixerBusTest PRIVATE AESTRA_HEADLESS)
add_test(NAME AestraMixerBusTest COMMAND AestraMixerBusTest)

# AestraUUID Test (header-only, no audio hardware required)
add_executable(AestraUUIDTest AestraAudio/AestraUUIDTest.cpp)
target_link_libraries(AestraUUIDTest PRIVATE AestraAudioCore)
target_include_directories(AestraUUIDTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraUUIDTest COMMAND AestraUUIDTest)
set_tests_properties(AestraUUIDTest PROPERTIES LABELS "audio;uuid")

# Arsenal route-mode compatibility test
add_executable(ArsenalRouteModeCompatibilityTest AestraAudio/ArsenalRouteModeCompatibilityTest.cpp)
target_link_libraries(ArsenalRouteModeCompatibilityTest PRIVATE AestraAudioCore)
target_include_directories(ArsenalRouteModeCompatibilityTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ArsenalRouteModeCompatibilityTest COMMAND ArsenalRouteModeCompatibilityTest)
set_tests_properties(ArsenalRouteModeCompatibilityTest PROPERTIES LABELS "audio;arsenal")

# Arsenal processing context routing test
add_executable(ArsenalProcessingContextRoutingTest AestraAudio/ArsenalProcessingContextRoutingTest.cpp)
target_link_libraries(ArsenalProcessingContextRoutingTest PRIVATE AestraAudioCore)
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
target_link_libraries(ArsenalRouteModeRoundTripTest PRIVATE AestraAudio)
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
target_link_libraries(ArsenalRouteModePolicyTest PRIVATE AestraAudioCore)
target_include_directories(ArsenalRouteModePolicyTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ArsenalRouteModePolicyTest COMMAND ArsenalRouteModePolicyTest)
set_tests_properties(ArsenalRouteModePolicyTest PROPERTIES LABELS "audio;arsenal;policy")

if(NOT WIN32)
    add_executable(PluginScannerSigpipeTest AestraAudio/PluginScannerSigpipeTest.cpp)
    target_link_libraries(PluginScannerSigpipeTest PRIVATE AestraAudioCore)
    target_include_directories(PluginScannerSigpipeTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    if(AESTRA_HAS_CLAP)
        target_compile_definitions(PluginScannerSigpipeTest PRIVATE AESTRA_TEST_HAS_CLAP=1)
    endif()
    add_test(NAME PluginScannerSigpipeTest COMMAND PluginScannerSigpipeTest)
    set_tests_properties(PluginScannerSigpipeTest PROPERTIES LABELS "audio;plugin;security")
endif()

# Arsenal bridge-mode terminology contract test (Phase 2B policy-only)
add_executable(ArsenalBridgeContractTest AestraAudio/ArsenalBridgeContractTest.cpp)
target_link_libraries(ArsenalBridgeContractTest PRIVATE)
target_include_directories(ArsenalBridgeContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ArsenalBridgeContractTest COMMAND ArsenalBridgeContractTest)
set_tests_properties(ArsenalBridgeContractTest PROPERTIES LABELS "arsenal;policy")

# Arsenal export current-policy parity guard (Phase 2C)
add_executable(ArsenalExportCurrentPolicyTest AestraAudio/ArsenalExportCurrentPolicyTest.cpp)
target_link_libraries(ArsenalExportCurrentPolicyTest PRIVATE AestraAudioCore)
target_include_directories(ArsenalExportCurrentPolicyTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ArsenalExportCurrentPolicyTest COMMAND ArsenalExportCurrentPolicyTest)
set_tests_properties(ArsenalExportCurrentPolicyTest PROPERTIES LABELS "arsenal;policy;offline")

# Arsenal live/export parity hardening test (Phase 3)
add_executable(ArsenalExportLiveParityTest AestraAudio/ArsenalExportLiveParityTest.cpp)
target_link_libraries(ArsenalExportLiveParityTest PRIVATE AestraAudio)
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
target_link_libraries(ArsenalBridgeModeRoundTripTest PRIVATE AestraAudioCore)
target_include_directories(ArsenalBridgeModeRoundTripTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ArsenalBridgeModeRoundTripTest COMMAND ArsenalBridgeModeRoundTripTest)
set_tests_properties(ArsenalBridgeModeRoundTripTest PROPERTIES LABELS "arsenal;policy;roundtrip")

# Sample Rate Converter Test
add_executable(AestraSampleRateConverterTest AestraAudio/SampleRateConverterTest.cpp)
target_link_libraries(AestraSampleRateConverterTest PRIVATE AestraAudio)
target_compile_definitions(AestraSampleRateConverterTest PRIVATE AESTRA_SRC_TEST_ACCESS=1)
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
target_link_libraries(RecordingPathTest PRIVATE AestraAudio)
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
target_link_libraries(AestraEQTest PRIVATE AestraAudio)
target_include_directories(AestraEQTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraEQTest COMMAND AestraEQTest)
set_tests_properties(AestraEQTest PROPERTIES LABELS "audio;plugins;eq")

# Aestra EQ Measurement Test
add_executable(AestraEQMeasurementTest AestraAudio/AestraEQMeasurementTest.cpp)
target_link_libraries(AestraEQMeasurementTest PRIVATE AestraAudio)
target_include_directories(AestraEQMeasurementTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraEQMeasurementTest COMMAND AestraEQMeasurementTest)
set_tests_properties(AestraEQMeasurementTest PROPERTIES LABELS "audio;plugins;eq;measurement")

# Aestra EQ Material Lab
add_executable(AestraEQMaterialLab AestraAudio/AestraEQMaterialLab.cpp)
target_link_libraries(AestraEQMaterialLab PRIVATE AestraAudio)
target_include_directories(AestraEQMaterialLab PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
target_compile_definitions(AestraEQMaterialLab
    PRIVATE AESTRA_LAB_OUTPUT_DIR="${CMAKE_BINARY_DIR}/labs/eq/quality"
)

# Aestra Comp Phase 0 Test
add_executable(AestraCompPhase0Test AestraAudio/AestraCompPhase0Test.cpp)
target_link_libraries(AestraCompPhase0Test PRIVATE AestraAudio)
target_include_directories(AestraCompPhase0Test PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraCompPhase0Test COMMAND AestraCompPhase0Test)
set_tests_properties(AestraCompPhase0Test PROPERTIES LABELS "audio;plugins;comp;phase0")

# Aestra Comp Phase 1 Test
add_executable(AestraCompPhase1Test AestraAudio/AestraCompPhase1Test.cpp)
target_link_libraries(AestraCompPhase1Test PRIVATE AestraAudio)
target_include_directories(AestraCompPhase1Test PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraCompPhase1Test COMMAND AestraCompPhase1Test)
set_tests_properties(AestraCompPhase1Test PROPERTIES LABELS "audio;plugins;comp;phase1")

# Aestra Comp Oversampling Test (#228)
add_executable(AestraCompOversamplingTest AestraAudio/AestraCompOversamplingTest.cpp)
target_link_libraries(AestraCompOversamplingTest PRIVATE AestraAudio)
target_include_directories(AestraCompOversamplingTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraCompOversamplingTest COMMAND AestraCompOversamplingTest)
set_tests_properties(AestraCompOversamplingTest PROPERTIES LABELS "audio;plugins;comp;oversampling")

# Aestra Comp Upgrade Test
add_executable(AestraCompUpgradeTest AestraAudio/AestraCompUpgradeTest.cpp)
target_link_libraries(AestraCompUpgradeTest PRIVATE AestraAudio)
target_include_directories(AestraCompUpgradeTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
if(AESTRA_ENABLE_RUNTIME_TESTS)
    add_test(NAME AestraCompUpgradeTest COMMAND AestraCompUpgradeTest)
    set_tests_properties(AestraCompUpgradeTest PROPERTIES LABELS "audio;plugins;comp;upgrade")
else()
    message(STATUS "AestraCompUpgradeTest built but not registered (set AESTRA_ENABLE_RUNTIME_TESTS=ON to enable)")
endif()

# Aestra Compressor Material Lab
add_executable(AestraCompressorMaterialLab AestraAudio/AestraCompressorMaterialLab.cpp)
target_link_libraries(AestraCompressorMaterialLab PRIVATE AestraAudio)
target_include_directories(AestraCompressorMaterialLab PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
target_compile_definitions(AestraCompressorMaterialLab PRIVATE AESTRA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME AestraCompressorMaterialLab COMMAND AestraCompressorMaterialLab)
set_tests_properties(AestraCompressorMaterialLab PROPERTIES LABELS "audio;plugins;comp;lab")

# Aestra Delay Upgrade Test
add_executable(AestraDelayUpgradeTest AestraAudio/AestraDelayUpgradeTest.cpp)
target_link_libraries(AestraDelayUpgradeTest PRIVATE AestraAudio)
target_include_directories(AestraDelayUpgradeTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraDelayUpgradeTest COMMAND AestraDelayUpgradeTest)
set_tests_properties(AestraDelayUpgradeTest PROPERTIES LABELS "audio;plugins;delay;upgrade")

# Aestra Limit Test
add_executable(AestraLimitTest AestraAudio/AestraLimitTest.cpp)
target_link_libraries(AestraLimitTest PRIVATE AestraAudio)
target_include_directories(AestraLimitTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraLimitTest COMMAND AestraLimitTest)
set_tests_properties(AestraLimitTest PROPERTIES LABELS "audio;plugins;limiter")

# Aestra Sat Test
add_executable(AestraSatTest AestraAudio/AestraSatTest.cpp)
target_link_libraries(AestraSatTest PRIVATE AestraAudio)
target_include_directories(AestraSatTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraSatTest COMMAND AestraSatTest)
set_tests_properties(AestraSatTest PROPERTIES LABELS "audio;plugins;saturation")

# Aestra Filter plugin test (AestraFilterTest is the DSP::Filter lab above)
add_executable(AestraFilterPluginTest AestraAudio/AestraFilterPluginTest.cpp)
target_link_libraries(AestraFilterPluginTest PRIVATE AestraAudio)
target_include_directories(AestraFilterPluginTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraFilterPluginTest COMMAND AestraFilterPluginTest)
set_tests_properties(AestraFilterPluginTest PROPERTIES LABELS "audio;plugins;filter")

# Aestra Filter multi-instance benchmark — build always, register only when
# experimental tests are enabled (never in the always-on CI tier).
add_executable(AestraFilterBench AestraAudio/AestraFilterBench.cpp)
target_link_libraries(AestraFilterBench PRIVATE AestraAudio)
target_include_directories(AestraFilterBench PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
if(AESTRA_ENABLE_EXPERIMENTAL_TESTS)
    add_test(NAME AestraFilterBench COMMAND AestraFilterBench)
    set_tests_properties(AestraFilterBench PROPERTIES LABELS "audio;benchmark;experimental")
else()
    message(STATUS "AestraFilterBench built but not registered (set AESTRA_ENABLE_EXPERIMENTAL_TESTS=ON to enable)")
endif()

# Aestra OTT Test
add_executable(AestraOTTTest AestraAudio/AestraOTTTest.cpp)
target_link_libraries(AestraOTTTest PRIVATE AestraAudio)
target_include_directories(AestraOTTTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraOTTTest COMMAND AestraOTTTest)
set_tests_properties(AestraOTTTest PROPERTIES LABELS "audio;plugins;ott")

# Aestra LFO Test
add_executable(AestraLFOTest AestraAudio/AestraLFOTest.cpp)
target_link_libraries(AestraLFOTest PRIVATE AestraAudio)
target_include_directories(AestraLFOTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraLFOTest COMMAND AestraLFOTest)
set_tests_properties(AestraLFOTest PROPERTIES LABELS "audio;plugins;lfo")

# Plugin initialization contract — parameters must survive EffectChain::prepare()
# re-initialize (sample-rate/device changes), across every internal effect.
add_executable(PluginInitContractTest AestraAudio/PluginInitContractTest.cpp)
target_link_libraries(PluginInitContractTest PRIVATE AestraAudio)
target_include_directories(PluginInitContractTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME PluginInitContractTest COMMAND PluginInitContractTest)
set_tests_properties(PluginInitContractTest PROPERTIES LABELS "audio;plugins;lifecycle")

# =============================================================================
# Audio Quality Tests (S-Tier validation suite, Spec §3, §8, §15)
# =============================================================================

# Summing Engine Test — Verifies N-track digital summing accuracy
# against mathematical reference. Pass: RMS error < -140 dB.
add_executable(SummingEngineTest AestraAudio/SummingEngineTest.cpp)
target_link_libraries(SummingEngineTest PRIVATE AestraAudio)
target_include_directories(SummingEngineTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME SummingEngineTest COMMAND SummingEngineTest)
set_tests_properties(SummingEngineTest PROPERTIES LABELS "audio;quality;summing;s-tier")

# Golden Reference Test — Signal-level regression against in-memory
# mathematical reference for 1kHz, 440Hz, 10kHz, and impulse signals.
add_executable(GoldenReferenceTest AestraAudio/GoldenReferenceTest.cpp)
target_link_libraries(GoldenReferenceTest PRIVATE AestraAudio)
target_include_directories(GoldenReferenceTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME GoldenReferenceTest COMMAND GoldenReferenceTest)
set_tests_properties(GoldenReferenceTest PROPERTIES LABELS "audio;quality;golden;s-tier")

# Golden Audio Regression — analytic golden cases for the render path:
# silence/empty project, impulse via master, multi-track gain+pan mix.
# Harness: Tests/AestraAudio/GoldenAudio/GoldenAudioHarness.h.
# Policy doc: AestraDocs/audio-integrity-infrastructure.md.
add_executable(GoldenAudioRegressionTest AestraAudio/GoldenAudioRegressionTest.cpp)
target_link_libraries(GoldenAudioRegressionTest PRIVATE AestraAudio)
target_include_directories(GoldenAudioRegressionTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME GoldenAudioRegressionTest COMMAND GoldenAudioRegressionTest)
set_tests_properties(GoldenAudioRegressionTest PROPERTIES LABELS "audio;quality;golden;integrity;s-tier" TIMEOUT 180)

# Realtime/Export Parity — realtime processBlock output vs offline
# bounceRangeToWav output for the same session, plus the preview-ducking
# export-contamination probe. Doc: AestraDocs/audio-integrity-infrastructure.md.
add_executable(RealtimeExportParityTest AestraAudio/RealtimeExportParityTest.cpp)
target_link_libraries(RealtimeExportParityTest PRIVATE AestraAudio)
target_include_directories(RealtimeExportParityTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME RealtimeExportParityTest COMMAND RealtimeExportParityTest)
set_tests_properties(RealtimeExportParityTest PROPERTIES LABELS "audio;quality;export;parity;integrity;s-tier" TIMEOUT 180)

# RT Allocation Trap — overrides operator new/delete in the test binary and
# renders a session; asserts ZERO steady-state heap activity on the audio
# callback path (thread marked by ScopedRealtimeAudioThread in processBlock).
# Findings doc: AestraDocs/rt-safety-audit.md.
add_executable(RTAllocationTrapTest AestraAudio/RTAllocationTrapTest.cpp)
target_link_libraries(RTAllocationTrapTest PRIVATE AestraAudio)
target_include_directories(RTAllocationTrapTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME RTAllocationTrapTest COMMAND RTAllocationTrapTest)
set_tests_properties(RTAllocationTrapTest PROPERTIES LABELS "audio;rt-safety;integrity;s-tier" TIMEOUT 180)

# Callback Deadline Telemetry — validates AudioTelemetry::recordCallbackDuration
# (avg/worst/budget/over-budget math) synthetically, plus a live smoke over
# real processBlock timings.
add_executable(CallbackDeadlineTelemetryTest AestraAudio/CallbackDeadlineTelemetryTest.cpp)
target_link_libraries(CallbackDeadlineTelemetryTest PRIVATE AestraAudio)
target_include_directories(CallbackDeadlineTelemetryTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME CallbackDeadlineTelemetryTest COMMAND CallbackDeadlineTelemetryTest)
set_tests_properties(CallbackDeadlineTelemetryTest PROPERTIES LABELS "audio;diagnostics;deadline;integrity" TIMEOUT 120)

# Sample-Rate & Buffer Truth — 96 kHz impulse exactness, 48k-clip-in-96k-engine
# SRC pitch/time truth, buffer-size independence sweep (64/193/512/1024/2048),
# sentinel overwrite guarantee, exact post-clip silence.
add_executable(SampleRateBufferTruthTest AestraAudio/SampleRateBufferTruthTest.cpp)
target_link_libraries(SampleRateBufferTruthTest PRIVATE AestraAudio)
target_include_directories(SampleRateBufferTruthTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME SampleRateBufferTruthTest COMMAND SampleRateBufferTruthTest)
set_tests_properties(SampleRateBufferTruthTest PROPERTIES LABELS "audio;quality;samplerate;buffers;integrity;s-tier" TIMEOUT 180)

# Audio Quality Audit — noise floor, THD+N, freq response, DC blocker, soft clipper
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/AudioQualityAuditTest.cpp")
    add_executable(AudioQualityAuditTest AestraAudio/AudioQualityAuditTest.cpp)
    target_link_libraries(AudioQualityAuditTest PRIVATE AestraAudio)
    target_include_directories(AudioQualityAuditTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME AudioQualityAuditTest COMMAND AudioQualityAuditTest)
    set_tests_properties(AudioQualityAuditTest PROPERTIES LABELS "audio;quality;audit;s-tier" TIMEOUT 180)
endif()

# Audio Purity Audit — transparent path, summing, buffer invariance, hidden clipping, SRC invariants
add_executable(AudioPurityAuditTest AestraAudio/AudioPurityAuditTest.cpp)
target_link_libraries(AudioPurityAuditTest PRIVATE AestraAudio)
target_include_directories(AudioPurityAuditTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/DSP
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AudioPurityAuditTest COMMAND AudioPurityAuditTest
    --report ${CMAKE_BINARY_DIR}/audio-purity-report.json
    --text-report ${CMAKE_BINARY_DIR}/audio-purity-report.txt)
set_tests_properties(AudioPurityAuditTest PROPERTIES LABELS "audio;quality;audit;purity;s-tier" TIMEOUT 240)

# AudioEngine Torture Test — stress harness: silence, NaN/Inf, extreme gain,
# buffer switching, sample rate switching, transport hammering, multi-track load
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/AudioEngineTortureTest.cpp")
    add_executable(AudioEngineTortureTest AestraAudio/AudioEngineTortureTest.cpp)
    target_link_libraries(AudioEngineTortureTest PRIVATE AestraAudio)
    target_include_directories(AudioEngineTortureTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    # In Debug configs the engine library keeps assert() live and
    # AudioEngine::processBlock hard-asserts on NaN/Inf, so the poisoned-input
    # cases must be skipped there. Tests compile with -UNDEBUG unconditionally
    # (see top of this file), so the test cannot detect the engine's build
    # config itself — pass it in explicitly.
    target_compile_definitions(AudioEngineTortureTest PRIVATE
        $<$<CONFIG:Debug>:AESTRA_ENGINE_ASSERTS_ACTIVE=1>)
    add_test(NAME AudioEngineTortureTest COMMAND AudioEngineTortureTest)
    set_tests_properties(AudioEngineTortureTest PROPERTIES LABELS "audio;stress;torture;s-tier" TIMEOUT 300)
endif()

# Export/Bounce Parity Test — Master bounce must equal sum of isolated
# track bounces to within -80 dB RMS (accounting for master stage processing).
add_executable(ExportBounceParityTest AestraAudio/ExportBounceParityTest.cpp)
target_link_libraries(ExportBounceParityTest PRIVATE AestraAudio)
target_include_directories(ExportBounceParityTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/IO
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ExportBounceParityTest COMMAND ExportBounceParityTest)
set_tests_properties(ExportBounceParityTest PROPERTIES LABELS "audio;quality;export;s-tier")

# =============================================================================
# Audio Research Bench (Tests/Research)
# =============================================================================
# Scientific audio-quality measurement layer: deterministic signal generators
# (SignalLab.h) + analysis routines (AudioMeasure.h), validated against
# closed-form analytic expectations. Header-only, test-side, no engine deps.
# Doc: AestraDocs/audio-research-bench.md.

# Measurement Core Self-Test — proves the measurement layer itself (peak, RMS,
# DC, tone fit/SINAD-style residual, THD, impulse stats, stereo correlation,
# diff forensics) against analytic math before it is used to measure DSP.
add_executable(MeasurementCoreSelfTest Research/MeasurementCoreSelfTest.cpp)
add_test(NAME MeasurementCoreSelfTest COMMAND MeasurementCoreSelfTest)
set_tests_properties(MeasurementCoreSelfTest PROPERTIES LABELS "audio;research;audio-research" TIMEOUT 120)

# Resampler Quality Audit — measures BOTH resampling engines: the production
# clip-playback path (Interpolators + phase accumulator, as run by
# AudioRenderer/AuditionEngine/SamplerPlugin) and the streaming polyphase
# SampleRateConverter (currently no production call sites), across a 5-pair
# rate matrix (44.1<->48, 48<->96, 44.1->96). Gates cite measured evidence;
# libsamplerate/soxr print optional non-gated diagnostics when present
# (same optional pattern as ResamplerWarTest above).
add_executable(ResamplerQualityAuditTest Research/ResamplerQualityAuditTest.cpp)
target_link_libraries(ResamplerQualityAuditTest PRIVATE AestraAudioCore)
target_include_directories(ResamplerQualityAuditTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
if(SAMPLERATE_FOUND)
    target_link_libraries(ResamplerQualityAuditTest PRIVATE ${SAMPLERATE_LIBRARIES})
    target_include_directories(ResamplerQualityAuditTest PRIVATE ${SAMPLERATE_INCLUDE_DIRS})
    target_compile_options(ResamplerQualityAuditTest PRIVATE ${SAMPLERATE_CFLAGS_OTHER})
    target_compile_definitions(ResamplerQualityAuditTest PRIVATE AESTRA_HAS_LIBSAMPLERATE=1)
endif()
if(SOXR_FOUND)
    target_link_libraries(ResamplerQualityAuditTest PRIVATE ${SOXR_LIBRARIES})
    target_include_directories(ResamplerQualityAuditTest PRIVATE ${SOXR_INCLUDE_DIRS})
    target_compile_options(ResamplerQualityAuditTest PRIVATE ${SOXR_CFLAGS_OTHER})
    target_compile_definitions(ResamplerQualityAuditTest PRIVATE AESTRA_HAS_SOXR=1)
endif()
add_test(NAME ResamplerQualityAuditTest COMMAND ResamplerQualityAuditTest)
set_tests_properties(ResamplerQualityAuditTest PROPERTIES LABELS "audio;research;audio-research;resampler" TIMEOUT 180)

# Anti-Alias Policy Lab (Phase 3) — test-side prototype comparison for finding F1
# (downsampling anti-aliasing): baseline legacy kernel vs an integrated ratio-aware
# cutoff-scaled sinc (Option A) vs a designed delay-compensated Kaiser prefilter
# (Option B), plus optional libsamplerate/soxr references. No production DSP is
# changed; results feed the product decision in AestraDocs/audio-research-bench.md §9.
add_executable(AntiAliasPolicyLab Research/AntiAliasPolicyLab.cpp)
target_link_libraries(AntiAliasPolicyLab PRIVATE AestraAudioCore)
target_include_directories(AntiAliasPolicyLab PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
if(SAMPLERATE_FOUND)
    target_link_libraries(AntiAliasPolicyLab PRIVATE ${SAMPLERATE_LIBRARIES})
    target_include_directories(AntiAliasPolicyLab PRIVATE ${SAMPLERATE_INCLUDE_DIRS})
    target_compile_options(AntiAliasPolicyLab PRIVATE ${SAMPLERATE_CFLAGS_OTHER})
    target_compile_definitions(AntiAliasPolicyLab PRIVATE AESTRA_HAS_LIBSAMPLERATE=1)
endif()
if(SOXR_FOUND)
    target_link_libraries(AntiAliasPolicyLab PRIVATE ${SOXR_LIBRARIES})
    target_include_directories(AntiAliasPolicyLab PRIVATE ${SOXR_INCLUDE_DIRS})
    target_compile_options(AntiAliasPolicyLab PRIVATE ${SOXR_CFLAGS_OTHER})
    target_compile_definitions(AntiAliasPolicyLab PRIVATE AESTRA_HAS_SOXR=1)
endif()
add_test(NAME AntiAliasPolicyLab COMMAND AntiAliasPolicyLab)
set_tests_properties(AntiAliasPolicyLab PROPERTIES LABELS "audio;research;audio-research;resampler" TIMEOUT 300)

# Session Resampling Truth (Phase 2D) — proves the isolated resampler findings
# through the actual shipped paths: realtime playback (TrackManager ->
# AudioGraphBuilder -> AudioEngine::processBlock), offline export
# (bounceRangeToWav -> AudioExporter), PreviewEngine's own Cubic resampler,
# and SamplerPlugin pitch-shift. Needs the full engine, so it links AestraAudio
# (like SampleRateBufferTruthTest / RealtimeExportParityTest), not AestraAudioCore.
add_executable(SessionResamplingTruthTest Research/SessionResamplingTruthTest.cpp)
target_link_libraries(SessionResamplingTruthTest PRIVATE AestraAudio)
target_include_directories(SessionResamplingTruthTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME SessionResamplingTruthTest COMMAND SessionResamplingTruthTest)
set_tests_properties(SessionResamplingTruthTest PROPERTIES LABELS "audio;research;audio-research;resampler" TIMEOUT 300)

# =============================================================================
# AestraPlat Tests
# =============================================================================

# Platform DPI Test
add_executable(PlatformDPITest AestraPlat/PlatformDPITest.cpp)
target_link_libraries(PlatformDPITest PRIVATE AestraPlat)

# Platform Window Test  
add_executable(PlatformWindowTest AestraPlat/PlatformWindowTest.cpp)
target_link_libraries(PlatformWindowTest PRIVATE AestraPlat)

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

# NUITheme JSON loading test (Issue #259)
# Compiles NUITheme.cpp directly (its only deps are header-only: NUITypes.h,
# AestraJSON.h, AestraLog.h) so the test also runs in headless builds where
# AestraUI targets are disabled.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraUI/NUIThemeJSONTest.cpp")
    add_executable(NUIThemeJSONTest
        AestraUI/NUIThemeJSONTest.cpp
        ${CMAKE_SOURCE_DIR}/AestraUI/Core/NUITheme.cpp
    )
    target_include_directories(NUIThemeJSONTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraUI/Core
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME NUIThemeJSONTest COMMAND NUIThemeJSONTest)
    set_tests_properties(NUIThemeJSONTest PROPERTIES LABELS "ui;theme")
    message(STATUS "NUIThemeJSONTest added - Issue #259 theme JSON loading test")
endif()

# KeyboardNoteInput Test — musical-typing translation logic (header-only, no
# UI targets needed, runs headless on every lane).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraUI/KeyboardNoteInputTest.cpp")
    add_executable(KeyboardNoteInputTest AestraUI/KeyboardNoteInputTest.cpp)
    target_include_directories(KeyboardNoteInputTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraUI/Core
        ${CMAKE_SOURCE_DIR}/Source/Core
    )
    add_test(NAME KeyboardNoteInputTest COMMAND KeyboardNoteInputTest)
    set_tests_properties(KeyboardNoteInputTest PROPERTIES LABELS "ui;input;midi")
endif()

# PianoRollInteraction Test - tests for pure helper functions
# Guard on target presence because headless/CI disables AestraUI targets.
if(TARGET AestraUI_Core)
    add_executable(PianoRollInteractionTest AestraUI/PianoRollInteractionTest.cpp)
    target_link_libraries(PianoRollInteractionTest PRIVATE AestraUI_Core)
    target_include_directories(PianoRollInteractionTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraUI
        ${CMAKE_SOURCE_DIR}/AestraUI/Common
    )
    add_test(NAME PianoRollInteractionTest COMMAND PianoRollInteractionTest)
    set_tests_properties(PianoRollInteractionTest PROPERTIES LABELS "ui;interaction")
else()
    message(STATUS "PianoRollInteractionTest skipped (AestraUI_Core target unavailable in this build mode)")
endif()

# Editor-close deferred-destruction regression (#475): removeChild() during
# event dispatch must defer and free safely (no iterator invalidation / UAF).
if(TARGET AestraUI_Core)
    add_executable(EditorCloseDeferredTest AestraUI/EditorCloseDeferredTest.cpp)
    target_link_libraries(EditorCloseDeferredTest PRIVATE AestraUI_Core)
    target_include_directories(EditorCloseDeferredTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraUI
        ${CMAKE_SOURCE_DIR}/AestraUI/Core
    )
    add_test(NAME EditorCloseDeferredTest COMMAND EditorCloseDeferredTest)
    set_tests_properties(EditorCloseDeferredTest PROPERTIES LABELS "ui;lifecycle;regression")
else()
    message(STATUS "EditorCloseDeferredTest skipped (AestraUI_Core target unavailable in this build mode)")
endif()

# Panel-window click-swallow regression: a floating plugin editor must consume
# clicks over its own empty area so they don't fall through to widgets behind it
# (click-through + spurious dismiss).
if(TARGET AestraUI_Core)
    add_executable(PanelWindowClickSwallowTest AestraUI/PanelWindowClickSwallowTest.cpp)
    target_link_libraries(PanelWindowClickSwallowTest PRIVATE AestraUI_Core)
    target_include_directories(PanelWindowClickSwallowTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraUI
        ${CMAKE_SOURCE_DIR}/AestraUI/Core
        ${CMAKE_SOURCE_DIR}/AestraUI/Widgets
    )
    add_test(NAME PanelWindowClickSwallowTest COMMAND PanelWindowClickSwallowTest)
    set_tests_properties(PanelWindowClickSwallowTest PROPERTIES LABELS "ui;input;regression")
else()
    message(STATUS "PanelWindowClickSwallowTest skipped (AestraUI_Core target unavailable in this build mode)")
endif()

# NUITextInput Layout / Caret / Selection regression tests
# Guard on target presence because headless/CI disables AestraUI targets.
if(TARGET AestraUI_Core AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraUI/NUITextInputLayoutTest.cpp")
    add_executable(NUITextInputLayoutTest AestraUI/NUITextInputLayoutTest.cpp)
    target_link_libraries(NUITextInputLayoutTest PRIVATE AestraUI_Core AestraPlat)
    target_include_directories(NUITextInputLayoutTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraUI
        ${CMAKE_SOURCE_DIR}/AestraUI/Base
        ${CMAKE_SOURCE_DIR}/AestraPlat/include
    )
    add_test(NAME NUITextInputLayoutTest COMMAND NUITextInputLayoutTest)
    set_tests_properties(NUITextInputLayoutTest PROPERTIES LABELS "ui;textinput;regression")
    message(STATUS "NUITextInputLayoutTest added - multiline layout/caret regression tests")
else()
    message(STATUS "NUITextInputLayoutTest skipped (AestraUI_Core target unavailable in this build mode)")
endif()

# No EXISTS guard: a missing committed test source must fail configure, not
# silently drop coverage (repo policy since #454/#463).
add_executable(MusicalTypingControllerTest
    AestraUI/MusicalTypingControllerTest.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/MusicalTypingController.cpp
)
target_include_directories(MusicalTypingControllerTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Source/Core
    ${CMAKE_SOURCE_DIR}/AestraUI/Core
)
add_test(NAME MusicalTypingControllerTest COMMAND MusicalTypingControllerTest)
set_tests_properties(MusicalTypingControllerTest PROPERTIES LABELS "ui;midi;regression")

# =============================================================================
# Commands Tests (Phase 2 - Undo/Redo System)
# =============================================================================

# CommandHistory Test
add_executable(CommandHistoryTest Commands/CommandHistoryTest.cpp)
target_link_libraries(CommandHistoryTest PRIVATE AestraAudioCore)
target_include_directories(CommandHistoryTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME CommandHistoryTest COMMAND CommandHistoryTest)
set_tests_properties(CommandHistoryTest PROPERTIES LABELS "commands;phase2")

# MoveClipCommand Test
add_executable(MoveClipCommandTest Commands/MoveClipCommandTest.cpp)
target_link_libraries(MoveClipCommandTest PRIVATE AestraAudioCore)
target_include_directories(MoveClipCommandTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME MoveClipCommandTest COMMAND MoveClipCommandTest)
set_tests_properties(MoveClipCommandTest PROPERTIES LABELS "commands;phase2")

# MacroCommand Test
add_executable(MacroCommandTest Commands/MacroCommandTest.cpp)
target_link_libraries(MacroCommandTest PRIVATE AestraAudioCore)
target_include_directories(MacroCommandTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME MacroCommandTest COMMAND MacroCommandTest)
set_tests_properties(MacroCommandTest PROPERTIES LABELS "commands;phase2")

# Note Commands Test (AddNote, RemoveNote, MoveNote, ResizeNote)
add_executable(NoteCommandsTest Commands/NoteCommandsTest.cpp)
target_link_libraries(NoteCommandsTest PRIVATE AestraAudioCore)
target_include_directories(NoteCommandsTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME NoteCommandsTest COMMAND NoteCommandsTest)
set_tests_properties(NoteCommandsTest PROPERTIES LABELS "commands;phase2")

# NoteDiff Test (headless diffing logic for piano roll save path)
add_executable(NoteDiffTest Commands/NoteDiffTest.cpp)
target_link_libraries(NoteDiffTest PRIVATE AestraAudioCore)
target_include_directories(NoteDiffTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME NoteDiffTest COMMAND NoteDiffTest)
set_tests_properties(NoteDiffTest PROPERTIES LABELS "commands;phase2")

# ScaleContext Test (scale context default values and helpers)
add_executable(ScaleContextTest AestraAudio/ScaleContextTest.cpp)
target_link_libraries(ScaleContextTest PRIVATE AestraAudioCore)
target_include_directories(ScaleContextTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ScaleContextTest COMMAND ScaleContextTest)
set_tests_properties(ScaleContextTest PROPERTIES LABELS "audio;scale;music")

# Mixer Commands Test (Volume, Pan, Mute, Solo)
add_executable(MixerCommandsTest Commands/MixerCommandsTest.cpp)
target_link_libraries(MixerCommandsTest PRIVATE AestraAudioCore)
target_include_directories(MixerCommandsTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME MixerCommandsTest COMMAND MixerCommandsTest)
set_tests_properties(MixerCommandsTest PROPERTIES LABELS "commands;phase2;mixer")

# Clip Commands Test (Trim, Duplicate)
add_executable(ClipCommandsTest Commands/ClipCommandsTest.cpp)
target_link_libraries(ClipCommandsTest PRIVATE AestraAudioCore)
target_include_directories(ClipCommandsTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ClipCommandsTest COMMAND ClipCommandsTest)
set_tests_properties(ClipCommandsTest PROPERTIES LABELS "commands;phase2;clip")

# CommandTransaction Test
add_executable(CommandTransactionTest Commands/CommandTransactionTest.cpp)
target_link_libraries(CommandTransactionTest PRIVATE AestraAudioCore)
target_include_directories(CommandTransactionTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME CommandTransactionTest COMMAND CommandTransactionTest)
set_tests_properties(CommandTransactionTest PROPERTIES LABELS "commands;phase2")

# CommandRegistry Parse Safety Test (regression for #197)
add_executable(CommandRegistryParseSafetyTest Commands/CommandRegistryParseSafetyTest.cpp)
target_include_directories(CommandRegistryParseSafetyTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME CommandRegistryParseSafetyTest COMMAND CommandRegistryParseSafetyTest)
set_tests_properties(CommandRegistryParseSafetyTest PROPERTIES LABELS "commands;regression")

# Rumble State Test (requires runtime plugin system)
if(TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Commands/RumbleStateTest.cpp")
    add_executable(RumbleStateTest Commands/RumbleStateTest.cpp)
    if(UNIX AND NOT APPLE)
        target_link_libraries(RumbleStateTest PRIVATE -Wl,--start-group AestraRumble AestraAudioCore -Wl,--end-group)
    else()
        target_link_libraries(RumbleStateTest PRIVATE AestraPremiumPlugins AestraAudioCore)
    endif()
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

if(TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Commands/RumbleUnauthorizedOutputSafetyTest.cpp")
    add_executable(RumbleUnauthorizedOutputSafetyTest Commands/RumbleUnauthorizedOutputSafetyTest.cpp)
    if(UNIX AND NOT APPLE)
        target_link_libraries(RumbleUnauthorizedOutputSafetyTest PRIVATE -Wl,--start-group AestraRumble AestraAudioCore -Wl,--end-group)
    else()
        target_link_libraries(RumbleUnauthorizedOutputSafetyTest PRIVATE AestraPremiumPlugins AestraAudioCore)
    endif()
    target_include_directories(RumbleUnauthorizedOutputSafetyTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
        ${CMAKE_SOURCE_DIR}/AestraPlugins/AestraRumble/include
    )
    add_test(NAME RumbleUnauthorizedOutputSafetyTest COMMAND RumbleUnauthorizedOutputSafetyTest)
    set_tests_properties(RumbleUnauthorizedOutputSafetyTest PROPERTIES LABELS "plugins;rumble;license;safety")
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

# =============================================================================
# Missing Test Registration (GitHub #250)
# =============================================================================

# Memory Benchmark (integration) — build-only, not registered with CTest.
# Intentionally excluded from CI test runs; run manually for profiling.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Integration/MemoryBenchmark.cpp")
    add_executable(MemoryBenchmark Integration/MemoryBenchmark.cpp)
    target_link_libraries(MemoryBenchmark PRIVATE AestraAudioCore)
    target_include_directories(MemoryBenchmark PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
endif()

# Delay Line Test (Audio/)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Audio/DelayLineTest.cpp")
    add_executable(DelayLineTest Audio/DelayLineTest.cpp)
    target_link_libraries(DelayLineTest PRIVATE AestraAudioCore)
    target_include_directories(DelayLineTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME DelayLineTest COMMAND DelayLineTest)
    set_tests_properties(DelayLineTest PROPERTIES LABELS "audio;dsp;delay")
endif()

# AudioEngine Diagnostics Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/AudioEngineDiagnosticsTest.cpp")
    add_executable(AudioEngineDiagnosticsTest AestraAudio/AudioEngineDiagnosticsTest.cpp)
    target_link_libraries(AudioEngineDiagnosticsTest PRIVATE AestraAudio)
    target_include_directories(AudioEngineDiagnosticsTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME AudioEngineDiagnosticsTest COMMAND AudioEngineDiagnosticsTest)
    set_tests_properties(AudioEngineDiagnosticsTest PROPERTIES LABELS "audio;engine;diagnostics")
endif()

# True Peak Meter Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/TruePeakMeterTest.cpp")
    add_executable(TruePeakMeterTest AestraAudio/TruePeakMeterTest.cpp)
    target_link_libraries(TruePeakMeterTest PRIVATE AestraAudio)
    target_include_directories(TruePeakMeterTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME TruePeakMeterTest COMMAND TruePeakMeterTest)
    set_tests_properties(TruePeakMeterTest PROPERTIES LABELS "audio;dsp;metering")
endif()

# Piano Roll Edge Case Stress Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/PianoRollEdgeCaseStressTest.cpp")
    add_executable(PianoRollEdgeCaseStressTest AestraAudio/PianoRollEdgeCaseStressTest.cpp)
    target_link_libraries(PianoRollEdgeCaseStressTest PRIVATE AestraAudioCore)
    target_include_directories(PianoRollEdgeCaseStressTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME PianoRollEdgeCaseStressTest COMMAND PianoRollEdgeCaseStressTest)
    set_tests_properties(PianoRollEdgeCaseStressTest PROPERTIES LABELS "audio;pianoroll;stress")
endif()

# Plugin Host Stress Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/PluginHostStressTest.cpp")
    add_executable(PluginHostStressTest AestraAudio/PluginHostStressTest.cpp)
    target_link_libraries(PluginHostStressTest PRIVATE AestraAudio)
    target_include_directories(PluginHostStressTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME PluginHostStressTest COMMAND PluginHostStressTest)
    set_tests_properties(PluginHostStressTest PROPERTIES LABELS "audio;plugin;stress")
endif()

# Transport Stress Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/TransportStressTest.cpp")
    add_executable(TransportStressTest AestraAudio/TransportStressTest.cpp)
    target_link_libraries(TransportStressTest PRIVATE AestraAudio)
    target_include_directories(TransportStressTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME TransportStressTest COMMAND TransportStressTest)
    set_tests_properties(TransportStressTest PROPERTIES LABELS "audio;transport;stress")
endif()

# Pathological Chaos Stress Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/PathologicalChaosStressTest.cpp")
    add_executable(PathologicalChaosStressTest AestraAudio/PathologicalChaosStressTest.cpp)
    target_link_libraries(PathologicalChaosStressTest PRIVATE AestraAudio)
    target_include_directories(PathologicalChaosStressTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME PathologicalChaosStressTest COMMAND PathologicalChaosStressTest)
    set_tests_properties(PathologicalChaosStressTest PROPERTIES LABELS "audio;stress;chaos")
endif()

# Mixer Channel Stress Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/MixerChannelStressTest.cpp")
    add_executable(MixerChannelStressTest AestraAudio/MixerChannelStressTest.cpp)
    target_link_libraries(MixerChannelStressTest PRIVATE AestraAudio)
    target_include_directories(MixerChannelStressTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME MixerChannelStressTest COMMAND MixerChannelStressTest)
    set_tests_properties(MixerChannelStressTest PROPERTIES LABELS "audio;mixer;stress")
endif()

# Track Manager Stress Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/TrackManagerStressTest.cpp")
    add_executable(TrackManagerStressTest AestraAudio/TrackManagerStressTest.cpp)
    target_link_libraries(TrackManagerStressTest PRIVATE AestraAudio)
    target_include_directories(TrackManagerStressTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME TrackManagerStressTest COMMAND TrackManagerStressTest)
    set_tests_properties(TrackManagerStressTest PROPERTIES LABELS "audio;trackmanager;stress")
endif()

# File Browser Stress Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/FileBrowserStressTest.cpp")
    add_executable(FileBrowserStressTest AestraAudio/FileBrowserStressTest.cpp)
    target_link_libraries(FileBrowserStressTest PRIVATE AestraAudioCore)
    target_include_directories(FileBrowserStressTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME FileBrowserStressTest COMMAND FileBrowserStressTest)
    set_tests_properties(FileBrowserStressTest PROPERTIES LABELS "audio;filebrowser;stress")
endif()

# Realtime Scheduling Test (Linux-only: uses sched_getscheduler, RLIMIT_RTPRIO, /proc/self/status)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/RealtimeSchedulingTest.cpp")
    add_executable(RealtimeSchedulingTest AestraAudio/RealtimeSchedulingTest.cpp)
    target_link_libraries(RealtimeSchedulingTest PRIVATE AestraAudioCore)
    target_include_directories(RealtimeSchedulingTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME RealtimeSchedulingTest COMMAND RealtimeSchedulingTest)
        set_tests_properties(RealtimeSchedulingTest PROPERTIES LABELS "audio;realtime;scheduling")
    endif()
endif()

# Latency Compensation Test
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/LatencyCompensationTest.cpp")
    add_executable(LatencyCompensationTest AestraAudio/LatencyCompensationTest.cpp)
    target_link_libraries(LatencyCompensationTest PRIVATE AestraAudio)
    target_include_directories(LatencyCompensationTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME LatencyCompensationTest COMMAND LatencyCompensationTest)
    set_tests_properties(LatencyCompensationTest PROPERTIES LABELS "audio;latency;pdc")
endif()
