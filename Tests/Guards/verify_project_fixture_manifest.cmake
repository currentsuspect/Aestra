set(FIXTURES
    "v1_minimal.aes|54a5b9b77409183111bc24062e07957a58bb8181ece5fac25fea6383bb3144a0"
    "v1_rich.aes|e047afe200ac340f7e7d5f2dddb844c19807eb9c7d4a1324731b8f2d81fb95d9"
    "v1_rich_assets/kick.wav|9ccf1d5147ca6f0d9f12cdb392b04b6a9d1c99409ec7ec96333224cd1892da4e"
    "v2/serializer-v2-identity.aes|3f4f801643e4bf24920b343b72395039b2d83c3e6f6e8fc83f03993ba5721347"
    "v3/serializer-v3-independent-mixer.aes|8a5cf58ed79f994421464c163fc555fcf2516eb45c0dd0e4c58f53c83c272ab4"
)

foreach(ENTRY IN LISTS FIXTURES)
    string(REPLACE "|" ";" FIELDS "${ENTRY}")
    list(GET FIELDS 0 RELATIVE_PATH)
    list(GET FIELDS 1 EXPECTED_SHA256)
    set(FIXTURE_PATH "${FIXTURE_ROOT}/${RELATIVE_PATH}")

    if(NOT EXISTS "${FIXTURE_PATH}")
        message(FATAL_ERROR "Project-format fixture is missing: ${RELATIVE_PATH}")
    endif()

    file(SHA256 "${FIXTURE_PATH}" ACTUAL_SHA256)
    if(NOT ACTUAL_SHA256 STREQUAL EXPECTED_SHA256)
        message(FATAL_ERROR
            "Immutable project-format fixture changed: ${RELATIVE_PATH}\n"
            "Expected: ${EXPECTED_SHA256}\n"
            "Actual:   ${ACTUAL_SHA256}")
    endif()
endforeach()

message(STATUS "Verified immutable project-format fixture manifest")

