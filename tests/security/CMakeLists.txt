cmake_minimum_required(VERSION 3.22)
project(SecurityTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

enable_testing()

add_executable(SecStoulCrash stoul_crash.cpp)
add_executable(SecJsonDos json_dos.cpp)
add_executable(SecDivZero div_zero_channels.cpp)
add_executable(SecSamplerPath sampler_path_traversal.cpp)
add_executable(SecId3Overflow id3_overflow.cpp)
add_executable(SecShellEscape shell_escape_test.cpp)
add_executable(SecLicenseProfileHardening
    license_profile_hardening.cpp
    ${REPO_ROOT}/Source/license/LicenseVerifier.cpp
)
add_executable(SecLicenseGateSignature license_gate_signature_verification.cpp)

# Blue team fixes — proof of fix tests (S002)
add_executable(SecClipColorStoul clip_color_stoul.cpp)
add_executable(SecPluginCacheBounds plugin_cache_bounds.cpp)
add_executable(SecFreadTruncatedWav fread_truncated_wav.cpp)

add_test(NAME SecStoulCrash COMMAND SecStoulCrash)
add_test(NAME SecJsonDos COMMAND SecJsonDos)
add_test(NAME SecDivZero COMMAND SecDivZero)
add_test(NAME SecSamplerPath COMMAND SecSamplerPath)
add_test(NAME SecId3Overflow COMMAND SecId3Overflow)
add_test(NAME SecShellEscape COMMAND SecShellEscape)
add_test(NAME SecLicenseProfileHardening COMMAND SecLicenseProfileHardening)
add_test(NAME SecLicenseGateSignature COMMAND SecLicenseGateSignature)

# Blue team fix proofs
add_test(NAME SecClipColorStoul COMMAND SecClipColorStoul)
add_test(NAME SecPluginCacheBounds COMMAND SecPluginCacheBounds)
add_test(NAME SecFreadTruncatedWav COMMAND SecFreadTruncatedWav)

# Blue team fixes — proof of fix tests (S003)
add_executable(SecJsonParserHardening json_parser_hardening.cpp)
add_executable(SecEnvVarValidation env_var_validation.cpp)
add_executable(SecAutosaveRecoveryGuard autosave_recovery_guard.cpp)

add_test(NAME SecJsonParserHardening COMMAND SecJsonParserHardening)
add_test(NAME SecEnvVarValidation COMMAND SecEnvVarValidation)
add_test(NAME SecAutosaveRecoveryGuard COMMAND SecAutosaveRecoveryGuard)

# Blue team fixes — proof of fix tests (S004)
add_executable(SecFlacVendorlenBounds flac_vendorlen_bounds.cpp)
add_executable(SecCacheMtimeIntegrity cache_mtime_integrity.cpp)
add_executable(SecPluginTrustedPath plugin_trusted_path.cpp)
add_executable(SecOutOfProcessPluginHost out_of_process_plugin_host.cpp)
add_executable(SecPluginScanIsolation plugin_scan_isolation.cpp)
add_executable(SecProjectLoadHardening project_load_hardening.cpp)

add_test(NAME SecFlacVendorlenBounds COMMAND SecFlacVendorlenBounds)
add_test(NAME SecCacheMtimeIntegrity COMMAND SecCacheMtimeIntegrity)
add_test(NAME SecPluginTrustedPath COMMAND SecPluginTrustedPath)
add_test(NAME SecOutOfProcessPluginHost COMMAND SecOutOfProcessPluginHost $<TARGET_FILE:AestraPluginHost>)
add_test(NAME SecPluginScanIsolation COMMAND SecPluginScanIsolation $<TARGET_FILE:AestraPluginHost> /usr/lib/clap/lsp-plugins.clap)
add_test(NAME SecProjectLoadHardening COMMAND SecProjectLoadHardening)

if(TARGET AestraAudio AND TARGET AestraCore AND TARGET AestraPlat)
    target_sources(SecStoulCrash PRIVATE ${REPO_ROOT}/Source/Core/ProjectSerializer.cpp)
    target_sources(SecDivZero PRIVATE ${REPO_ROOT}/Source/Core/ProjectSerializer.cpp)

    target_link_libraries(SecStoulCrash PRIVATE AestraAudio AestraCore AestraPlat)
    target_link_libraries(SecDivZero PRIVATE AestraAudio AestraCore AestraPlat)
    target_link_libraries(SecSamplerPath PRIVATE AestraAudio AestraCore AestraPlat)
    target_link_libraries(SecShellEscape PRIVATE AestraPlat AestraCore)
    target_link_libraries(SecJsonDos PRIVATE AestraCore)

    target_include_directories(SecStoulCrash PRIVATE
        ${REPO_ROOT}/Source
        ${REPO_ROOT}/Source/Core
        ${REPO_ROOT}/AestraAudio/include
        ${REPO_ROOT}/AestraAudio/include/Core
        ${REPO_ROOT}/AestraAudio/include/Models
        ${REPO_ROOT}/AestraAudio/include/IO
        ${REPO_ROOT}/AestraCore/include
        ${REPO_ROOT}/AestraPlat/include
    )
    target_include_directories(SecDivZero PRIVATE
        ${REPO_ROOT}/Source
        ${REPO_ROOT}/Source/Core
        ${REPO_ROOT}/AestraAudio/include
        ${REPO_ROOT}/AestraAudio/include/Core
        ${REPO_ROOT}/AestraAudio/include/Models
        ${REPO_ROOT}/AestraAudio/include/IO
        ${REPO_ROOT}/AestraCore/include
        ${REPO_ROOT}/AestraPlat/include
    )
    target_include_directories(SecSamplerPath PRIVATE
        ${REPO_ROOT}/AestraAudio/include
        ${REPO_ROOT}/AestraAudio/include/Plugin
        ${REPO_ROOT}/AestraAudio/include/IO
        ${REPO_ROOT}/AestraCore/include
        ${REPO_ROOT}/AestraPlat/include
    )
    target_include_directories(SecShellEscape PRIVATE
        ${REPO_ROOT}/AestraCore/include
        ${REPO_ROOT}/AestraPlat/include
        ${REPO_ROOT}/AestraPlat/src
    )
    target_include_directories(SecJsonDos PRIVATE
        ${REPO_ROOT}/AestraCore/include
    )
    target_include_directories(SecLicenseProfileHardening PRIVATE
        ${REPO_ROOT}/Source
        ${REPO_ROOT}/Source/license
        ${REPO_ROOT}/AestraCore/include
    )

    target_include_directories(SecJsonParserHardening PRIVATE
        ${REPO_ROOT}/Source/Core
        ${REPO_ROOT}/AestraCore/include
    )
    target_link_libraries(SecJsonParserHardening PRIVATE AestraCore)
    target_link_libraries(SecLicenseProfileHardening PRIVATE AestraCore)

    target_include_directories(SecEnvVarValidation PRIVATE
        ${REPO_ROOT}/AestraCore/include
    )
    target_link_libraries(SecEnvVarValidation PRIVATE AestraCore)

    target_include_directories(SecAutosaveRecoveryGuard PRIVATE
        ${REPO_ROOT}/Source/Core
        ${REPO_ROOT}/AestraCore/include
        ${REPO_ROOT}/AestraAudio/include
    )
    target_link_libraries(SecAutosaveRecoveryGuard PRIVATE AestraCore)

    target_include_directories(SecFlacVendorlenBounds PRIVATE
        ${REPO_ROOT}/AestraCore/include
        ${REPO_ROOT}/AestraAudio/include
        ${REPO_ROOT}/AestraAudio/include/IO
    )
    target_link_libraries(SecFlacVendorlenBounds PRIVATE AestraCore)

    target_include_directories(SecCacheMtimeIntegrity PRIVATE
        ${REPO_ROOT}/AestraCore/include
    )
    target_link_libraries(SecCacheMtimeIntegrity PRIVATE AestraCore)

    target_include_directories(SecPluginTrustedPath PRIVATE
        ${REPO_ROOT}/AestraAudio/include
        ${REPO_ROOT}/AestraAudio/include/Plugin
        ${REPO_ROOT}/AestraCore/include
    )
    target_link_libraries(SecPluginTrustedPath PRIVATE AestraAudio AestraCore)

    target_include_directories(SecOutOfProcessPluginHost PRIVATE
        ${REPO_ROOT}/AestraAudio/include
        ${REPO_ROOT}/AestraAudio/include/Plugin
        ${REPO_ROOT}/AestraCore/include
    )
    target_link_libraries(SecOutOfProcessPluginHost PRIVATE AestraAudio AestraCore)

    target_include_directories(SecPluginScanIsolation PRIVATE
        ${REPO_ROOT}/AestraAudio/include
        ${REPO_ROOT}/AestraAudio/include/Plugin
        ${REPO_ROOT}/AestraCore/include
    )
    target_link_libraries(SecPluginScanIsolation PRIVATE AestraAudio AestraCore)

    target_sources(SecProjectLoadHardening PRIVATE ${REPO_ROOT}/Source/Core/ProjectSerializer.cpp)
    target_include_directories(SecProjectLoadHardening PRIVATE
        ${REPO_ROOT}/Source
        ${REPO_ROOT}/Source/Core
        ${REPO_ROOT}/AestraAudio/include
        ${REPO_ROOT}/AestraAudio/include/Core
        ${REPO_ROOT}/AestraAudio/include/Models
        ${REPO_ROOT}/AestraAudio/include/IO
        ${REPO_ROOT}/AestraCore/include
        ${REPO_ROOT}/AestraPlat/include
    )
    target_link_libraries(SecProjectLoadHardening PRIVATE AestraAudio AestraCore AestraPlat)
endif()

if(TARGET AestraLicense)
    target_include_directories(SecLicenseGateSignature PRIVATE
        ${REPO_ROOT}/AestraLicense/include
    )
    target_link_libraries(SecLicenseGateSignature PRIVATE AestraLicense)
endif()
