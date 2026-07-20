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
if(TARGET AestraLicense)
    add_executable(SecLicenseGateSignature license_gate_signature_verification.cpp)
    add_test(NAME SecLicenseGateSignature COMMAND SecLicenseGateSignature)
    add_executable(SecLicenseWorkerFixture license_worker_fixture_verification.cpp)
    add_test(NAME SecLicenseWorkerFixture COMMAND SecLicenseWorkerFixture)
    add_executable(SecLicenseRefreshClient license_refresh_client_test.cpp)
    add_test(NAME SecLicenseRefreshClient COMMAND SecLicenseRefreshClient)
    add_executable(SecAccountApiClient account_api_client_test.cpp)
    add_test(NAME SecAccountApiClient COMMAND SecAccountApiClient)
    add_executable(SecAccountEndToEndSmoke account_end_to_end_smoke.cpp)
    add_test(NAME SecAccountEndToEndSmoke COMMAND SecAccountEndToEndSmoke)
    set_tests_properties(SecAccountEndToEndSmoke PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(SecAccountSessionCache account_session_cache_test.cpp)
    add_test(NAME SecAccountSessionCache COMMAND SecAccountSessionCache)
    add_executable(SecEntitlementStore entitlement_store_test.cpp)
    add_test(NAME SecEntitlementStore COMMAND SecEntitlementStore)
    add_executable(SecMembershipViewModel membership_view_model_test.cpp)
    add_test(NAME SecMembershipViewModel COMMAND SecMembershipViewModel)
endif()

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
find_file(AESTRA_CLAP_TEST_PLUGIN
    NAMES lsp-plugins.clap
    PATHS
        /usr/lib/clap
        /usr/local/lib/clap
        /opt/homebrew/lib/clap
        /opt/local/lib/clap
)
if(AESTRA_CLAP_TEST_PLUGIN)
    add_test(NAME SecPluginScanIsolation COMMAND SecPluginScanIsolation $<TARGET_FILE:AestraPluginHost> ${AESTRA_CLAP_TEST_PLUGIN})
else()
    message(STATUS "SecPluginScanIsolation: CLAP fixture not found at configure time; test will self-skip.")
    add_test(NAME SecPluginScanIsolation COMMAND SecPluginScanIsolation $<TARGET_FILE:AestraPluginHost> ${CMAKE_BINARY_DIR}/missing-lsp-plugins.clap)
endif()
add_test(NAME SecProjectLoadHardening COMMAND SecProjectLoadHardening)
set_tests_properties(SecPluginScanIsolation PROPERTIES SKIP_RETURN_CODE 77)

if(TARGET AestraAudio AND TARGET AestraCore AND TARGET AestraPlat)
    target_sources(SecStoulCrash PRIVATE ${REPO_ROOT}/Source/Core/ProjectSerializer.cpp)
    target_sources(SecDivZero PRIVATE ${REPO_ROOT}/Source/Core/ProjectSerializer.cpp)

    target_link_libraries(SecStoulCrash PRIVATE AestraAudio AestraPlat)
    target_link_libraries(SecDivZero PRIVATE AestraAudio AestraPlat)
    target_link_libraries(SecSamplerPath PRIVATE AestraAudio AestraPlat)
    target_link_libraries(SecShellEscape PRIVATE AestraPlat)
    target_link_libraries(SecJsonDos PRIVATE)

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
    target_link_libraries(SecJsonParserHardening PRIVATE)
    target_link_libraries(SecLicenseProfileHardening PRIVATE)

    target_include_directories(SecEnvVarValidation PRIVATE
        ${REPO_ROOT}/AestraCore/include
    )
    target_link_libraries(SecEnvVarValidation PRIVATE)

    target_include_directories(SecAutosaveRecoveryGuard PRIVATE
        ${REPO_ROOT}/Source/Core
        ${REPO_ROOT}/AestraCore/include
        ${REPO_ROOT}/AestraAudio/include
    )
    target_link_libraries(SecAutosaveRecoveryGuard PRIVATE)

    target_include_directories(SecFlacVendorlenBounds PRIVATE
        ${REPO_ROOT}/AestraCore/include
        ${REPO_ROOT}/AestraAudio/include
        ${REPO_ROOT}/AestraAudio/include/IO
    )
    target_link_libraries(SecFlacVendorlenBounds PRIVATE)

    target_include_directories(SecCacheMtimeIntegrity PRIVATE
        ${REPO_ROOT}/AestraCore/include
    )
    target_link_libraries(SecCacheMtimeIntegrity PRIVATE)

    target_include_directories(SecPluginTrustedPath PRIVATE
        ${REPO_ROOT}/AestraAudio/include
        ${REPO_ROOT}/AestraAudio/include/Plugin
        ${REPO_ROOT}/AestraCore/include
    )
    target_link_libraries(SecPluginTrustedPath PRIVATE AestraAudio)

    target_include_directories(SecOutOfProcessPluginHost PRIVATE
        ${REPO_ROOT}/AestraAudio/include
        ${REPO_ROOT}/AestraAudio/include/Plugin
        ${REPO_ROOT}/AestraCore/include
    )
    # Enables the OutOfProcessPluginInstance TESTNOTES accessor used by the #244
    # CLAP-note e2e checks (matches the AestraPluginHost test-hooks build).
    target_compile_definitions(SecOutOfProcessPluginHost PRIVATE AESTRA_ENABLE_TEST_HOOKS)
    target_link_libraries(SecOutOfProcessPluginHost PRIVATE AestraAudio)

    target_include_directories(SecPluginScanIsolation PRIVATE
        ${REPO_ROOT}/AestraAudio/include
        ${REPO_ROOT}/AestraAudio/include/Plugin
        ${REPO_ROOT}/AestraCore/include
    )
    target_link_libraries(SecPluginScanIsolation PRIVATE AestraAudio)

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
    target_link_libraries(SecProjectLoadHardening PRIVATE AestraAudio AestraPlat)
endif()

if(TARGET AestraLicense)
    target_include_directories(SecLicenseGateSignature PRIVATE
        ${REPO_ROOT}/AestraLicense/include
    )
    target_link_libraries(SecLicenseGateSignature PRIVATE AestraLicense)

    target_include_directories(SecLicenseWorkerFixture PRIVATE
        ${REPO_ROOT}/AestraLicense/include
    )
    target_link_libraries(SecLicenseWorkerFixture PRIVATE AestraLicense)

    target_include_directories(SecLicenseRefreshClient PRIVATE
        ${REPO_ROOT}/AestraLicense/include
    )
    target_link_libraries(SecLicenseRefreshClient PRIVATE AestraLicense)

    target_include_directories(SecAccountApiClient PRIVATE
        ${REPO_ROOT}/AestraLicense/include
    )
    target_link_libraries(SecAccountApiClient PRIVATE AestraLicense)

    target_include_directories(SecAccountEndToEndSmoke PRIVATE
        ${REPO_ROOT}/AestraLicense/include
        ${REPO_ROOT}/AestraCore/include
    )
    target_link_libraries(SecAccountEndToEndSmoke PRIVATE AestraLicense)

    target_include_directories(SecAccountSessionCache PRIVATE
        ${REPO_ROOT}/AestraLicense/include
    )
    target_link_libraries(SecAccountSessionCache PRIVATE AestraLicense)

    target_include_directories(SecEntitlementStore PRIVATE
        ${REPO_ROOT}/AestraLicense/include
    )
    target_link_libraries(SecEntitlementStore PRIVATE AestraLicense)

    target_include_directories(SecMembershipViewModel PRIVATE
        ${REPO_ROOT}/AestraLicense/include
    )
    target_link_libraries(SecMembershipViewModel PRIVATE AestraLicense)
endif()
