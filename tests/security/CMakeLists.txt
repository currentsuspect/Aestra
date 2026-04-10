cmake_minimum_required(VERSION 3.22)
project(SecurityTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SecStoulCrash security/stoul_crash.cpp)
add_executable(SecJsonDos security/json_dos.cpp)
add_executable(SecDivZero security/div_zero_channels.cpp)
add_executable(SecSamplerPath security/sampler_path_traversal.cpp)
add_executable(SecId3Overflow security/id3_overflow.cpp)
add_executable(SecShellEscape security/shell_escape_test.cpp)

# Blue team fixes — proof of fix tests (S002)
add_executable(SecClipColorStoul security/clip_color_stoul.cpp)
add_executable(SecPluginCacheBounds security/plugin_cache_bounds.cpp)
add_executable(SecFreadTruncatedWav security/fread_truncated_wav.cpp)

add_test(NAME SecStoulCrash COMMAND SecStoulCrash)
add_test(NAME SecJsonDos COMMAND SecJsonDos)
add_test(NAME SecDivZero COMMAND SecDivZero)
add_test(NAME SecSamplerPath COMMAND SecSamplerPath)
add_test(NAME SecId3Overflow COMMAND SecId3Overflow)
add_test(NAME SecShellEscape COMMAND SecShellEscape)

# Blue team fix proofs
add_test(NAME SecClipColorStoul COMMAND SecClipColorStoul)
add_test(NAME SecPluginCacheBounds COMMAND SecPluginCacheBounds)
add_test(NAME SecFreadTruncatedWav COMMAND SecFreadTruncatedWav)

# Blue team fixes — proof of fix tests (S003)
add_executable(SecJsonParserHardening security/json_parser_hardening.cpp)
add_executable(SecEnvVarValidation security/env_var_validation.cpp)
add_executable(SecAutosaveRecoveryGuard security/autosave_recovery_guard.cpp)

add_test(NAME SecJsonParserHardening COMMAND SecJsonParserHardening)
add_test(NAME SecEnvVarValidation COMMAND SecEnvVarValidation)
add_test(NAME SecAutosaveRecoveryGuard COMMAND SecAutosaveRecoveryGuard)

# Blue team fixes — proof of fix tests (S004)
add_executable(SecFlacVendorlenBounds security/flac_vendorlen_bounds.cpp)
add_executable(SecCacheMtimeIntegrity security/cache_mtime_integrity.cpp)
add_executable(SecPluginTrustedPath security/plugin_trusted_path.cpp)

add_test(NAME SecFlacVendorlenBounds COMMAND SecFlacVendorlenBounds)
add_test(NAME SecCacheMtimeIntegrity COMMAND SecCacheMtimeIntegrity)
add_test(NAME SecPluginTrustedPath COMMAND SecPluginTrustedPath)
