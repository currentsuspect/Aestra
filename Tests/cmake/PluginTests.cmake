# =============================================================================
# Plugin & DSP Tests — EQ, dynamics, modulation, saturation, filters.
# =============================================================================
#
# Split out of Tests/CMakeLists.txt so parallel branches stop colliding (#635),
# following the pattern #614 established for Commands tests. Every new target in
# this area used to be appended at the same line, so two independent PRs adding
# unrelated tests conflicted on it — and because develop dismisses stale
# approvals on ANY new commit, each trivial "keep both blocks" resolution cost a
# full review cycle.
#
# WHEN ADDING A TEST: append it at the END of this file. This fragment is
# append-only, so branches adding to different fragments never touch the same
# lines. Resist inserting next to a target your test merely resembles — that
# recreates the shared insertion point this split removed.
# =============================================================================

# Aestra EQ Plugin Test
add_executable(AestraEQTest AestraAudio/AestraEQTest.cpp)
target_link_libraries(AestraEQTest PRIVATE AestraAudio)
target_include_directories(AestraEQTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraEQTest COMMAND AestraEQTest)
set_tests_properties(AestraEQTest PROPERTIES LABELS "audio;plugins;eq;contract:audio")

# Aestra EQ Measurement Test
add_executable(AestraEQMeasurementTest AestraAudio/AestraEQMeasurementTest.cpp)
target_link_libraries(AestraEQMeasurementTest PRIVATE AestraAudio)
target_include_directories(AestraEQMeasurementTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraEQMeasurementTest COMMAND AestraEQMeasurementTest)
set_tests_properties(AestraEQMeasurementTest PROPERTIES LABELS "audio;plugins;eq;measurement;contract:audio")

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
set_tests_properties(AestraCompPhase0Test PROPERTIES LABELS "audio;plugins;comp;phase0;contract:audio")

# Aestra Comp Phase 1 Test
add_executable(AestraCompPhase1Test AestraAudio/AestraCompPhase1Test.cpp)
target_link_libraries(AestraCompPhase1Test PRIVATE AestraAudio)
target_include_directories(AestraCompPhase1Test PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraCompPhase1Test COMMAND AestraCompPhase1Test)
set_tests_properties(AestraCompPhase1Test PROPERTIES LABELS "audio;plugins;comp;phase1;contract:plugins")

# Aestra Comp Oversampling Test (#228)
add_executable(AestraCompOversamplingTest AestraAudio/AestraCompOversamplingTest.cpp)
target_link_libraries(AestraCompOversamplingTest PRIVATE AestraAudio)
target_include_directories(AestraCompOversamplingTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraCompOversamplingTest COMMAND AestraCompOversamplingTest)
set_tests_properties(AestraCompOversamplingTest PROPERTIES LABELS "audio;plugins;comp;oversampling;contract:audio")

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
    set_tests_properties(AestraCompUpgradeTest PROPERTIES LABELS "audio;plugins;comp;upgrade;contract:plugins")
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
set_tests_properties(AestraCompressorMaterialLab PROPERTIES LABELS "audio;plugins;comp;lab;contract:audio")

# Aestra Delay Upgrade Test
add_executable(AestraDelayUpgradeTest AestraAudio/AestraDelayUpgradeTest.cpp)
target_link_libraries(AestraDelayUpgradeTest PRIVATE AestraAudio)
target_include_directories(AestraDelayUpgradeTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraDelayUpgradeTest COMMAND AestraDelayUpgradeTest)
set_tests_properties(AestraDelayUpgradeTest PROPERTIES LABELS "audio;plugins;delay;upgrade;contract:audio")

add_executable(AestraDriftQualityTest AestraAudio/AestraDriftQualityTest.cpp)
target_link_libraries(AestraDriftQualityTest PRIVATE AestraAudio)
target_include_directories(AestraDriftQualityTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
)
add_test(NAME AestraDriftQualityTest COMMAND AestraDriftQualityTest)
set_tests_properties(AestraDriftQualityTest PROPERTIES LABELS "audio;plugins;drift;quality;contract:audio")

# Aestra Limit Test
add_executable(AestraLimitTest AestraAudio/AestraLimitTest.cpp)
target_link_libraries(AestraLimitTest PRIVATE AestraAudio)
target_include_directories(AestraLimitTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraLimitTest COMMAND AestraLimitTest)
set_tests_properties(AestraLimitTest PROPERTIES LABELS "audio;plugins;limiter;contract:audio")

# Aestra Sat Test
add_executable(AestraSatTest AestraAudio/AestraSatTest.cpp)
target_link_libraries(AestraSatTest PRIVATE AestraAudio)
target_include_directories(AestraSatTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraSatTest COMMAND AestraSatTest)
set_tests_properties(AestraSatTest PROPERTIES LABELS "audio;plugins;saturation;contract:audio")

# Aestra Filter plugin test. Not to be confused with AestraFilterTest, which is
# the DSP::Filter lab (AestraAudio/FilterTest.cpp, registered in Tests/CMakeLists.txt
# and gated on AESTRA_ENABLE_EXPERIMENTAL_TESTS).
add_executable(AestraFilterPluginTest AestraAudio/AestraFilterPluginTest.cpp)
target_link_libraries(AestraFilterPluginTest PRIVATE AestraAudio)
target_include_directories(AestraFilterPluginTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraFilterPluginTest COMMAND AestraFilterPluginTest)
set_tests_properties(AestraFilterPluginTest PROPERTIES LABELS "audio;plugins;filter;contract:audio")

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
    # No contract label. This case reports per-callback cost and asserts no
    # threshold, so it cannot fail on a regression — calling it
    # contract:realtime would claim a gate that does not exist.
    #
    # The exemption needs two independent keys: this role, and an exact-name
    # entry in Tests/policy/benchmark-allowlist.txt. Neither alone exempts a
    # case, so adding this label to a failing test buys nothing without a
    # separate change to the allowlist.
    set_tests_properties(AestraFilterBench PROPERTIES LABELS "audio;benchmark;experimental;role:benchmark")
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
set_tests_properties(AestraOTTTest PROPERTIES LABELS "audio;plugins;ott;contract:audio")

# Aestra LFO Test
add_executable(AestraLFOTest AestraAudio/AestraLFOTest.cpp)
target_link_libraries(AestraLFOTest PRIVATE AestraAudio)
target_include_directories(AestraLFOTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AestraLFOTest COMMAND AestraLFOTest)
set_tests_properties(AestraLFOTest PROPERTIES LABELS "audio;plugins;lfo;contract:audio")

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
set_tests_properties(PluginInitContractTest PROPERTIES LABELS "audio;plugins;lifecycle;contract:plugins")

# Missing-plugin state preservation (#647) — a plugin that cannot be
# instantiated must survive load/save instead of being silently erased.
add_executable(EffectChainMissingPluginTest AestraAudio/EffectChainMissingPluginTest.cpp)
target_link_libraries(EffectChainMissingPluginTest PRIVATE AestraAudio)
target_include_directories(EffectChainMissingPluginTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME EffectChainMissingPluginTest COMMAND EffectChainMissingPluginTest)
set_tests_properties(EffectChainMissingPluginTest PROPERTIES LABELS "audio;plugins;serialization;contract:durability")

# Plugin-instance identity must survive chain reordering (#667) — automation
# addresses the instance a curve was drawn for, never the position it occupied.
add_executable(EffectChainInstanceIdentityTest AestraAudio/EffectChainInstanceIdentityTest.cpp)
target_link_libraries(EffectChainInstanceIdentityTest PRIVATE AestraAudio)
target_include_directories(EffectChainInstanceIdentityTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME EffectChainInstanceIdentityTest COMMAND EffectChainInstanceIdentityTest)
set_tests_properties(EffectChainInstanceIdentityTest PROPERTIES LABELS "audio;plugins;automation;regression;contract:plugins")

# Automation Identity Resolution Test (contract I2/I3/I8/I10)
add_executable(AutomationIdentityResolutionTest AestraAudio/AutomationIdentityResolutionTest.cpp)
target_link_libraries(AutomationIdentityResolutionTest PRIVATE AestraAudio)
target_include_directories(AutomationIdentityResolutionTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AutomationIdentityResolutionTest COMMAND AutomationIdentityResolutionTest)

# Master strip is a plugin host like any other channel (triage 2026-08-14):
# processing on the summed master buffer + save/load roundtrip + old-project
# compatibility.
add_executable(MasterEffectChainTest
    AestraAudio/MasterEffectChainTest.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
)
target_link_libraries(MasterEffectChainTest PRIVATE AestraAudio)
target_include_directories(MasterEffectChainTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
    ${CMAKE_SOURCE_DIR}/Source
    ${CMAKE_SOURCE_DIR}/Tests
)
add_test(NAME MasterEffectChainTest COMMAND MasterEffectChainTest)
set_tests_properties(MasterEffectChainTest PROPERTIES LABELS "audio;plugins;mixer;regression;persistence;contract:plugins")

