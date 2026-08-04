# =============================================================================
# Project Tests — clips, lanes, drag/drop and project dirty state.
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

# Drop-as-one-undo-step regression test (#551)
add_executable(DropTransactionTest AestraAudio/DropTransactionTest.cpp)
target_link_libraries(DropTransactionTest PRIVATE AestraAudioCore)
target_include_directories(DropTransactionTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME DropTransactionTest COMMAND DropTransactionTest)
set_tests_properties(DropTransactionTest PROPERTIES LABELS "audio;commands;project")

# Cancelled-drag history regression test (#551)
add_executable(CancelledDragHistoryTest AestraAudio/CancelledDragHistoryTest.cpp)
target_link_libraries(CancelledDragHistoryTest PRIVATE AestraAudioCore)
target_include_directories(CancelledDragHistoryTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME CancelledDragHistoryTest COMMAND CancelledDragHistoryTest)
set_tests_properties(CancelledDragHistoryTest PROPERTIES LABELS "audio;commands;project")

# Project dirty-state integrity regression test (#551)
add_executable(ProjectDirtyStateTest AestraAudio/ProjectDirtyStateTest.cpp)
target_link_libraries(ProjectDirtyStateTest PRIVATE AestraAudioCore)
target_include_directories(ProjectDirtyStateTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME ProjectDirtyStateTest COMMAND ProjectDirtyStateTest)
set_tests_properties(ProjectDirtyStateTest PROPERTIES LABELS "audio;commands;project;contract:durability")

# Audio clip instance gain, pan, and fade rendering regression test
add_executable(AudioClipInstanceRenderTest AestraAudio/AudioClipInstanceRenderTest.cpp)
target_link_libraries(AudioClipInstanceRenderTest PRIVATE AestraAudioCore)
target_include_directories(AudioClipInstanceRenderTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AudioClipInstanceRenderTest COMMAND AudioClipInstanceRenderTest)
set_tests_properties(AudioClipInstanceRenderTest PROPERTIES LABELS "audio;routing")

# Project-format compatibility policy: migration completeness, observable
# outcomes, historical fixtures, and non-destructive unsupported-version loads.
add_executable(ProjectCompatibilityPolicyTest
    Integration/ProjectCompatibilityPolicyTest.cpp
    ${CMAKE_SOURCE_DIR}/Source/Core/ProjectSerializer.cpp
)
target_link_libraries(ProjectCompatibilityPolicyTest PRIVATE AestraAudio)
target_include_directories(ProjectCompatibilityPolicyTest PRIVATE
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
target_compile_definitions(ProjectCompatibilityPolicyTest PRIVATE
    AESTRA_PROJECT_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/ProjectFormat"
)
add_test(NAME ProjectCompatibilityPolicyTest COMMAND ProjectCompatibilityPolicyTest)
set_tests_properties(ProjectCompatibilityPolicyTest PROPERTIES
    LABELS "integration;serialization;migration;compatibility;contract:durability"
)

# Source/ has no linkable application-layer target (#666), so this narrow
# source guard keeps the load lifecycle call site from reverting to an
# unconditional clean until the application layer can have a behavioral test.
add_test(
    NAME ProjectMigrationLifecycleGuardTest
    COMMAND ${CMAKE_COMMAND}
        -DAESTRA_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/Guards/verify_project_migration_lifecycle.cmake
)
set_tests_properties(ProjectMigrationLifecycleGuardTest PROPERTIES
    LABELS "project;migration;compatibility;guard"
)

add_test(NAME ProjectFixtureImmutabilityTest
    COMMAND ${CMAKE_COMMAND}
        "-DFIXTURE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/Fixtures/ProjectFormat"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/Guards/verify_project_fixture_manifest.cmake"
)
set_tests_properties(ProjectFixtureImmutabilityTest PROPERTIES
    LABELS "serialization;fixtures;compatibility;contract:durability"
)

# --- Crash-flag lifecycle (#675) -------------------------------------------
# Header-only holder, so this links nothing from Source/ — which is why it can
# exist at all while Source/ has no library target (#666).
add_executable(CrashFlagPathTest App/CrashFlagPathTest.cpp)
target_include_directories(CrashFlagPathTest PRIVATE ${CMAKE_SOURCE_DIR}/Source/App)
add_test(NAME CrashFlagPathTest COMMAND CrashFlagPathTest)
set_tests_properties(CrashFlagPathTest PROPERTIES LABELS "app;recovery;regression;contract:durability")

# Source guard: the shutdown ORDERING is the invariant that makes the crash flag
# meaningful, and it is not expressible in a unit test while Source/ cannot be
# linked. Path resolution lives in Tests/Guards/ so the blast-radius classifier
# does not treat the app source path as a compiled input (lesson from #669).
add_test(
    NAME CrashFlagShutdownOrderGuardTest
    COMMAND ${CMAKE_COMMAND}
        -DAESTRA_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/Guards/verify_crash_flag_shutdown_order.cmake
)
set_tests_properties(CrashFlagShutdownOrderGuardTest PROPERTIES
    LABELS "app;recovery;guard"
)

add_test(
    NAME ProjectLoadReportLifecycleGuardTest
    COMMAND ${CMAKE_COMMAND}
        -DAESTRA_SOURCE_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/Guards/verify_project_load_report_lifecycle.cmake
)
set_tests_properties(ProjectLoadReportLifecycleGuardTest PROPERTIES
    LABELS "app;project;muse;recovery;guard"
)

# SourceManager revision counter — the change signal TrackManagerUI uses to
# notice sources that are ready but still have no waveform cache.
add_executable(SourceManagerRevisionTest AestraAudio/SourceManagerRevisionTest.cpp)
target_link_libraries(SourceManagerRevisionTest PRIVATE AestraAudioCore)
target_include_directories(SourceManagerRevisionTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME SourceManagerRevisionTest COMMAND SourceManagerRevisionTest)
set_tests_properties(SourceManagerRevisionTest PROPERTIES LABELS "audio;waveform")
