cmake_minimum_required(VERSION 3.22)

project(AestraLicense VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(AestraLicense STATIC
    src/LicenseGate.cpp
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
    find_library(LIBSODIUM_LIBRARY NAMES sodium libsodium HINTS ${LIBSODIUM_LIBRARY_DIRS})
    if(NOT LIBSODIUM_LIBRARY)
        message(FATAL_ERROR "AestraLicense requires libsodium on macOS for Ed25519 verification.")
    endif()

    target_include_directories(AestraLicense PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
    target_link_libraries(AestraLicense PRIVATE ${LIBSODIUM_LIBRARY} "-framework Security")
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
