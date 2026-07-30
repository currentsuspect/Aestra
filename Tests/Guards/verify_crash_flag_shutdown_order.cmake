# Shutdown-ordering guard for the crash flag (#675).
#
# Two properties have to hold together, and neither is expressible as a unit
# test while Source/ has no linkable library target (#666):
#
#   1. The crash flag is cleared AFTER all fallible teardown work, so a crash
#      during shutdown leaves the flag behind for recovery. This is why the fix
#      for #675 must NOT be "move clearCrashFlag() earlier".
#   2. The clear uses the path retained at startup, not a freshly resolved one.
#      Platform::shutdown() destroys the platform utilities, after which
#      getAppDataPath() silently falls back to the working directory — so
#      re-resolving there finds nothing, removes nothing, and logs nothing.
#
# The application source path is resolved HERE rather than passed in from
# Tests/cmake/*.cmake: scripts/ci/classify-changes.sh derives its
# headless-compiled deny-list by grepping those files for `(Source|AestraUI)/…
# .cpp`, and a literal app-source path there is indistinguishable from a
# compiled input (lesson from #669). Tests/Guards/ is not scanned.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED AESTRA_SOURCE_ROOT OR AESTRA_SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "AESTRA_SOURCE_ROOT was not provided to the crash-flag guard")
endif()

set(APP_SOURCE "${AESTRA_SOURCE_ROOT}/Source/App/AestraApp.cpp")
if(NOT EXISTS "${APP_SOURCE}")
    message(FATAL_ERROR "Guard cannot find the application source at ${APP_SOURCE}")
endif()

file(READ "${APP_SOURCE}" APP_TEXT)

# --- Property 2: the clear uses the retained path --------------------------
# Scoped to the body of clearCrashFlag(). A whole-file search is NOT sufficient:
# the helper activeCrashFlagPath() legitimately contains both
# CrashFlagPath::isPrimed() and CrashFlagPath::get(), so a file-wide match would
# still succeed after clearCrashFlag() regressed to fresh resolution. That hole
# was demonstrated, not hypothesised — the guard passed against a deliberately
# regressed clear before this was scoped.
string(FIND "${APP_TEXT}" "void AestraApp::clearCrashFlag()" CLEAR_FN_AT)
if(CLEAR_FN_AT EQUAL -1)
    message(FATAL_ERROR "Could not find AestraApp::clearCrashFlag() to inspect")
endif()
# Body runs to the next function definition at column 0, which is the closing
# brace followed by a blank line and a new top-level definition.
string(SUBSTRING "${APP_TEXT}" ${CLEAR_FN_AT} 1200 CLEAR_BODY)
string(FIND "${CLEAR_BODY}" "\n}" CLEAR_END)
if(NOT CLEAR_END EQUAL -1)
    string(SUBSTRING "${CLEAR_BODY}" 0 ${CLEAR_END} CLEAR_BODY)
endif()

string(FIND "${CLEAR_BODY}" "CrashFlagPath::isPrimed()" USES_PRIMED_CHECK)
string(FIND "${CLEAR_BODY}" "CrashFlagPath::get()" USES_RETAINED_PATH)
string(FIND "${CLEAR_BODY}" "getCrashFlagPath()" CLEAR_RERESOLVES)
if(USES_PRIMED_CHECK EQUAL -1 OR USES_RETAINED_PATH EQUAL -1)
    message(FATAL_ERROR
        "clearCrashFlag no longer uses the retained crash-flag path.\n"
        "Resolving it during shutdown goes through getAppDataPath(), which "
        "falls back to the working directory once Platform::shutdown() has "
        "released the platform utilities — the flag is then never cleared and "
        "a real crash is never detected (#675).")
endif()
if(NOT CLEAR_RERESOLVES EQUAL -1)
    message(FATAL_ERROR
        "clearCrashFlag calls getCrashFlagPath() again.\n"
        "By that point Platform::shutdown() has released the platform "
        "utilities, so the path resolves to the working directory (#675).")
endif()

# Priming must happen at a real call site, not merely be defined somewhere.
string(FIND "${APP_TEXT}" "CrashFlagPath::prime(getCrashFlagPath())" PRIMES_PATH)
if(PRIMES_PATH EQUAL -1)
    message(FATAL_ERROR
        "Nothing primes the crash-flag path from a resolved value while platform "
        "utilities are alive (#675).")
endif()

# --- Property 1: the clear still happens after platform teardown -----------
string(FIND "${APP_TEXT}" "Platform::shutdown();" PLATFORM_SHUTDOWN_AT)
string(FIND "${APP_TEXT}" "clearCrashFlag();" CLEAR_CALL_AT)
if(PLATFORM_SHUTDOWN_AT EQUAL -1)
    message(FATAL_ERROR "Could not find Platform::shutdown() in the application shutdown path")
endif()
if(CLEAR_CALL_AT EQUAL -1)
    message(FATAL_ERROR "Could not find the clearCrashFlag() call in the application shutdown path")
endif()
if(CLEAR_CALL_AT LESS PLATFORM_SHUTDOWN_AT)
    message(FATAL_ERROR
        "clearCrashFlag() now runs BEFORE Platform::shutdown().\n"
        "The flag must be cleared only after all fallible teardown work, or a "
        "crash during shutdown is recorded as a clean exit and the session is "
        "not offered for recovery (#675). Fix the path resolution, not the "
        "ordering.")
endif()

# --- Property 3: detection reads the resolved path, after platform init ----
# A crashed session was previously never detected: isCrashedSession() ran
# before Platform::initialize(), so it looked in the working directory and
# missed the real flag entirely. Proven by launching with a flag in the working
# directory only — recovery fired.
string(FIND "${APP_TEXT}" "Platform::initialize()" PLATFORM_INIT_AT)
string(FIND "${APP_TEXT}" "isCrashedSession();" DETECT_AT)
if(PLATFORM_INIT_AT EQUAL -1 OR DETECT_AT EQUAL -1)
    message(FATAL_ERROR "Could not locate platform init / crash detection in the startup path")
endif()
if(DETECT_AT LESS PLATFORM_INIT_AT)
    message(FATAL_ERROR
        "Crash detection runs BEFORE Platform::initialize() again.\n"
        "getAppDataPath() falls back to the working directory when platform "
        "utilities are absent, so detection there reads the wrong file and no "
        "crash is ever detected (#675).")
endif()

# Scoped for the same reason as Property 2: the helper's own definition would
# otherwise satisfy a file-wide search.
string(FIND "${APP_TEXT}" "bool AestraApp::isCrashedSession()" DETECT_FN_AT)
if(DETECT_FN_AT EQUAL -1)
    message(FATAL_ERROR "Could not find AestraApp::isCrashedSession() to inspect")
endif()
string(SUBSTRING "${APP_TEXT}" ${DETECT_FN_AT} 400 DETECT_BODY)
string(FIND "${DETECT_BODY}" "\n}" DETECT_END)
if(NOT DETECT_END EQUAL -1)
    string(SUBSTRING "${DETECT_BODY}" 0 ${DETECT_END} DETECT_BODY)
endif()
string(FIND "${DETECT_BODY}" "activeCrashFlagPath()" READS_RESOLVED)
if(READS_RESOLVED EQUAL -1)
    message(FATAL_ERROR
        "isCrashedSession no longer reads through the resolved path helper (#675).")
endif()

message(STATUS "Verified crash-flag shutdown ordering, detection ordering and resolved-path use")
