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
set_tests_properties(ProjectDirtyStateTest PROPERTIES LABELS "audio;commands;project")

# Audio clip instance gain, pan, and fade rendering regression test
add_executable(AudioClipInstanceRenderTest AestraAudio/AudioClipInstanceRenderTest.cpp)
target_link_libraries(AudioClipInstanceRenderTest PRIVATE AestraAudioCore)
target_include_directories(AudioClipInstanceRenderTest PRIVATE
    ${CMAKE_SOURCE_DIR}/AestraAudio/include
    ${CMAKE_SOURCE_DIR}/AestraCore/include
)
add_test(NAME AudioClipInstanceRenderTest COMMAND AudioClipInstanceRenderTest)
set_tests_properties(AudioClipInstanceRenderTest PROPERTIES LABELS "audio;routing")

# Arsenal route-mode project round-trip compatibility test
