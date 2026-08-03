cmake_minimum_required(VERSION 3.22)

project(AestraAudio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prevent Windows min/max macros from conflicting with std::min/std::max
if(WIN32)
    add_definitions(-DNOMINMAX)
endif()

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
    set(RTAUDIO_LIBS dsound ole32 winmm ksuser mfplat mfuuid wmcodecdspuuid mfreadwrite)

elseif(APPLE)
    # macOS: CoreAudio
    add_definitions(-D__MACOSX_CORE__)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    set(RTAUDIO_LIBS ${COREAUDIO_LIBRARY} ${COREFOUNDATION_LIBRARY})

elseif(UNIX)
    # Linux: ALSA (primary), JACK (optional), PulseAudio (optional)
    if(RTAUDIO_API_ALSA)
        add_definitions(-D__LINUX_ALSA__)
    endif()
    find_package(ALSA QUIET)
    find_package(PkgConfig QUIET)

    if(ALSA_FOUND)
        set(RTAUDIO_LIBS ${ALSA_LIBRARIES} pthread)
    else()
        message(WARNING "ALSA not found. Audio backend will not function.")
    endif()

    if(RTAUDIO_API_PULSE AND PkgConfig_FOUND)
        pkg_check_modules(PULSE libpulse-simple IMPORTED_TARGET)
        if(PULSE_FOUND)
            add_definitions(-D__LINUX_PULSE__)
            list(APPEND RTAUDIO_LIBS PkgConfig::PULSE)
        endif()
    endif()

    if(RTAUDIO_API_JACK AND PkgConfig_FOUND)
        pkg_check_modules(JACK jack IMPORTED_TARGET)
        if(JACK_FOUND)
            add_definitions(-D__UNIX_JACK__)
            list(APPEND RTAUDIO_LIBS PkgConfig::JACK)
        endif()
    endif()
endif()

# =============================================================================
# RtMidi Integration (MIT-style License) — hardware MIDI input
# =============================================================================
# Vendored like RtAudio (same author/license family, owner-approved 2026-07-09).
# Compiled ONLY into the platform backend targets (AGENTS.md §8), never into
# AestraAudioCore — core/headless builds get the MidiInputService stub instead.

set(RTMIDI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External/rtmidi)
set(RTMIDI_SOURCES ${RTMIDI_DIR}/RtMidi.cpp)
set(RTMIDI_HEADERS ${RTMIDI_DIR}/RtMidi.h)

# Platform APIs: reuse the audio backend decisions. On Linux, RtMidi shares
# RtAudio's __LINUX_ALSA__ define (already set above) and the ALSA libs. On
# Windows it needs Windows Multimedia (__WINDOWS_MM__; winmm already in
# RTAUDIO_LIBS). On macOS it would use __MACOSX_CORE__ (backend absent, #267).
if(WIN32)
    add_definitions(-D__WINDOWS_MM__)
endif()

# =============================================================================
# VST3 SDK Integration (MIT License)
# =============================================================================

set(VST3_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External/vst3sdk)

if(EXISTS ${VST3_SDK_DIR}/pluginterfaces AND EXISTS ${VST3_SDK_DIR}/public.sdk)
    set(AESTRA_HAS_VST3 ON)
    message(STATUS "VST3 SDK: Found at ${VST3_SDK_DIR}")
    
    # VST3 SDK include directories
    set(VST3_INCLUDE_DIRS
        ${VST3_SDK_DIR}
        ${VST3_SDK_DIR}/pluginterfaces
        ${VST3_SDK_DIR}/public.sdk
        ${VST3_SDK_DIR}/public.sdk/source
        ${VST3_SDK_DIR}/base
    )
    
    # VST3 hosting sources (minimal set for plugin hosting)
    set(VST3_HOST_SOURCES
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/hostclasses.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
        ${VST3_SDK_DIR}/public.sdk/source/common/memorystream.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/utility/stringconvert.cpp
        ${VST3_SDK_DIR}/public.sdk/source/common/commonstringconvert.cpp
        ${VST3_SDK_DIR}/public.sdk/source/vst/vstinitiids.cpp
        ${VST3_SDK_DIR}/base/source/fdebug.cpp
        ${VST3_SDK_DIR}/base/source/fobject.cpp
        ${VST3_SDK_DIR}/base/source/fstring.cpp
        ${VST3_SDK_DIR}/pluginterfaces/base/funknown.cpp
        ${VST3_SDK_DIR}/pluginterfaces/base/ustring.cpp
        ${VST3_SDK_DIR}/pluginterfaces/base/coreiids.cpp
    )
    
    if(WIN32)
        list(APPEND VST3_HOST_SOURCES
            ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_win32.cpp
        )
    elseif(APPLE)
        list(APPEND VST3_HOST_SOURCES
            ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm
        )
        set_source_files_properties(
            ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_mac.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc"
        )
    else()
        list(APPEND VST3_HOST_SOURCES
            ${VST3_SDK_DIR}/public.sdk/source/vst/hosting/module_linux.cpp
        )
    endif()
else()
    set(AESTRA_HAS_VST3 OFF)
    message(STATUS "VST3 SDK: Not found - plugin hosting disabled")
endif()

# =============================================================================
# CLAP SDK Integration (MIT License)
# =============================================================================

set(CLAP_SDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/External/clap)

if(EXISTS ${CLAP_SDK_DIR}/include/clap/clap.h)
    set(AESTRA_HAS_CLAP ON)
    message(STATUS "CLAP SDK: Found at ${CLAP_SDK_DIR}")
    
    # CLAP SDK is header-only, just need include path
    set(CLAP_INCLUDE_DIRS
        ${CLAP_SDK_DIR}/include
    )
else()
    set(AESTRA_HAS_CLAP OFF)
    message(STATUS "CLAP SDK: Not found")
endif()

# =============================================================================
# AestraAudio Library
# =============================================================================

# =============================================================================
# AestraAudioCore (Platform-Agnostic)
# =============================================================================

# Core sources (NO platform-specific code)
# Organized into subdirectories for maintainability
set(AESTRA_AUDIO_CORE_SOURCES
    # Core
    src/Core/AudioEngine.cpp
    src/Core/ClipRenderKernel.cpp
    src/Core/AudioDeviceManager.cpp
    src/Core/AutosaveManager.cpp
    src/Core/ArsenalProcessingContext.cpp
    src/Core/LatencyTopology.cpp
    src/Core/MixerBus.cpp
    src/Core/MixerChannel.cpp
    src/Core/EngineSupervisor.cpp
    src/Core/AsyncCleanupManager.cpp
    src/Core/RTGuard.cpp

    # Playback
    src/Playback/AuditionEngine.cpp
    src/Playback/PreviewEngine.cpp
    src/Playback/PatternPlaybackEngine.cpp
    src/Playback/MetronomeEngine.cpp
    src/Playback/TimelineClock.cpp
    
    # Models
    src/Models/TrackManager.cpp
    src/Models/ClipPrefilterService.cpp
    src/Models/PlaylistModel.cpp
    src/Models/UnitManager.cpp
    src/Models/PatternManager.cpp
    src/Models/ClipSource.cpp
    src/Models/ClipRenderService.cpp
    src/Models/AudioClip.cpp

    # DSP
    src/DSP/AudioProcessor.cpp
    src/DSP/Filter.cpp
    src/DSP/Oscillator.cpp
    src/DSP/ReverbSIMD.cpp
    src/DSP/SincAVX512.cpp
    src/DSP/SampleRateConverter.cpp
    src/DSP/ClipPrefilter.cpp
    src/DSP/TruePeakMeter.cpp

    # IO
    src/IO/AudioExporter.cpp
    src/IO/MiniAudioDecoder.cpp
    src/IO/StreamingDecoder.cpp
    src/IO/WaveformCache.cpp
    src/IO/PathUtils.cpp
    src/IO/MetadataParser.cpp
    
    # Drivers
    src/Drivers/DummyAudioDriver.cpp
    
    # Other (root-level for now)
    src/AudioGraphBuilder.cpp
    src/ChannelSlotMap.cpp
    src/Core/PlaybackGraphController.cpp
    src/SamplePool.cpp
    src/AudioRenderer.cpp
    
    # Commands
    src/Commands/CommandHistory.cpp
    src/Commands/CommandTransaction.cpp
    src/Commands/SplitClipCommand.cpp
    src/Commands/RenderAudioClipCommand.cpp
    src/Commands/ImportAudioClipCommand.cpp
    src/Commands/AddClipCommand.cpp
    src/Commands/CreateLaneCommand.cpp
    src/Commands/AddNoteCommand.cpp
    src/Commands/RemoveNoteCommand.cpp
    src/Commands/MoveNoteCommand.cpp
    src/Commands/ResizeNoteCommand.cpp
    src/Commands/UpdateNoteCommand.cpp
    src/Commands/NoteDiff.cpp
    src/Commands/MuseGrammar.cpp
    src/Commands/CommandParser.cpp
    src/Commands/CommandRegistry.cpp
    src/Commands/SessionLog.cpp
    src/Commands/MuseStubs.cpp
    src/Commands/HostVerbRegistry.cpp
    src/Commands/MuseService.cpp
    src/Commands/MuseSocketServer.cpp
    
    # Headless / batch processing
    src/Headless/HeadlessMusicGenerator.cpp
    
    # Plugin hosting system
    src/Plugin/PluginScanner.cpp
    src/Plugin/PluginManager.cpp
    src/Plugin/PluginFactory.cpp
    src/Plugin/OutOfProcessPluginInstance.cpp
    src/Plugin/InternalPluginRegistry.cpp
    src/Plugin/BuiltInPlugins.cpp
    src/Plugin/SamplerPlugin.cpp
    src/Plugin/EffectChain.cpp
    src/CommandManager.cpp
)

# Driver registry wiring:
# - Linux with ALSA builds the real registry in AestraAudioLinux.
# - Other builds keep the weak fallback in core so the dummy driver is available.
if(NOT (UNIX AND ALSA_FOUND))
    list(APPEND AESTRA_AUDIO_CORE_SOURCES
        src/Core/DriverRegistryStub.cpp
    )
endif()

# Set per-file compile flags for AVX-512 (x86_64 only, not ARM64)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686")
    if(MSVC)
        set_source_files_properties(src/DSP/SincAVX512.cpp PROPERTIES COMPILE_FLAGS "/arch:AVX512")
    else()
        set_source_files_properties(src/DSP/SincAVX512.cpp PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512dq")
    endif()
else()
    message(STATUS "Skipping AVX-512 optimizations for ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# Add VST3 host sources if SDK is available
if(AESTRA_HAS_VST3)
    list(APPEND AESTRA_AUDIO_CORE_SOURCES
        src/Plugin/VST3Host.cpp
        ${VST3_HOST_SOURCES}
    )
endif()

# Add CLAP host sources if SDK is available
if(AESTRA_HAS_CLAP)
    list(APPEND AESTRA_AUDIO_CORE_SOURCES
        src/Plugin/CLAPHost.cpp
    )
endif()


# Core headers
set(AESTRA_AUDIO_CORE_HEADERS
    include/Core/AudioGraphBuilder.h
    include/Core/AudioEngine.h
    include/Core/LatencyTopology.h
    include/Core/PlaybackGraphController.h
    include/Core/ArsenalProcessingContext.h
    include/Core/MasterSafetyLimiter.h
    include/Core/AudioCommandQueue.h
    include/Core/AudioTelemetry.h
    include/Core/AudioGraph.h
    include/Core/ChannelSlotMap.h
    include/Core/EngineState.h
    include/Core/AestraAudio.h
    include/Core/AudioDeviceManager.h
    include/Core/AutosaveManager.h
    include/Core/AudioEngineDiagnostics.h
    include/Core/EngineSupervisor.h
    include/Core/AsyncCleanupManager.h
    include/Core/RTGuard.h
    include/Drivers/IAudioDriver.h
    include/Drivers/AudioDriver.h           # Legacy/Base types
    include/Drivers/AudioDriverTypes.h
    include/Drivers/AudioDriverRegistry.h   # Registry interface
    include/DSP/AudioProcessor.h
    include/Models/MeterSnapshot.h
    include/Core/MixerBus.h
    include/DSP/Oscillator.h
    include/Playback/MetronomeEngine.h
    include/IO/PathUtils.h
    include/IO/AudioExportQuantization.h
    include/IO/AudioExporter.h
    include/Playback/PreviewEngine.h
    include/IO/SamplePool.h
    include/DSP/SampleRateConverter.h
    include/DSP/TruePeakMeter.h
    include/Core/MixerChannel.h
    include/Models/TrackManager.h
    include/Models/PatternSource.h
    include/Models/ClipInstance.h
    include/Models/PatternManager.h
    include/IO/MiniAudioDecoder.h
    # Multi-clip playlist system
    include/Models/TimeTypes.h
    include/Models/ClipSource.h
    include/Models/PlaylistModel.h
    include/Models/PlaylistRuntimeSnapshot.h
    include/Models/PlaylistMixer.h
    include/IO/WaveformCache.h
    include/Models/UnitManager.h
    include/Music/ScaleContext.h

    include/IO/StreamingDecoder.h
    # Undo/Redo
    include/Commands/ICommand.h
    include/Commands/CommandHistory.h
    include/Commands/CommandTransaction.h
    include/Commands/SplitClipCommand.h
    include/Commands/AddClipCommand.h
    include/Commands/AddChannelCommand.h
    include/Commands/CreateLaneCommand.h
    include/Commands/DuplicateClipCommand.h
    include/Commands/MoveClipCommand.h
    include/Commands/RemoveClipCommand.h
    include/Commands/TrimClipCommand.h
    include/Commands/MacroCommand.h
    include/Commands/SetVolumeCommand.h
    include/Commands/SetPanCommand.h
    include/Commands/SetMuteCommand.h
    include/Commands/SetSoloCommand.h
    include/Commands/AddNoteCommand.h
    include/Commands/RemoveNoteCommand.h
    include/Commands/MoveNoteCommand.h
    include/Commands/ResizeNoteCommand.h
    include/Commands/NoteDiff.h
    include/Commands/MuseGrammar.h
    include/Commands/CommandResult.h
    include/Commands/CommandParser.h
    include/Commands/CommandRegistry.h
    include/Commands/SessionLog.h
    include/Commands/MuseStubs.h

    # Headless / batch processing
    include/Headless/HeadlessMusicGenerator.h

    src/DummyAudioDriver.h
    # Plugin hosting system
    include/Plugin/PluginHost.h
    include/Plugin/PluginScanner.h
    include/Plugin/PluginManager.h
    include/Plugin/PluginFactory.h
    include/Plugin/OutOfProcessPluginInstance.h
    include/Plugin/InternalPluginRegistry.h
    include/Plugin/BuiltInPlugins.h
    include/Plugin/EffectChain.h
    include/CommandManager.h
    include/Plugin/VST3Host.h
    include/Plugin/AestraEQ.h
    include/Plugin/AestraComp.h
    include/Plugin/AestraVerb.h
    include/Plugin/AestraDelay.h
    include/Playback/AuditionEngine.h
)

# Create AestraAudioCore library
add_library(AestraAudioCore STATIC
    ${AESTRA_AUDIO_CORE_SOURCES}
    ${AESTRA_AUDIO_CORE_HEADERS}
)

# Apply NDEBUG only to VST3 SDK files (required by fdebug.h)
# Do NOT apply to entire target — preserves assertions in Aestra audio code
if(AESTRA_HAS_VST3)
    set_source_files_properties(
        ${VST3_HOST_SOURCES}
        PROPERTIES COMPILE_DEFINITIONS NDEBUG
    )
    # Suppress VST3 SDK warnings (external dependency, not under our control)
    # Use APPEND_STRING to avoid clobbering -fobjc-arc on module_mac.mm
    if(NOT MSVC)
        set_source_files_properties(
            ${VST3_HOST_SOURCES}
            APPEND_STRING PROPERTY COMPILE_FLAGS " -Wno-class-memaccess -Wno-deprecated-declarations -Wno-format -Wno-extra"
        )
    endif()
endif()

# Enable miniaudio for fast audio decoding (MP3, FLAC, etc.)
set(AESTRA_USE_MINIAUDIO ON)

# Apply miniaudio compile definition
if (AESTRA_USE_MINIAUDIO)
    target_compile_definitions(AestraAudioCore PRIVATE AESTRA_USE_MINIAUDIO)
endif()

# AestraVerb stage profiling (lab/benchmark only). Zero overhead when off.
if (AESTRA_REVERB_PROFILE)
    target_compile_definitions(AestraAudioCore PUBLIC AESTRA_REVERB_PROFILE)
endif()

# AestraVerb clamp diagnostics (lab only). Zero overhead when off.
if (AESTRA_REVERB_DIAGNOSTICS)
    target_compile_definitions(AestraAudioCore PUBLIC AESTRA_REVERB_DIAGNOSTICS)
endif()

# AVX2/FMA: Enabled per-TU via #pragma in SincAVX2.h for safe runtime dispatch.
# IMPORTANT: Do NOT add global /arch:AVX2 here - it causes VEX-encoded scalar ops
# that crash on non-AVX2 CPUs even in the fallback path.

target_include_directories(AestraAudioCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        # Add all include subdirectories for reorganization-proof headers
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Core
        ${CMAKE_CURRENT_SOURCE_DIR}/include/DSP
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Models
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Playback
        ${CMAKE_CURRENT_SOURCE_DIR}/include/IO
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Drivers
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Plugin
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Commands
        ${CMAKE_CURRENT_SOURCE_DIR}/include/Headless
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../AestraCore/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../AestraPlat/include
        # Add all src subdirectories for reorganization-proof includes
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Playback
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Models
        ${CMAKE_CURRENT_SOURCE_DIR}/src/DSP
        ${CMAKE_CURRENT_SOURCE_DIR}/src/IO
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Drivers
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Plugin
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Commands
)

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/External/miniaudio)
    target_include_directories(AestraAudioCore PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/External/miniaudio
    )
endif()

# VST3 SDK includes (if available)
if(AESTRA_HAS_VST3)
    target_include_directories(AestraAudioCore PRIVATE
        ${VST3_INCLUDE_DIRS}
    )
    target_compile_definitions(AestraAudioCore PRIVATE AESTRA_HAS_VST3)
endif()

# CLAP SDK includes (if available)
if(AESTRA_HAS_CLAP)
    # SYSTEM, not plain PRIVATE. Five vendored CLAP headers declare file-scope
    # constants with internal linkage (CLAP_INVALID_ID, CLAP_COLOR_TRANSPARENT,
    # CLAP_BEATTIME_FACTOR, CLAP_SECTIME_FACTOR, CLAP_TIMESTAMP_UNKNOWN,
    # CLAP_CORE_EVENT_SPACE_ID). That is upstream's deliberate pattern and is valid
    # C and C++ — each translation unit gets its own copy, so there is no ODR
    # problem — but GCC and Clang can emit -Wunused-const-variable in any TU that
    # includes a header without reading its constant, which a stricter warning
    # profile would turn into a build failure.
    #
    # Fixed here rather than in the headers, because External/clap/VENDOR.md records
    # a sha256 of the vendored tree: editing those files would break the provenance
    # contract that proves they match upstream at the pinned commit. One build-side
    # line keeps the tree a byte-identical copy and keeps SDK updates a clean drop.
    target_include_directories(AestraAudioCore SYSTEM PRIVATE
        ${CLAP_INCLUDE_DIRS}
    )
    target_compile_definitions(AestraAudioCore PRIVATE AESTRA_HAS_CLAP)
endif()

target_link_libraries(AestraAudioCore
    PUBLIC
)

if(TARGET AestraCore)
    target_link_libraries(AestraAudioCore PUBLIC AestraCore)
endif()

# MuseSocketServer uses winsock on Windows.
if(WIN32)
    target_link_libraries(AestraAudioCore PUBLIC ws2_32)
endif()

# AestraAudioCore stays license-free: no AestraAudio source references any
# AestraLicense symbol (grep-verified 2026-07-09), and a PUBLIC link here dragged
# licensing into every headless/test/core consumer, contradicting core-mode
# semantics (AGENTS.md §2; architecture audit 2026Q2 §1.1). Consumers that
# genuinely need licensing (the app at Source/CMakeLists.txt, license tests)
# link AestraLicense directly.
target_link_libraries(AestraAudioCore PUBLIC AestraPlat)

# macOS frameworks needed by VST3 hosting
if(APPLE)
    target_link_libraries(AestraAudioCore PUBLIC
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework Foundation"
    )
endif()

# Media Foundation symbols are referenced from AestraAudioCore sources on Windows
# (for example MiniAudioDecoder), so consumers of the static library need these
# system libraries transitively.
if(WIN32)
    target_link_libraries(AestraAudioCore PUBLIC mfplat mfreadwrite mfuuid)
endif()

add_executable(AestraPluginHost
    src/Plugin/AestraPluginHostMain.cpp
)

# The helper is otherwise self-contained (hand-mirrored plugin ABIs), but it now
# includes the shared Plugin/ClapNoteConversion.h header, so it needs AestraAudio's
# include root on every config — not only when VST3 links AestraAudioCore (#244).
target_include_directories(AestraPluginHost PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(AestraPluginHost PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(AESTRA_ENABLE_TESTS)
    target_compile_definitions(AestraPluginHost PRIVATE AESTRA_ENABLE_TEST_HOOKS)
endif()

if(AESTRA_HAS_VST3)
    target_compile_definitions(AestraPluginHost PRIVATE AESTRA_HAS_VST3)
    target_link_libraries(AestraPluginHost PRIVATE AestraAudioCore)
endif()

# ENFORCEMENT: AestraAudioCore MUST NOT link Windows libraries
# We can check this by property retrieval (not easy at config time for implicit libs)
# But we ensure we DO NOT call target_link_libraries with avrt, advapi32 etc.

# =============================================================================
# AestraAudioWin (Windows Backend)
# =============================================================================

if(WIN32)
    set(AESTRA_AUDIO_WIN_SOURCES
        src/Win32/WindowsDriverRegistry.cpp
        src/Win32/WASAPISharedDriver.cpp
        src/Win32/WASAPIExclusiveDriver.cpp
        src/Win32/ASIODriverInfo.cpp
        src/Win32/RtAudioBackend.cpp
        src/Drivers/ASIODriver.cpp  # Added Phase 6
    )
    
    set(AESTRA_AUDIO_WIN_HEADERS
        src/Win32/WASAPISharedDriver.h
        src/Win32/WASAPIExclusiveDriver.h
        src/Win32/ASIODriverInfo.h
        src/Win32/RtAudioBackend.h
        include/Drivers/NativeAudioDriver.h
        include/Drivers/ASIODriver.h     # Added Phase 6
        include/Drivers/ASIOInterface.h  # Added Phase 6
        ${RTAUDIO_HEADERS} # RtAudio headers needed for backend compilation
        ${RTMIDI_HEADERS}
    )

    add_library(AestraAudioWin STATIC
        ${AESTRA_AUDIO_WIN_SOURCES}
        ${AESTRA_AUDIO_WIN_HEADERS}
        ${RTAUDIO_SOURCES}
        ${RTMIDI_SOURCES}
        src/IO/MidiInputService.cpp
    )

    # Real hardware MIDI input lives in the backend (AGENTS.md §8); the
    # no-backend stub build of MidiInputService.cpp happens in AestraAudioCore
    # only when no platform backend target exists (see end of backend section).
    target_compile_definitions(AestraAudioWin PRIVATE AESTRA_HAS_RTMIDI)

    target_include_directories(AestraAudioWin
        PUBLIC
            # Nothing public - backend is opaque
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include # Access to Core headers
            ${CMAKE_CURRENT_SOURCE_DIR}/src/Win32
            ${RTAUDIO_DIR}
            ${RTMIDI_DIR}
    )

    target_link_libraries(AestraAudioWin
        PUBLIC
            AestraAudioCore
        PRIVATE
            ${RTAUDIO_LIBS}
            avrt        # MMCSS
            advapi32    # Registry
    )
    
    # Use INTERFACE (not ALIAS) so PUBLIC include dirs from AestraAudioWin propagate
    add_library(AestraAudio INTERFACE)
    target_link_libraries(AestraAudio INTERFACE AestraAudioWin)

elseif(UNIX)
    # Non-Windows (Linux)
    if(ALSA_FOUND)
        set(AESTRA_AUDIO_LINUX_SOURCES
            src/Linux/LinuxDriverRegistry.cpp
            src/Linux/RtAudioDriver.cpp
        )

        set(AESTRA_AUDIO_LINUX_HEADERS
            src/Linux/RtAudioDriver.h
            include/Drivers/AudioPlatformRegistry.h
            ${RTAUDIO_HEADERS}
            ${RTMIDI_HEADERS}
        )

        add_library(AestraAudioLinux STATIC
            ${AESTRA_AUDIO_LINUX_SOURCES}
            ${AESTRA_AUDIO_LINUX_HEADERS}
            ${RTAUDIO_SOURCES}
            ${RTMIDI_SOURCES}
            src/IO/MidiInputService.cpp
        )

        # RtMidi reuses __LINUX_ALSA__ (global add_definitions above) and the
        # ALSA libs already in RTAUDIO_LIBS. Real implementation is backend-only
        # (AGENTS.md §8); the stub fallback is below the backend section.
        target_compile_definitions(AestraAudioLinux PRIVATE AESTRA_HAS_RTMIDI)

        target_include_directories(AestraAudioLinux
            PUBLIC
                # Nothing public
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/src/Linux
                ${RTAUDIO_DIR}
                ${RTMIDI_DIR}
        )

        target_link_libraries(AestraAudioLinux
            PUBLIC
                AestraAudioCore
            PRIVATE
                ${RTAUDIO_LIBS}
        )

        add_library(AestraAudio INTERFACE)
        if(TARGET AestraRumble)
            # Rumble's static plugin registration references InternalPluginRegistry
            # symbols that live in AestraAudioCore. Appended whole-archive AFTER the
            # closed rescan group (the old AestraPremiumPlugins wiring) it could not
            # resolve those symbols backwards — GNU ld is single-pass — which broke
            # every full-UI test binary that didn't coincidentally pull the registry
            # first (issue #555: undefined refs on some, silent/empty plugin registry
            # on others). Fold it INTO the rescan group, keeping WHOLE_ARCHIVE so the
            # self-registration TU is always linked and always resolvable.
            target_link_libraries(AestraAudio INTERFACE
                "$<LINK_GROUP:RESCAN,AestraAudioLinux,AestraAudioCore,$<LINK_LIBRARY:WHOLE_ARCHIVE,AestraRumble>>"
            )
            set(AESTRA_RUMBLE_IN_LINK_GROUP TRUE)
        else()
            target_link_libraries(AestraAudio INTERFACE
                "$<LINK_GROUP:RESCAN,AestraAudioLinux,AestraAudioCore>"
            )
        endif()
    else()
        # No ALSA - stub already in AestraAudioCore
        # Create an interface library for compatibility
        add_library(AestraAudio INTERFACE)
        target_link_libraries(AestraAudio INTERFACE AestraAudioCore)
    endif()
endif()

# MidiInputService compiles exactly once per build: into the platform backend
# above (real RtMidi implementation, AESTRA_HAS_RTMIDI defined) when one
# exists, otherwise here into AestraAudioCore as the no-backend stub — same
# API, zero ports. Keeps RtMidi out of core/headless builds (AGENTS.md §8).
if(NOT TARGET AestraAudioWin AND NOT TARGET AestraAudioLinux)
    target_sources(AestraAudioCore PRIVATE src/IO/MidiInputService.cpp)
endif()

# On Linux+ALSA with Rumble present, Rumble is already whole-archived inside
# the rescan group above; appending the premium interface (which wraps the same
# archive in WHOLE_ARCHIVE) would double-link it into every consumer.
if(TARGET AestraAudio AND TARGET AestraPremiumPlugins AND NOT AESTRA_RUMBLE_IN_LINK_GROUP)
    target_link_libraries(AestraAudio INTERFACE AestraPremiumPlugins)
endif()


# =============================================================================
# Test Applications
# =============================================================================
# ALL TESTS MOVED TO Tests/CMakeLists.txt FOR CLEAN SEPARATION
# Do not add test targets here - they are centralized in the root Tests/ directory

# Per-TU AVX2 for benchmark (only the SincAVX2.h code uses intrinsics)
# For now, rely on runtime dispatch; SIMD code is guarded by CPUDetection.

# =============================================================================
# Status
# =============================================================================

message(STATUS "")
message(STATUS "=================================")
message(STATUS "  AestraAudio v${PROJECT_VERSION}")
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
