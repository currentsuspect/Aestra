cmake_minimum_required(VERSION 3.22)

project(AestraRumble LANGUAGES CXX)

add_library(AestraRumble STATIC
    src/RumbleInstance.cpp
    src/RumblePluginRegistration.cpp
    src/RumbleVariation.cpp
    src/RumbleVoice.cpp
    include/RumbleInstance.h
    include/RumbleVariation.h
    include/RumbleVoice.h
    include/RumblePresetBank.h
    include/RumblePluginRegistration.h
)

target_include_directories(AestraRumble
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(AestraRumble
    PUBLIC
        AestraPluginAPI
        AestraLicense
)

target_compile_features(AestraRumble PUBLIC cxx_std_17)
