cmake_minimum_required(VERSION 3.22)

project(AestraLicense VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(AestraLicense STATIC
    src/AccountApiClient.cpp
    src/AccountService.cpp
    src/AccountSession.cpp
    src/EntitlementStore.cpp
    src/HttpTransport.cpp
    src/LicenseGate.cpp
    src/LicenseRefreshClient.cpp
    src/LocalAccountCache.cpp
    src/MembershipViewModel.cpp
)

target_include_directories(AestraLicense
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/AestraCore/include
)

target_link_libraries(AestraLicense
    PUBLIC
        AestraCore
)

set(AESTRA_LICENSE_DEV_PUBLIC_KEY_HEX "bf30bfb9e66ff349bb96922b26e92fb860272adc2413f15b4052bc8b56800f58")
set(AESTRA_LICENSE_ENV_PUBLIC_KEY_HEX "$ENV{AESTRA_LICENSE_PUBLIC_KEY_HEX}")
set(AESTRA_LICENSE_EFFECTIVE_PUBLIC_KEY_HEX "${AESTRA_LICENSE_ENV_PUBLIC_KEY_HEX}")

if(AESTRA_LICENSE_ENABLE_PREMIUM_LEASES)
    if("${AESTRA_LICENSE_EFFECTIVE_PUBLIC_KEY_HEX}" STREQUAL "")
        message(FATAL_ERROR "AESTRA_LICENSE_ENABLE_PREMIUM_LEASES requires environment variable AESTRA_LICENSE_PUBLIC_KEY_HEX=<64 hex chars>.")
    endif()
else()
    if("${AESTRA_LICENSE_EFFECTIVE_PUBLIC_KEY_HEX}" STREQUAL "")
        set(AESTRA_LICENSE_EFFECTIVE_PUBLIC_KEY_HEX "${AESTRA_LICENSE_DEV_PUBLIC_KEY_HEX}")
    endif()
endif()

string(TOLOWER "${AESTRA_LICENSE_EFFECTIVE_PUBLIC_KEY_HEX}" AESTRA_LICENSE_PUBLIC_KEY_HEX_LOWER)
string(LENGTH "${AESTRA_LICENSE_PUBLIC_KEY_HEX_LOWER}" AESTRA_LICENSE_PUBLIC_KEY_HEX_LENGTH)
if(NOT AESTRA_LICENSE_PUBLIC_KEY_HEX_LENGTH EQUAL 64 OR
   NOT "${AESTRA_LICENSE_PUBLIC_KEY_HEX_LOWER}" MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "AESTRA_LICENSE_PUBLIC_KEY_HEX must contain exactly 64 hex characters.")
endif()
if(AESTRA_LICENSE_ENABLE_PREMIUM_LEASES AND
   AESTRA_LICENSE_PUBLIC_KEY_HEX_LOWER STREQUAL AESTRA_LICENSE_DEV_PUBLIC_KEY_HEX)
    message(FATAL_ERROR "Premium lease builds must not use the committed development license verification key.")
endif()

set(AESTRA_LICENSE_PUBLIC_KEY_BYTES "")
foreach(index RANGE 0 31)
    math(EXPR byte_offset "${index} * 2")
    string(SUBSTRING "${AESTRA_LICENSE_PUBLIC_KEY_HEX_LOWER}" ${byte_offset} 2 byte_hex)
    if(index GREATER 0)
        string(APPEND AESTRA_LICENSE_PUBLIC_KEY_BYTES ", ")
    endif()
    string(APPEND AESTRA_LICENSE_PUBLIC_KEY_BYTES "0x${byte_hex}")
endforeach()
if(AESTRA_LICENSE_PUBLIC_KEY_HEX_LOWER STREQUAL AESTRA_LICENSE_DEV_PUBLIC_KEY_HEX)
    set(AESTRA_LICENSE_PUBLIC_KEY_LABEL "AESTRA_DEV_TEST_PUBKEY_V3")
else()
    string(SUBSTRING "${AESTRA_LICENSE_PUBLIC_KEY_HEX_LOWER}" 0 8 _key_prefix)
    set(AESTRA_LICENSE_PUBLIC_KEY_LABEL "prod-${_key_prefix}")
endif()
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/AestraLicensePublicKey.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/AestraLicensePublicKey.h
    @ONLY
)
target_include_directories(AestraLicense PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

if(AESTRA_LICENSE_ENABLE_PREMIUM_LEASES)
    target_compile_definitions(AestraLicense PRIVATE AESTRA_LICENSE_ENABLE_PREMIUM_LEASES=1)
endif()

find_package(CURL QUIET)
if(CURL_FOUND)
    target_link_libraries(AestraLicense PRIVATE CURL::libcurl)
    target_compile_definitions(AestraLicense PRIVATE AESTRA_LICENSE_HAS_CURL=1)
else()
    message(STATUS "AestraLicense: libcurl not found; native account HTTP transport will report unavailable.")
endif()

if(WIN32)
    find_path(LIBSODIUM_INCLUDE_DIR sodium.h)
    find_library(LIBSODIUM_LIBRARY NAMES sodium libsodium)
    if(NOT LIBSODIUM_INCLUDE_DIR OR NOT LIBSODIUM_LIBRARY)
        message(FATAL_ERROR "AestraLicense requires libsodium on Windows for Ed25519 verification.")
    endif()

    target_include_directories(AestraLicense PRIVATE ${LIBSODIUM_INCLUDE_DIR})
    target_link_libraries(AestraLicense PRIVATE crypt32 ${LIBSODIUM_LIBRARY})
elseif(APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSODIUM REQUIRED libsodium)
    # CI on macOS reported linker failures with plain ${LIBSODIUM_LIBRARIES} ("-lsodium").
    # Resolve the concrete library path from pkg-config hints to keep linking reliable.
    find_library(LIBSODIUM_LIBRARY NAMES sodium libsodium HINTS ${LIBSODIUM_LIBRARY_DIRS})
    if(NOT LIBSODIUM_LIBRARY)
        message(FATAL_ERROR "AestraLicense requires libsodium on macOS for Ed25519 verification.")
    endif()

    target_include_directories(AestraLicense PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
    target_link_libraries(AestraLicense PRIVATE ${LIBSODIUM_LIBRARY})
    target_link_libraries(AestraLicense PUBLIC "-framework Security" "-framework CoreFoundation")
    target_compile_options(AestraLicense PRIVATE ${LIBSODIUM_CFLAGS_OTHER})
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSODIUM REQUIRED libsodium)
    pkg_check_modules(LIBSECRET REQUIRED libsecret-1)

    target_include_directories(AestraLicense PRIVATE
        ${LIBSODIUM_INCLUDE_DIRS}
        ${LIBSECRET_INCLUDE_DIRS}
    )
    target_link_directories(AestraLicense PRIVATE
        ${LIBSODIUM_LIBRARY_DIRS}
        ${LIBSECRET_LIBRARY_DIRS}
    )
    target_link_libraries(AestraLicense PRIVATE
        ${LIBSODIUM_LIBRARIES}
        ${LIBSECRET_LIBRARIES}
    )
    target_compile_options(AestraLicense PRIVATE
        ${LIBSODIUM_CFLAGS_OTHER}
        ${LIBSECRET_CFLAGS_OTHER}
    )
endif()
