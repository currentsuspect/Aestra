# The application source path is resolved HERE, not passed in from
# Tests/cmake/ProjectTests.cmake.
#
# scripts/ci/classify-changes.sh derives its headless-compiled deny-list by
# text-grepping CMakeLists.txt, Tests/CMakeLists.txt and Tests/cmake/*.cmake for
# `(Source|AestraUI)/....cpp`. A literal app-source path in one of those files
# is indistinguishable from a compiled source, so naming it there made the
# classifier believe AestraApp.cpp is built headlessly and forced every lane on
# every PR touching it. Failing broad is safe, but it silently defeats the
# narrowing — which is why the classifier self-test fails when it happens.
#
# Tests/Guards/ is not scanned, so resolving the path here keeps the guard
# honest without poisoning the derivation.
set(AESTRA_APP_SOURCE "${AESTRA_SOURCE_ROOT}/Source/App/AestraApp.cpp")

if(NOT EXISTS "${AESTRA_APP_SOURCE}")
    message(FATAL_ERROR "Guard cannot find the application source at ${AESTRA_APP_SOURCE}")
endif()

file(READ "${AESTRA_APP_SOURCE}" APP_SOURCE)

string(FIND "${APP_SOURCE}"
    "trackManager->setModified(result.requiresSaveAfterLoad());"
    MIGRATION_AWARE_CLEAR)
if(MIGRATION_AWARE_CLEAR EQUAL -1)
    message(FATAL_ERROR
        "AestraApp no longer propagates ProjectSerializer migration metadata to project modified state")
endif()

message(STATUS "Verified migration-aware project lifecycle propagation")
