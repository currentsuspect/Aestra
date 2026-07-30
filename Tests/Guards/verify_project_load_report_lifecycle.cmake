# get_project_load_report lifecycle guard.
#
# Source/ has no linkable application-layer target (#666), so the focused C++
# test proves the state holder and serializer mapping while this guard protects
# the actual New Project, normal load, and recovery call sites.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED AESTRA_SOURCE_ROOT OR AESTRA_SOURCE_ROOT STREQUAL "")
    message(FATAL_ERROR "AESTRA_SOURCE_ROOT was not provided to the project-load-report guard")
endif()

set(APP_SOURCE "${AESTRA_SOURCE_ROOT}/Source/App/AestraApp.cpp")
if(NOT EXISTS "${APP_SOURCE}")
    message(FATAL_ERROR "Guard cannot find the application source at ${APP_SOURCE}")
endif()

file(READ "${APP_SOURCE}" APP_TEXT)

# New Project must clear the report after replacing the session with a blank.
string(FIND "${APP_TEXT}" "menu->addItem(\"New Project\"" NEW_PROJECT_AT)
if(NEW_PROJECT_AT EQUAL -1)
    message(FATAL_ERROR "Could not find the New Project application path")
endif()
string(SUBSTRING "${APP_TEXT}" ${NEW_PROJECT_AT} 900 NEW_PROJECT_BODY)
string(FIND "${NEW_PROJECT_BODY}" "resetToDefaultProject();" NEW_RESET_AT)
string(FIND "${NEW_PROJECT_BODY}" "clearProjectLoadReport();" NEW_CLEAR_AT)
if(NEW_RESET_AT EQUAL -1 OR NEW_CLEAR_AT EQUAL -1 OR NEW_CLEAR_AT LESS NEW_RESET_AT)
    message(FATAL_ERROR
        "New Project no longer clears get_project_load_report after creating the blank session")
endif()

# Every canonical/snapshot load clears stale state before loading and publishes
# exactly the result it received before returning or applying the project.
string(FIND "${APP_TEXT}" "ProjectSerializer::LoadResult AestraApp::loadProjectFromPath" LOAD_FN_AT)
if(LOAD_FN_AT EQUAL -1)
    message(FATAL_ERROR "Could not find AestraApp::loadProjectFromPath()")
endif()
string(SUBSTRING "${APP_TEXT}" ${LOAD_FN_AT} 1800 LOAD_BODY)
string(FIND "${LOAD_BODY}" "beginProjectLoadAttempt();" LOAD_BEGIN_AT)
string(FIND "${LOAD_BODY}" "ProjectSerializer::load(" SERIALIZER_LOAD_AT)
if(LOAD_BEGIN_AT EQUAL -1 OR SERIALIZER_LOAD_AT EQUAL -1)
    message(FATAL_ERROR "Normal project load no longer clears and records get_project_load_report")
endif()
if(SERIALIZER_LOAD_AT LESS LOAD_BEGIN_AT)
    message(FATAL_ERROR "Normal project-load report lifecycle ordering regressed")
endif()
string(SUBSTRING "${LOAD_BODY}" ${SERIALIZER_LOAD_AT} -1 LOAD_AFTER_SERIALIZER)
string(FIND "${LOAD_AFTER_SERIALIZER}" "recordProjectLoadAttempt(result, source);" LOAD_RECORD_AT)
if(LOAD_RECORD_AT EQUAL -1)
    message(FATAL_ERROR "Normal project load no longer records the serializer result")
endif()

# Recovery bypasses loadProjectFromPath through loadFirstValid(), so it needs
# the same clear/record lifecycle at that separate call site.
string(FIND "${APP_TEXT}" "if (response == Aestra::RecoveryResponse::Recover)" RECOVERY_AT)
if(RECOVERY_AT EQUAL -1)
    message(FATAL_ERROR "Could not find the recovery load application path")
endif()
string(SUBSTRING "${APP_TEXT}" ${RECOVERY_AT} 1200 RECOVERY_BODY)
string(FIND "${RECOVERY_BODY}" "beginProjectLoadAttempt();" RECOVERY_BEGIN_AT)
string(FIND "${RECOVERY_BODY}" "ProjectSerializer::loadFirstValid(" RECOVERY_LOAD_AT)
string(FIND "${RECOVERY_BODY}"
    "recordProjectLoadAttempt(result, ProjectLoadSource::Recovery);"
    RECOVERY_RECORD_AT)
if(RECOVERY_BEGIN_AT EQUAL -1 OR RECOVERY_LOAD_AT EQUAL -1 OR RECOVERY_RECORD_AT EQUAL -1)
    message(FATAL_ERROR "Recovery load no longer clears and records get_project_load_report")
endif()
if(RECOVERY_LOAD_AT LESS RECOVERY_BEGIN_AT OR RECOVERY_RECORD_AT LESS RECOVERY_LOAD_AT)
    message(FATAL_ERROR "Recovery project-load report lifecycle ordering regressed")
endif()

message(STATUS "Verified project-load report replacement and New Project clearing")
