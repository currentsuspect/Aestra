# NoAudioEngineSingletonGuard — zero-tolerance source check (R2).
#
# The AudioEngine singleton (getInstance() + a file-static registration pointer
# written from the ctor) was removed after its last reader shipped a crash: the
# accessor aborted when no engine existed, and registration was last-ctor-wins,
# so two live engines silently corrupted the global. Every AudioEngine is now
# explicitly owned by its creator. This guard fails the build the moment any
# equivalent global-instance mechanism returns, in any form:
#
#   - AudioEngine::getInstance          (qualified accessor use/definition)
#   - AudioEngine& getInstance / AudioEngine &getInstance   (in-class redecl)
#   - g_audioEngineInstance             (the registration global, any spelling)
#
# Required -D args:
#   REPO_ROOT  absolute path to the repository root
#
# There is no allowlist on purpose. If you believe you need a process-wide
# engine pointer, wire explicit ownership instead (see AestraAudioController).

if(NOT REPO_ROOT)
    message(FATAL_ERROR "REPO_ROOT must be provided")
endif()

set(scan_dirs
    AestraAudio
    AestraCore
    AestraPlat
    AestraUI
    AestraPlugins
    Source
    Tests
    aestra-core
)

set(violations "")
foreach(dir ${scan_dirs})
    if(NOT EXISTS "${REPO_ROOT}/${dir}")
        continue()
    endif()
    file(GLOB_RECURSE files
        "${REPO_ROOT}/${dir}/*.h"
        "${REPO_ROOT}/${dir}/*.hpp"
        "${REPO_ROOT}/${dir}/*.cpp")
    foreach(f ${files})
        # Vendored third-party trees are not ours to police.
        if(f MATCHES "/External/")
            continue()
        endif()
        file(READ "${f}" contents)
        if(contents MATCHES "AudioEngine::getInstance" OR
           contents MATCHES "AudioEngine[ \t]*&[ \t]*getInstance" OR
           contents MATCHES "g_audioEngineInstance")
            string(REPLACE "${REPO_ROOT}/" "" rel "${f}")
            list(APPEND violations "${rel}")
        endif()
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n  " joined)
    message(FATAL_ERROR
        "AudioEngine singleton mechanism reintroduced in:\n  ${joined}\n"
        "The engine has no process-wide instance. Pass an explicitly owned "
        "AudioEngine (constructor injection / owner reference) instead.")
endif()

message(STATUS "NoAudioEngineSingletonGuard: clean")
