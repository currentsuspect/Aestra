# Immutability guard for released project-format fixtures.
#
# Run via `cmake -P`. Script mode starts with NO policies set, so anything that
# depends on one behaves differently across CMake versions — `if(x IN_LIST y)`
# needs CMP0057 and errors with "Unknown arguments specified" without it. This
# guard therefore uses `list(FIND)`, which is policy-independent, and states a
# minimum so a future edit that does reach for IN_LIST still works.
cmake_minimum_required(VERSION 3.16)
#
# The manifest is BIDIRECTIONAL on purpose. A one-way check (every manifest
# entry exists and hashes correctly) proves nothing about a fixture that was
# added, renamed or deleted-and-replaced: the corpus can change shape while
# every listed entry still matches. Both directions are therefore enforced:
#
#   manifest -> disk : every listed fixture exists with exactly the listed bytes
#   disk -> manifest : every discovered fixture is listed
#
# Together these fail on additions, deletions, renames, omissions and byte
# changes. A rename shows up as one missing entry plus one unlisted file; a
# deletion as a missing entry; an addition as an unlisted file.
#
# Adding a fixture is therefore a deliberate two-part act: check in the file and
# record its hash here. That is the intent — see
# Aestra-Internals: aestra-docs/architecture/project-format-compatibility.md.

# Files under the fixture root that are documentation rather than fixture
# evidence. Kept explicit: an unrecognised file must fail, not be guessed at.
set(NON_FIXTURE_FILES
    "README.md"
)

set(FIXTURES
    "v1_minimal.aes|54a5b9b77409183111bc24062e07957a58bb8181ece5fac25fea6383bb3144a0"
    "v1_rich.aes|e047afe200ac340f7e7d5f2dddb844c19807eb9c7d4a1324731b8f2d81fb95d9"
    "v1_rich_assets/kick.wav|9ccf1d5147ca6f0d9f12cdb392b04b6a9d1c99409ec7ec96333224cd1892da4e"
    "v2/serializer-v2-identity.aes|3f4f801643e4bf24920b343b72395039b2d83c3e6f6e8fc83f03993ba5721347"
    "v2/serializer-v2-positional-mixer.aes|56159ce3af222e2405595a44883503610670975ba1a29b6f6f0b584c8b6649a2"
    "v2/serializer-v2-legacy-audio-split.aes|d41550c593bc4a6f4a60d4dc61737b6c931af76101864f4b04414f0fcb28765f"
    "v2/legacy_audio_assets/shared.wav|856b60bb9680b5cc5bd5ea6925f38df292bf76e807813b51334ac30b74d85134"
    "v3/serializer-v3-independent-mixer.aes|8a5cf58ed79f994421464c163fc555fcf2516eb45c0dd0e4c58f53c83c272ab4"
)

if(NOT DEFINED FIXTURE_ROOT OR FIXTURE_ROOT STREQUAL "")
    message(FATAL_ERROR "FIXTURE_ROOT was not provided to the fixture manifest guard")
endif()

if(NOT IS_DIRECTORY "${FIXTURE_ROOT}")
    message(FATAL_ERROR "Project-format fixture root does not exist: ${FIXTURE_ROOT}")
endif()

# ---------------------------------------------------------------------------
# Direction 1: manifest -> disk
# ---------------------------------------------------------------------------
set(EXPECTED_PATHS "")
foreach(ENTRY IN LISTS FIXTURES)
    string(REPLACE "|" ";" FIELDS "${ENTRY}")
    list(GET FIELDS 0 RELATIVE_PATH)
    list(GET FIELDS 1 EXPECTED_SHA256)
    list(APPEND EXPECTED_PATHS "${RELATIVE_PATH}")
    set(FIXTURE_PATH "${FIXTURE_ROOT}/${RELATIVE_PATH}")

    if(NOT EXISTS "${FIXTURE_PATH}")
        message(FATAL_ERROR
            "Project-format fixture is missing: ${RELATIVE_PATH}\n"
            "Released fixtures are immutable evidence and may not be deleted or renamed.")
    endif()

    file(SHA256 "${FIXTURE_PATH}" ACTUAL_SHA256)
    if(NOT ACTUAL_SHA256 STREQUAL EXPECTED_SHA256)
        message(FATAL_ERROR
            "Immutable project-format fixture changed: ${RELATIVE_PATH}\n"
            "Expected: ${EXPECTED_SHA256}\n"
            "Actual:   ${ACTUAL_SHA256}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Direction 2: disk -> manifest
# ---------------------------------------------------------------------------
file(GLOB_RECURSE DISCOVERED
    RELATIVE "${FIXTURE_ROOT}"
    "${FIXTURE_ROOT}/*"
)

if(DISCOVERED STREQUAL "")
    message(FATAL_ERROR
        "Project-format fixture root contains no files: ${FIXTURE_ROOT}\n"
        "An empty corpus is a failure, not a pass — the guard would otherwise "
        "have nothing to verify.")
endif()

set(UNLISTED "")
foreach(FOUND IN LISTS DISCOVERED)
    if(IS_DIRECTORY "${FIXTURE_ROOT}/${FOUND}")
        continue()
    endif()
    list(FIND NON_FIXTURE_FILES "${FOUND}" NON_FIXTURE_INDEX)
    if(NOT NON_FIXTURE_INDEX EQUAL -1)
        continue()
    endif()
    list(FIND EXPECTED_PATHS "${FOUND}" EXPECTED_INDEX)
    if(EXPECTED_INDEX EQUAL -1)
        list(APPEND UNLISTED "${FOUND}")
    endif()
endforeach()

if(NOT UNLISTED STREQUAL "")
    string(REPLACE ";" "\n  " UNLISTED_TEXT "${UNLISTED}")
    message(FATAL_ERROR
        "Project-format fixture(s) present on disk but absent from the manifest:\n"
        "  ${UNLISTED_TEXT}\n"
        "Every fixture must be recorded with its SHA-256 in "
        "Tests/Guards/verify_project_fixture_manifest.cmake. If this is a rename, "
        "note that renaming a released fixture is not permitted.")
endif()

list(LENGTH FIXTURES FIXTURE_COUNT)
message(STATUS
    "Verified immutable project-format fixture manifest "
    "(${FIXTURE_COUNT} fixtures, manifest and directory agree)")
