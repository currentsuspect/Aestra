# Self-test for no_audioengine_singleton.cmake — pins the guard's contract.
#
# Materializes fixture trees in WORK_DIR and runs the real guard against each,
# asserting it fails on every forbidden shape (including renamed mechanisms) and
# passes on benign ownership patterns and the prefix-type control. Since #559
# retired the CommandRegistry exemption, it also pins that the former-exempt
# path is now enforced like everywhere else.
#
# Required -D args:
#   GUARD_SCRIPT  absolute path to no_audioengine_singleton.cmake
#   WORK_DIR      scratch directory (recreated per fixture)

if(NOT GUARD_SCRIPT OR NOT WORK_DIR)
    message(FATAL_ERROR "GUARD_SCRIPT and WORK_DIR must be provided")
endif()

set(failures 0)

# run_fixture(<name> <relative-path> <content> <expect>)  expect: PASS|FAIL
function(run_fixture name relpath content expect)
    file(REMOVE_RECURSE "${WORK_DIR}/tree")
    get_filename_component(dir "${WORK_DIR}/tree/${relpath}" DIRECTORY)
    file(MAKE_DIRECTORY "${dir}")
    file(WRITE "${WORK_DIR}/tree/${relpath}" "${content}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -DREPO_ROOT=${WORK_DIR}/tree -P "${GUARD_SCRIPT}"
        RESULT_VARIABLE rc
        OUTPUT_QUIET ERROR_QUIET)
    if(expect STREQUAL "FAIL" AND rc EQUAL 0)
        message(SEND_ERROR "fixture '${name}': guard PASSED but must FAIL")
        set(failures 1 PARENT_SCOPE)
    elseif(expect STREQUAL "PASS" AND NOT rc EQUAL 0)
        message(SEND_ERROR "fixture '${name}': guard FAILED but must PASS")
        set(failures 1 PARENT_SCOPE)
    else()
        message(STATUS "fixture '${name}': ok (${expect})")
    endif()
endfunction()

# ── Forbidden shapes ─────────────────────────────────────────────────────────
run_fixture(legacy-accessor-call "Source/A.cpp"
    "void f() { auto& e = AudioEngine::getInstance()\; }" FAIL)
run_fixture(legacy-inclass-decl "AestraAudio/B.h"
    "class AudioEngine { static AudioEngine& getInstance()\; }\;" FAIL)
run_fixture(legacy-global "AestraAudio/C.cpp"
    "static AudioEngine* g_audioEngineInstance = nullptr\;" FAIL)
run_fixture(renamed-static-pointer "Source/D.cpp"
    "static AudioEngine* s_engine = nullptr\;" FAIL)
run_fixture(renamed-static-object "Source/E.cpp"
    "static AudioEngine s_theEngine\;" FAIL)
run_fixture(renamed-static-ref-accessor "Source/F.h"
    "static AudioEngine& theEngine()\;" FAIL)
run_fixture(renamed-qualified-static "Tests/G.cpp"
    "static Aestra::Audio::AudioEngine* s_e = nullptr\;" FAIL)
run_fixture(g-prefixed-pointer "Source/H.cpp"
    "AudioEngine* g_theEngine = nullptr\;" FAIL)

# ── Benign ownership patterns ────────────────────────────────────────────────
run_fixture(member-pointer "Source/I.h"
    "class C { AudioEngine* m_engine = nullptr\; }\;" PASS)
run_fixture(owned-unique-ptr "Source/J.h"
    "class C { std::unique_ptr<AudioEngine> m_audioEngine\; }\;" PASS)
run_fixture(stack-local "Tests/K.cpp"
    "void t() { AudioEngine engine\; }" PASS)
run_fixture(prefix-type-control "Source/L.cpp"
    "static AudioEngineDiagnosticsHelper s_helper\;" PASS)

# ── Exemption removed (#559): CommandRegistry is enforced like anywhere else ─
# The path that used to be waived for pattern 4/5 now fails on the same static
# pointer shape — proving the exemption is gone, not just unused.
run_fixture(former-exempt-path-now-fails "AestraAudio/include/Commands/CommandRegistry.h"
    "class CommandRegistry { static AudioEngine* s_engine\; }\;" FAIL)
run_fixture(former-exempt-path-legacy-fails "AestraAudio/src/Commands/CommandRegistry.cpp"
    "static AudioEngine* g_audioEngineInstance = nullptr\;" FAIL)
run_fixture(commands-dir-same-shape-fails "AestraAudio/src/Commands/OtherFile.cpp"
    "static AudioEngine* s_engine = nullptr\;" FAIL)

file(REMOVE_RECURSE "${WORK_DIR}/tree")

if(failures)
    message(FATAL_ERROR "NoAudioEngineSingletonGuard self-test: FAILED")
endif()
message(STATUS "NoAudioEngineSingletonGuard self-test: all fixtures ok")
