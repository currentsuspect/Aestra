# NoAudioEngineSingletonGuard — zero-tolerance source check (R2).
#
# The AudioEngine singleton (getInstance() + a file-static registration pointer
# written from the ctor) was removed after its last reader shipped a crash: the
# accessor aborted when no engine existed, and registration was last-ctor-wins,
# so two live engines silently corrupted the global. Every AudioEngine is now
# explicitly owned by its creator. This guard fails the build if an equivalent
# global-instance mechanism returns.
#
# Enforced patterns:
#   1. AudioEngine::getInstance            legacy accessor, any qualified use
#   2. AudioEngine& getInstance            legacy in-class redeclaration shape
#   3. g_audioEngineInstance               the legacy registration global
#   4. static AudioEngine [*&...]          ANY static engine object, pointer,
#                                          reference, class member, function-
#                                          local static, or accessor return —
#                                          catches renamed globals structurally
#   5. AudioEngine* g_<name>               engine pointer under the house
#                                          global-naming convention, any name
#
# Contract limits (deliberate): this is a textual tripwire, not a static
# analyzer. A determined hidden indirection (e.g. an engine pointer parked in
# an unrelated named struct) is caught by review and the ownership doctrine
# (AudioEngineOwnershipTest), not by regex. The point of patterns 4-5 is that
# the *realistic* reintroductions — a renamed static pointer or accessor —
# cannot land silently.
#
# Exemption (single, tracked): CommandRegistry.{h,cpp} carry an explicitly
# injected static AudioEngine* for Muse transport commands. That is a distinct
# (still undesirable) mechanism scheduled for context-injection migration in
# issue #559; patterns 1-3 remain enforced there, only pattern 4/5 is waived.
# Do not add further exemptions — remove this one via #559.
#
# Required -D args:
#   REPO_ROOT  absolute path to the repository root

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

set(exempt_static_pattern
    "AestraAudio/include/Commands/CommandRegistry.h"
    "AestraAudio/src/Commands/CommandRegistry.cpp"
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
        string(REPLACE "${REPO_ROOT}/" "" rel "${f}")

        # Patterns 1-3: legacy mechanism, enforced everywhere with no exemption.
        if(contents MATCHES "AudioEngine::getInstance" OR
           contents MATCHES "AudioEngine[ \t]*&[ \t]*getInstance" OR
           contents MATCHES "g_audioEngineInstance")
            list(APPEND violations "${rel} (legacy singleton token)")
            continue()
        endif()

        # Patterns 4-5: structural static/global engine shapes.
        list(FIND exempt_static_pattern "${rel}" exempt_idx)
        if(exempt_idx EQUAL -1)
            # Two shapes so longer type names (AudioEngineXyz) cannot false-
            # positive: pointer/ref immediately after the type, or a whitespace-
            # separated object/accessor name.
            if(contents MATCHES "static[ \t]+(Aestra::Audio::)?AudioEngine[ \t]*[*&]" OR
               contents MATCHES "static[ \t]+(Aestra::Audio::)?AudioEngine[ \t]+[a-zA-Z_]" OR
               contents MATCHES "AudioEngine[ \t]*\\*[ \t]*g_[A-Za-z0-9_]")
                list(APPEND violations "${rel} (static/global engine shape)")
            endif()
        endif()
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n  " joined)
    message(FATAL_ERROR
        "AudioEngine singleton mechanism reintroduced in:\n  ${joined}\n"
        "The engine has no process-wide instance. Pass an explicitly owned "
        "AudioEngine (constructor injection / owner reference) instead. "
        "The only tracked exemption is CommandRegistry (issue #559).")
endif()

message(STATUS "NoAudioEngineSingletonGuard: clean")
