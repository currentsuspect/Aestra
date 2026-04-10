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

add_test(NAME SecStoulCrash COMMAND SecStoulCrash)
add_test(NAME SecJsonDos COMMAND SecJsonDos)
add_test(NAME SecDivZero COMMAND SecDivZero)
add_test(NAME SecSamplerPath COMMAND SecSamplerPath)
add_test(NAME SecId3Overflow COMMAND SecId3Overflow)
add_test(NAME SecShellEscape COMMAND SecShellEscape)
