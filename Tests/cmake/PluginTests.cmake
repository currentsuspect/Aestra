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

# Crashed-plugin state preservation (#931) — a helper death between two saves
# must not erase the state the first save captured.
add_executable(EffectChainCrashedPluginStateTest AestraAudio/EffectChainCrashedPluginStateTest.cpp)
target_link_libraries(EffectChainCrashedPluginStateTest PRIVATE AestraAudio)
target_include_directories(EffectChainCrashedPluginStateTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME EffectChainCrashedPluginStateTest COMMAND EffectChainCrashedPluginStateTest)
set_tests_properties(EffectChainCrashedPluginStateTest PROPERTIES LABELS "audio;plugins;serialization;regression;contract:durability")

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

# FD-16: read-only automation presence aggregation (per-channel curve counts
# and target masks over playlist lanes). Unassigned mixerChannelId 0 matches
# nothing — zero is unassigned, never master.
add_executable(AutomationPresenceTest AestraAudio/AutomationPresenceTest.cpp)
target_link_libraries(AutomationPresenceTest PRIVATE AestraAudio)
target_include_directories(AutomationPresenceTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AutomationPresenceTest COMMAND AutomationPresenceTest)
set_tests_properties(AutomationPresenceTest PROPERTIES LABELS "audio;automation;mixer;contract:audio")

# FD-14 recovery regression: the loader's clear block must empty the track
# table (clearAllTracks) or every restored ownership entry collides with
# surviving default tracks and the migration doubles the table.
add_executable(TrackRestoreAfterClearTest AestraAudio/TrackRestoreAfterClearTest.cpp)
target_link_libraries(TrackRestoreAfterClearTest PRIVATE AestraAudio)
target_include_directories(TrackRestoreAfterClearTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME TrackRestoreAfterClearTest COMMAND TrackRestoreAfterClearTest)
set_tests_properties(TrackRestoreAfterClearTest PROPERTIES LABELS "audio;regression;contract:durability")

# #747: varispeed tempo-fit math — span/rate relationship, varispeed clamp,
# input guards. Pitch-follows-tempo is definitional, not a defect.
add_executable(ClipFitToBarsTest AestraAudio/ClipFitToBarsTest.cpp)
target_link_libraries(ClipFitToBarsTest PRIVATE AestraAudio)
target_include_directories(ClipFitToBarsTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ClipFitToBarsTest COMMAND ClipFitToBarsTest)
set_tests_properties(ClipFitToBarsTest PROPERTIES LABELS "audio;automation;contract:audio")

# Automation Identity Resolution Test (contract I2/I3/I8/I10)
add_executable(AutomationIdentityResolutionTest AestraAudio/AutomationIdentityResolutionTest.cpp)
target_link_libraries(AutomationIdentityResolutionTest PRIVATE AestraAudio)
target_include_directories(AutomationIdentityResolutionTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AutomationIdentityResolutionTest COMMAND AutomationIdentityResolutionTest)
set_tests_properties(AutomationIdentityResolutionTest PROPERTIES LABELS "audio;automation;contract:audio")

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

# Registry-driven conformance sweep — one contract, every built-in plugin. The
# sweep reads InternalPluginRegistry, so a plugin gets its generic contract
# coverage by being registered, with no test code of its own. This is the
# family-wide sweep that PluginInitContractTest hand-lists (and therefore
# silently skips EQ, Transient and the sampler for).
add_executable(PluginConformanceSweepTest AestraAudio/PluginConformanceSweepTest.cpp)
target_link_libraries(PluginConformanceSweepTest PRIVATE AestraAudio)
target_include_directories(PluginConformanceSweepTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME PluginConformanceSweepTest COMMAND PluginConformanceSweepTest)
set_tests_properties(PluginConformanceSweepTest PROPERTIES LABELS "audio;plugins;registry;conformance;contract:plugins" TIMEOUT 300)

# The base class must not change a byte of any plugin's saved state: every
# built-in shipped {magic, version, params[count]}, and a migrated plugin has
# to produce the identical blob or every saved project breaks (AGENTS.md §12).
add_executable(InternalPluginBaseBlobTest AestraAudio/InternalPluginBaseBlobTest.cpp)
target_link_libraries(InternalPluginBaseBlobTest PRIVATE AestraAudio)
target_include_directories(InternalPluginBaseBlobTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME InternalPluginBaseBlobTest COMMAND InternalPluginBaseBlobTest)
set_tests_properties(InternalPluginBaseBlobTest PROPERTIES LABELS "audio;plugins;serialization;contract:durability" TIMEOUT 120)

# A plugin that IS installed but rejects its own saved state must be reported
# (#1014). This is the mirror of EffectChainMissingPluginTest: that one depends
# on NO plugin resolving so it can drive the placeholder path, and this one needs
# a REAL built-in so it can drive the installed-but-rejecting path. The two
# failure modes look identical from the outside and are not the same event —
# the first loses nothing, the second loses the user's settings.
add_executable(EffectChainUnreadablePluginStateTest
    AestraAudio/EffectChainUnreadablePluginStateTest.cpp
)
target_link_libraries(EffectChainUnreadablePluginStateTest PRIVATE AestraAudio)
target_include_directories(EffectChainUnreadablePluginStateTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME EffectChainUnreadablePluginStateTest COMMAND EffectChainUnreadablePluginStateTest)
set_tests_properties(EffectChainUnreadablePluginStateTest PROPERTIES
    LABELS "audio;plugins;persistence;regression;contract:durability"
    TIMEOUT 120)

# #1015 — the state-load policy is two decisions, not one. An out-of-range
# value clamps (ambiguous: it may be a legitimate value from a build whose range
# was wider, and nothing forces a range change to bump kStateVersion); a
# non-finite value rejects the whole blob (unambiguous corruption). One test file
# pins both, because the damaging regression is one half drifting into the other
# -- NaN clamped into range is a plausible number nobody chose.
add_executable(InternalPluginBaseStatePolicyTest
    AestraAudio/InternalPluginBaseStatePolicyTest.cpp
)
target_link_libraries(InternalPluginBaseStatePolicyTest PRIVATE AestraAudio)
target_include_directories(InternalPluginBaseStatePolicyTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME InternalPluginBaseStatePolicyTest COMMAND InternalPluginBaseStatePolicyTest)
set_tests_properties(InternalPluginBaseStatePolicyTest PROPERTIES
    LABELS "audio;plugins;serialization;contract:durability"
    TIMEOUT 120)
