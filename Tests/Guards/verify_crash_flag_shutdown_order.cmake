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
string(FIND "${APP_TEXT}" "CrashFlagPath::isPrimed()" USES_PRIMED_CHECK)
string(FIND "${APP_TEXT}" "CrashFlagPath::get()" USES_RETAINED_PATH)
if(USES_PRIMED_CHECK EQUAL -1 OR USES_RETAINED_PATH EQUAL -1)
    message(FATAL_ERROR
        "clearCrashFlag no longer uses the retained crash-flag path.\n"
        "Resolving it during shutdown goes through getAppDataPath(), which "
        "falls back to the working directory once Platform::shutdown() has "
        "released the platform utilities — the flag is then never cleared and "
        "every launch offers a spurious recovery (#675).")
endif()

string(FIND "${APP_TEXT}" "CrashFlagPath::prime(" PRIMES_PATH)
if(PRIMES_PATH EQUAL -1)
    message(FATAL_ERROR
        "Nothing primes the crash-flag path while platform utilities are alive (#675).")
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

string(FIND "${APP_TEXT}" "activeCrashFlagPath()" READS_RESOLVED)
if(READS_RESOLVED EQUAL -1)
    message(FATAL_ERROR
        "Crash-flag readers no longer share the resolved path helper (#675).")
endif()

message(STATUS "Verified crash-flag shutdown ordering, detection ordering and resolved-path use")
