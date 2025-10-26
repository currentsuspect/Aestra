cmake_minimum_required(VERSION 3.22)

project(NomadAudio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# RtAudio Integration (MIT License)
# =============================================================================

# RtAudio source files
set(RTAUDIO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External/rtaudio)

set(RTAUDIO_SOURCES
    ${RTAUDIO_DIR}/RtAudio.cpp
)

set(RTAUDIO_HEADERS
    ${RTAUDIO_DIR}/RtAudio.h
)

# Platform-specific audio APIs
if(WIN32)
    # Windows: WASAPI only (stable, universally available)
    add_definitions(-D__WINDOWS_WASAPI__)
    set(RTAUDIO_LIBS dsound ole32 winmm ksuser mfplat mfuuid wmcodecdspuuid)

elseif(APPLE)
    # macOS: CoreAudio
    add_definitions(-D__MACOSX_CORE__)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    set(RTAUDIO_LIBS ${COREAUDIO_LIBRARY} ${COREFOUNDATION_LIBRARY})

elseif(UNIX)
    # Linux: ALSA (primary), JACK (optional), PulseAudio (optional)
    add_definitions(-D__LINUX_ALSA__)
    find_package(ALSA REQUIRED)
    set(RTAUDIO_LIBS ${ALSA_LIBRARIES} pthread)

    # Optional JACK support
    # add_definitions(-D__UNIX_JACK__)
    # find_package(Jack)
    # if(JACK_FOUND)
    #     set(RTAUDIO_LIBS ${RTAUDIO_LIBS} ${JACK_LIBRARIES})
    # endif()
endif()

# =============================================================================
# NomadAudio Library
# =============================================================================

set(NOMAD_AUDIO_SOURCES
    src/AudioDeviceManager.cpp
    src/RtAudioBackend.cpp
    src/AudioProcessor.cpp
    src/MixerBus.cpp
    src/Oscillator.cpp
    src/Track.cpp
    src/TrackManager.cpp
)

set(NOMAD_AUDIO_HEADERS
    include/NomadAudio.h
    include/AudioDeviceManager.h
    include/AudioDriver.h
    include/AudioProcessor.h
    include/MixerBus.h
    include/Oscillator.h
    include/Track.h
    include/TrackManager.h
)

# Create NomadAudio library
add_library(NomadAudio STATIC
    ${NOMAD_AUDIO_SOURCES}
    ${NOMAD_AUDIO_HEADERS}
    ${RTAUDIO_SOURCES}
    ${RTAUDIO_HEADERS}
)

target_include_directories(NomadAudio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${RTAUDIO_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/NomadCore/include
)

target_link_libraries(NomadAudio
    PUBLIC
        NomadCore
    PRIVATE
        ${RTAUDIO_LIBS}
)

# =============================================================================
# Test Applications
# =============================================================================

# Basic audio test
add_executable(NomadAudioTest
    test/AudioTest.cpp
)

target_link_libraries(NomadAudioTest
    PRIVATE
        NomadAudio
        NomadCore
)

# Device manager test
add_executable(NomadDeviceManagerTest
    test/DeviceManagerTest.cpp
)

target_link_libraries(NomadDeviceManagerTest
    PRIVATE
        NomadAudio
        NomadCore
)

# Audio callback test
add_executable(NomadAudioCallbackTest
    test/AudioCallbackTest.cpp
)

target_link_libraries(NomadAudioCallbackTest
    PRIVATE
        NomadAudio
        NomadCore
)

# Mixer bus test
add_executable(NomadMixerBusTest
    test/MixerBusTest.cpp
)

target_link_libraries(NomadMixerBusTest
    PRIVATE
        NomadAudio
        NomadCore
)

# Oscillator test
add_executable(NomadOscillatorTest
    test/OscillatorTest.cpp
)

target_link_libraries(NomadOscillatorTest
    PRIVATE
        NomadAudio
        NomadCore
)

# =============================================================================
# Status
# =============================================================================

message(STATUS "")
message(STATUS "=================================")
message(STATUS "  NomadAudio v${PROJECT_VERSION}")
message(STATUS "=================================")
message(STATUS "RtAudio: Integrated (MIT)")
if(WIN32)
    message(STATUS "Audio API: WASAPI")
    message(STATUS "Backend: Windows Audio Session API")
elseif(APPLE)
    message(STATUS "Audio API: CoreAudio")
elseif(UNIX)
    message(STATUS "Audio API: ALSA")
endif()
message(STATUS "=================================")
message(STATUS "")
