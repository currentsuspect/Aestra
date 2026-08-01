# =============================================================================
# Commands Tests — undo/redo system, the Muse surface, and host capabilities.
# =============================================================================
#
# Split out of Tests/CMakeLists.txt so parallel branches stop colliding. Every
# new target here used to be appended just above add_executable(MuseSocketServerTest),
# so two independent PRs adding unrelated tests conflicted on that one line —
# four rebases in a single day, none of them about anything real, and each one
# costing a full review cycle because a force-push dismisses approval.
#
# WHEN ADDING A TEST: append it at the END of the sub-section it belongs to.
# Sub-sections are append-only, so two branches adding to different sub-sections
# never touch the same lines. Resist inserting next to a target your test merely
# resembles — that recreates the shared insertion point this split removed.
# =============================================================================

# CommandHistory Test
# --- Undo/redo core ----------------------------------------------------------
# History, transactions, macros and the command registry itself.
# Append new targets for this group at the END of this sub-section.

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

# MuseService — the structured JSON surface agents drive Aestra through.
add_executable(DeleteTrackUndoTest Commands/DeleteTrackUndoTest.cpp)
target_link_libraries(DeleteTrackUndoTest PRIVATE AestraAudioCore)
target_include_directories(DeleteTrackUndoTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME DeleteTrackUndoTest COMMAND DeleteTrackUndoTest)
set_tests_properties(DeleteTrackUndoTest PROPERTIES LABELS "commands;regression")

# --- Muse surface ------------------------------------------------------------
# The JSON verb surface agents drive Aestra through, and its clients.
# Append new targets for this group at the END of this sub-section.

add_executable(MuseServiceTest Commands/MuseServiceTest.cpp)
target_link_libraries(MuseServiceTest PRIVATE AestraAudioCore)
target_include_directories(MuseServiceTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME MuseServiceTest COMMAND MuseServiceTest)
set_tests_properties(MuseServiceTest PROPERTIES LABELS "commands;muse")

add_executable(MuseCliRequestTest Commands/MuseCliRequestTest.cpp)
target_include_directories(MuseCliRequestTest PRIVATE
    ${CMAKE_SOURCE_DIR}/Source/MuseAgent
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME MuseCliRequestTest COMMAND MuseCliRequestTest)
set_tests_properties(MuseCliRequestTest PROPERTIES LABELS "commands;muse")

add_executable(ProjectLoadReportTest
    Commands/ProjectLoadReportTest.cpp
    ${CMAKE_SOURCE_DIR}/Source/App/MuseProjectLoadReport.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
)
target_link_libraries(ProjectLoadReportTest PRIVATE AestraAudio)
target_include_directories(ProjectLoadReportTest PRIVATE
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
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(ProjectLoadReportTest PRIVATE
    AESTRA_PROJECT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/ProjectFormat"
)
add_test(NAME ProjectLoadReportTest COMMAND ProjectLoadReportTest)
set_tests_properties(ProjectLoadReportTest PROPERTIES LABELS "commands;muse;project;serialization")

add_executable(RoutingGraphTest Commands/RoutingGraphTest.cpp)
target_link_libraries(RoutingGraphTest PRIVATE AestraAudioCore)
target_include_directories(RoutingGraphTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME RoutingGraphTest COMMAND RoutingGraphTest)
set_tests_properties(RoutingGraphTest PROPERTIES LABELS "commands;muse;routing")

add_executable(LatencyReportTest Commands/LatencyReportTest.cpp)
target_link_libraries(LatencyReportTest PRIVATE AestraAudioCore)
target_include_directories(LatencyReportTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
    ${CMAKE_CURRENT_SOURCE_DIR}/Headless
)
add_test(NAME LatencyReportTest COMMAND LatencyReportTest)
set_tests_properties(LatencyReportTest PROPERTIES LABELS "commands;muse;latency;pdc")

# --- Host capabilities -------------------------------------------------------
# The seam the application registers settings./view./browser. verbs into.
# Append new targets for this group at the END of this sub-section.

add_executable(HostVerbRegistryTest Commands/HostVerbRegistryTest.cpp)
target_link_libraries(HostVerbRegistryTest PRIVATE AestraAudioCore)
target_include_directories(HostVerbRegistryTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME HostVerbRegistryTest COMMAND HostVerbRegistryTest)
set_tests_properties(HostVerbRegistryTest PROPERTIES LABELS "commands;muse")

add_executable(MuseSocketServerTest Commands/MuseSocketServerTest.cpp)
target_link_libraries(MuseSocketServerTest PRIVATE AestraAudioCore)
target_include_directories(MuseSocketServerTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME MuseSocketServerTest COMMAND MuseSocketServerTest)
set_tests_properties(MuseSocketServerTest PROPERTIES LABELS "commands;muse")

if(TARGET MuseAgentCore)
    add_executable(MuseAgentLoopTest Commands/MuseAgentLoopTest.cpp)
    target_link_libraries(MuseAgentLoopTest PRIVATE MuseAgentCore AestraAudioCore)
    target_include_directories(MuseAgentLoopTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME MuseAgentLoopTest COMMAND MuseAgentLoopTest)
    set_tests_properties(MuseAgentLoopTest PROPERTIES LABELS "commands;muse")
endif()

# Rumble state and migration are deterministic pure-logic coverage. Keep this in
# the normal test tier so stable parameter IDs cannot drift behind a runtime gate.
# --- Premium plugin commands -------------------------------------------------
# Guarded on AestraPremiumPlugins; absent from public builds.
# Append new targets for this group at the END of this sub-section.

# TARGET AestraRumble is required as well as AestraPremiumPlugins: the Linux
# branch below links AestraRumble by name, and AestraPlugins only defines that
# target when its AestraRumble subdirectory is present. Without this, a premium
# build with Rumble absent emits -lAestraRumble and fails to link. AestraPlugins
# guards its own use the same way (if(TARGET AestraRumble)).
if(TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND TARGET AestraRumble
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Commands/RumbleStateTest.cpp")
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
    add_test(NAME RumbleStateTest COMMAND RumbleStateTest)
    set_tests_properties(RumbleStateTest PROPERTIES LABELS "plugins;rumble;state")
endif()

# TARGET AestraRumble is required as well as AestraPremiumPlugins: the Linux
# branch below links AestraRumble by name, and AestraPlugins only defines that
# target when its AestraRumble subdirectory is present. Without this, a premium
# build with Rumble absent emits -lAestraRumble and fails to link. AestraPlugins
# guards its own use the same way (if(TARGET AestraRumble)).
if(TARGET AestraAudioCore AND TARGET AestraPremiumPlugins AND TARGET AestraRumble
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Commands/RumbleUnauthorizedOutputSafetyTest.cpp")
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

if(TARGET AestraAudioCore AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Commands/ClipRenderServiceTest.cpp")
    add_executable(ClipRenderServiceTest Commands/ClipRenderServiceTest.cpp)
    target_link_libraries(ClipRenderServiceTest PRIVATE AestraAudioCore)
    target_include_directories(ClipRenderServiceTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME ClipRenderServiceTest COMMAND ClipRenderServiceTest)
    set_tests_properties(ClipRenderServiceTest PROPERTIES LABELS "commands;audio;clip-render")
endif()

if(TARGET AestraAudioCore AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Commands/RenderAudioClipCommandTest.cpp")
    add_executable(RenderAudioClipCommandTest Commands/RenderAudioClipCommandTest.cpp)
    target_link_libraries(RenderAudioClipCommandTest PRIVATE AestraAudioCore)
    target_include_directories(RenderAudioClipCommandTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME RenderAudioClipCommandTest COMMAND RenderAudioClipCommandTest)
    set_tests_properties(RenderAudioClipCommandTest PROPERTIES LABELS "commands;audio;clip-render;undo")
endif()

if(TARGET AestraAudioCore AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Commands/MuseClipRoundTripTest.cpp")
    add_executable(MuseClipRoundTripTest Commands/MuseClipRoundTripTest.cpp)
    target_link_libraries(MuseClipRoundTripTest PRIVATE AestraAudioCore)
    target_include_directories(MuseClipRoundTripTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME MuseClipRoundTripTest COMMAND MuseClipRoundTripTest)
    set_tests_properties(MuseClipRoundTripTest PROPERTIES LABELS "commands;muse;clip;roundtrip")
endif()

if(TARGET AestraAudioCore AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ClipRenderCharacterizationTest.cpp")
    add_executable(ClipRenderCharacterizationTest AestraAudio/ClipRenderCharacterizationTest.cpp)
    target_link_libraries(ClipRenderCharacterizationTest PRIVATE AestraAudioCore)
    target_include_directories(ClipRenderCharacterizationTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME ClipRenderCharacterizationTest COMMAND ClipRenderCharacterizationTest)
    set_tests_properties(ClipRenderCharacterizationTest PROPERTIES LABELS "audio;clip-render;characterization")
endif()

if(TARGET AestraAudioCore AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AestraAudio/ClipSrcTelemetryTest.cpp")
    add_executable(ClipSrcTelemetryTest AestraAudio/ClipSrcTelemetryTest.cpp)
    target_link_libraries(ClipSrcTelemetryTest PRIVATE AestraAudioCore)
    target_include_directories(ClipSrcTelemetryTest PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraAudio/include
        ${CMAKE_SOURCE_DIR}/AestraCore/include
    )
    add_test(NAME ClipSrcTelemetryTest COMMAND ClipSrcTelemetryTest)
    set_tests_properties(ClipSrcTelemetryTest PROPERTIES LABELS "audio;clip-render;telemetry")
endif()

message(STATUS "Commands tests added - Phase 2 undo/redo system")
