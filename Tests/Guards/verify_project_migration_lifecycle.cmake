file(READ "${AESTRA_APP_SOURCE}" APP_SOURCE)

string(FIND "${APP_SOURCE}"
    "trackManager->setModified(result.requiresSaveAfterLoad());"
    MIGRATION_AWARE_CLEAR)
if(MIGRATION_AWARE_CLEAR EQUAL -1)
    message(FATAL_ERROR
        "AestraApp no longer propagates ProjectSerializer migration metadata to project modified state")
endif()

message(STATUS "Verified migration-aware project lifecycle propagation")
