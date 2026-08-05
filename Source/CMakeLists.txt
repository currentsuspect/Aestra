# =============================================================================
# Aestra - Main Application
# =============================================================================

cmake_minimum_required(VERSION 3.22)



# Create executable
set(Aestra_SOURCES
	# App
	App/Main.cpp
	App/AestraApp.h
	App/AestraApp.cpp
    App/MuseHostVerbs.cpp
    App/MuseProjectLoadReport.h
    App/MuseProjectLoadReport.cpp
	
	# Core
	Core/AestraAudioController.h
	Core/AestraAudioController.cpp
	Core/AestraWindowManager.h
	Core/AestraWindowManager.cpp
	Core/AestraRootComponent.h
	Core/AestraRootComponent.cpp
	Core/AestraContent.h
	Core/AestraContent.cpp
	Core/MusicalTypingController.h
	Core/MusicalTypingController.cpp
	Core/ProjectDocumentState.h
	Core/ProjectSerializer.cpp
	Core/TakeManager.h
	Core/TakeManager.cpp
	Core/Preferences.h
	Core/AudioSettingsStore.cpp
	Core/Preferences.cpp
	Core/UIState.h
	Core/UIState.cpp
	
	# Panels
	Panels/TransportBar.h
	Panels/TransportBar.cpp
	Panels/WindowPanel.h
	Panels/WindowPanel.cpp
	Panels/PianoRollPanel.h
	Panels/PianoRollPanel.cpp
	Panels/MixerPanel.h
	Panels/MixerPanel.cpp
	Panels/ArsenalPanel.h
	Panels/ArsenalPanel.cpp
	# Panels/StepSequencerPanel removed
	Panels/PatternBrowserPanel.h
	Panels/PatternBrowserPanel.cpp
	Panels/AuditionPanel.h
	Panels/AuditionPanel.cpp
	Panels/AestraHistoryPanel.h
	Panels/AestraHistoryPanel.cpp
	Panels/TakesPanel.h
	Panels/TakesPanel.cpp
	Panels/SampleEditorPanel.h
	Panels/SampleEditorPanel.cpp
	Panels/AudioClipEditorPanel.h
	Panels/AudioClipEditorPanel.cpp
	
	# Settings
	Settings/ConfirmationDialog.h
	Settings/ConfirmationDialog.cpp
	Settings/RecoveryDialog.h
	Settings/RecoveryDialog.cpp
	Settings/SettingsDialog.h
	Settings/SettingsDialog.cpp
	Settings/ISettingsPage.h
	Settings/AudioSettingsPage.h
	Settings/AudioSettingsPage.cpp
	Settings/GeneralSettingsPage.h
	Settings/GeneralSettingsPage.cpp
	Settings/MembershipSettingsPage.h
	Settings/MembershipSettingsPage.cpp
	Settings/AppearanceSettingsPage.h
	Settings/AppearanceSettingsPage.cpp
	Settings/ExportDialog.h
	Settings/ExportDialog.cpp
	
	# Components
	Components/TransportInfoContainer.h
	Components/TransportInfoContainer.cpp
	Components/FileBrowser.h
	Components/FileBrowser.cpp
	Components/FilePreviewPanel.h
	Components/FilePreviewPanel.cpp
	Components/AudioVisualizer.h
	Components/AudioVisualizer.cpp
	Components/TrackUIComponent.h
	Components/TrackUIComponent.cpp
	Components/TrackManagerUI.h
	Components/TrackManagerUI.cpp
	Components/TrackManagerUIInternal.h
	Components/TrackManagerUIToolbar.cpp
	Components/TrackManagerUIClipOps.cpp
	Components/TrackManagerUIRender.cpp
	Components/TrackManagerUIEvents.cpp
	Components/TrackManagerUIView.cpp
	Components/TrackManagerUIDragDrop.cpp
	Components/MixerView.h
	Components/MixerView.cpp
	Components/MixerMath.h
	Components/MixerViewModel.h
	Components/MixerViewModel.cpp
	Components/TimelineMinimapModel.h
	Components/TimelineSummaryCache.h
	Components/TimelineSummaryCache.cpp
	Components/TimelineMinimapRenderer.h
	Components/TimelineMinimapRenderer.cpp
	Components/TimelineMinimapBar.h
	Components/TimelineMinimapBar.cpp
	
	# HUD
	HUD/PerformanceHUD.h
	HUD/PerformanceHUD.cpp
	HUD/UnifiedHUD.h
	HUD/UnifiedHUD.cpp
	
	# Root-level (not moved)
	OverlayLayer.h
	
	# Other
	ui/InfoTab.h
	ui/InfoTab.cpp
	license/LicenseVerifier.h
	license/LicenseVerifier.cpp
)



add_executable(Aestra ${Aestra_SOURCES})

if(AESTRA_ENABLE_TEST_LICENSES)
	target_compile_definitions(Aestra PRIVATE AESTRA_ENABLE_TEST_LICENSES=1)
endif()

if(TARGET AestraLicense)
	target_link_libraries(Aestra PRIVATE AestraLicense)
	target_compile_definitions(Aestra PRIVATE AESTRA_HAS_LICENSE_GATE=1)
else()
	if(NOT AESTRA_ALLOW_LICENSE_GATE_OFF)
		message(FATAL_ERROR "Aestra UI requires AestraLicense. Set AESTRA_ALLOW_LICENSE_GATE_OFF=ON only for lab/development builds that intentionally omit offline license verification.")
	endif()
	target_compile_definitions(Aestra PRIVATE AESTRA_HAS_LICENSE_GATE=0)
endif()

# Enable UTF-8 encoding for source files (fixes Unicode character rendering)
if(MSVC)
    target_compile_options(Aestra PRIVATE /utf-8)
endif()

# Output to bin directoryandard
set_target_properties(Aestra PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
)

# Link against AESTRA libraries
# --start-group around audio libs so GNU ld resolves RegisterPlatformDrivers
# (Core calls it, Linux provides it — circular dependency)
target_link_libraries(Aestra PRIVATE
	AestraCore
	AestraPlat
	AestraUI_Core
	AestraUI_Platform
	AestraUI_OpenGL
	-Wl,--start-group AestraAudio AestraAudioCore -Wl,--end-group
)

# Release-only optimization flags
# Scoped to Release via generator expressions — never applied to Debug/RelWithDebInfo.
# NO -ffast-math: codebase has ~15 std::isfinite()/std::isnan() guards in audio hot paths.
# Enabling -ffast-math silently disables those guards (compiler optimizes them away).
# -ffast-math is a separate project: audit guards, replace with fast-math-safe equivalents, test.
# NO -fno-exceptions: the app relies on exception handling — Main.cpp's top-level
# catch, AestraJSON's SEC-RTM-013 parse guards, and std file/parse paths all catch.
# Disabling exceptions turns those throws into aborts.
# NO -fno-rtti: Source UI files use std::dynamic_pointer_cast (e.g. dropdown
# resolution in AudioSettingsPage::onRender), which requires RTTI.
# Note: CI runs with AESTRA_CI=ON, which forces headless mode — the Aestra app
# target (and these flags) are NOT exercised by CI. Validate app builds locally.
if(NOT MSVC)
    target_compile_options(Aestra PRIVATE
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-flto>
    )
    target_link_options(Aestra PRIVATE
        $<$<CONFIG:Release>:-flto>
    )
endif()

# Include directories
# Add all subdirectories so headers can be included without relative paths
# This makes the codebase reorganization-proof - #include "ViewTypes.h" works from anywhere
target_include_directories(Aestra PRIVATE
	${CMAKE_SOURCE_DIR}/AestraCore/include
	${CMAKE_SOURCE_DIR}/AestraPlat/include
	${CMAKE_SOURCE_DIR}/AestraUI/Graphics
	${CMAKE_SOURCE_DIR}/AestraUI/Core
	${CMAKE_SOURCE_DIR}/AestraUI/Base
	${CMAKE_SOURCE_DIR}/AestraAudio/include/Core
	${CMAKE_SOURCE_DIR}/AestraAudio/include/DSP
	${CMAKE_SOURCE_DIR}/AestraAudio/include/Models
	${CMAKE_SOURCE_DIR}/AestraAudio/include/Playback
	${CMAKE_SOURCE_DIR}/AestraAudio/include/IO
	${CMAKE_SOURCE_DIR}/AestraAudio/include/Drivers
	${CMAKE_SOURCE_DIR}/AestraAudio/include/Plugin
	${CMAKE_SOURCE_DIR}/AestraAudio/include/Commands
	${CMAKE_CURRENT_SOURCE_DIR}/license
	# Add all Source subdirectories to include path
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/App
	${CMAKE_CURRENT_SOURCE_DIR}/Core
	${CMAKE_CURRENT_SOURCE_DIR}/Panels
	${CMAKE_CURRENT_SOURCE_DIR}/Settings
	${CMAKE_CURRENT_SOURCE_DIR}/Components
	${CMAKE_CURRENT_SOURCE_DIR}/HUD
)



# Platform-specific settings
if(WIN32)
	# Windows-specific settings
	target_compile_definitions(Aestra PRIVATE
		_CRT_SECURE_NO_WARNINGS
		NOMINMAX
	)
	
	# Windows GUI application (no console window)
	set_target_properties(Aestra PROPERTIES
	    WIN32_EXECUTABLE TRUE
	)
	
	# Link Windows libraries
	# Note: We don't explicitly link opengl32 here
	# AestraPlat links it privately for WGL functions (context creation)
	# GLAD provides all GL* functions dynamically
	target_link_libraries(Aestra PRIVATE
		gdi32
		user32
		shell32
		ole32
		comdlg32
	)
	
	# Force GLAD symbols to take precedence over any opengl32 symbols
	# This ensures GL functions come from GLAD, not opengl32
	target_link_options(Aestra PRIVATE
		"/WHOLEARCHIVE:$<TARGET_FILE:glad>"
		"/IGNORE:4098"  # Ignore CRT mismatch warning (FreeType uses static CRT)
	)

	# Add the resource script so the icon is embedded into the executable
	# Note: we expect Source/aestra.ico to be present in the repository. The
	# automatic SVG->ICO conversion was removed to keep CI/tooling simple.
	target_sources(Aestra PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.rc")
elseif(UNIX AND NOT APPLE)
	# Linux-specific settings
	find_package(OpenGL REQUIRED)
	find_package(X11 REQUIRED)

	target_link_libraries(Aestra PRIVATE
		OpenGL::GL
		X11::X11
		pthread
		dl
	)
elseif(APPLE)
	# macOS-specific settings
	find_library(COCOA_LIBRARY Cocoa)
	find_library(OPENGL_LIBRARY OpenGL)
	find_library(IOKIT_LIBRARY IOKit)
	find_library(COREVIDEO_LIBRARY CoreVideo)
	
	target_link_libraries(Aestra PRIVATE
		${COCOA_LIBRARY}
		${OPENGL_LIBRARY}
		${IOKIT_LIBRARY}
		${COREVIDEO_LIBRARY}
	)
endif()

# Set output directory
set_target_properties(Aestra PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Deploy the tracked desktop font bundle beside the executable. Main.cpp
# resolves AestraAssets/fonts relative to the executable, so this keeps the
# intended Geist/Manrope rendering independent of the process working directory
# and works for external build trees as well as packaged desktop layouts.
set(AESTRA_DESKTOP_FONT_SOURCE_DIR "${CMAKE_SOURCE_DIR}/AestraAssets/fonts")
set(AESTRA_DESKTOP_FONT_FILES
	Geist/Geist-Bold.ttf
	Geist/Geist-Medium.ttf
	Geist/Geist-Regular.ttf
	Manrope/Manrope-Bold.ttf
	Manrope/Manrope-Medium.ttf
	Manrope/Manrope-Regular.ttf
)

foreach(font_relative_path IN LISTS AESTRA_DESKTOP_FONT_FILES)
	set(font_source_path "${AESTRA_DESKTOP_FONT_SOURCE_DIR}/${font_relative_path}")
	if(NOT EXISTS "${font_source_path}")
		message(FATAL_ERROR "Required Aestra desktop font is missing: ${font_source_path}")
	endif()

	get_filename_component(font_subdirectory "${font_relative_path}" DIRECTORY)
	add_custom_command(TARGET Aestra POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory
			"$<TARGET_FILE_DIR:Aestra>/AestraAssets/fonts/${font_subdirectory}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${font_source_path}"
			"$<TARGET_FILE_DIR:Aestra>/AestraAssets/fonts/${font_relative_path}"
	)
endforeach()

# Copy mock license file alongside binary for contributors
add_custom_command(TARGET Aestra POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Aestra>/data"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/license/user_info.json"
		"$<TARGET_FILE_DIR:Aestra>/data/user_info.json"
)

if(BUILD_TESTING)
	add_test(NAME AestraDesktopFontBundleTest
		COMMAND ${CMAKE_COMMAND}
			"-DSOURCE_FONT_ROOT=${AESTRA_DESKTOP_FONT_SOURCE_DIR}"
			"-DBUNDLED_FONT_ROOT=$<TARGET_FILE_DIR:Aestra>/AestraAssets/fonts"
			-P "${CMAKE_SOURCE_DIR}/Tests/Guards/verify_aestra_font_bundle.cmake"
	)
	set_tests_properties(AestraDesktopFontBundleTest PROPERTIES
		LABELS "ui;assets;fonts;regression;contract:application"
		TIMEOUT 30
	)
endif()

message(STATUS "Aestra application configured")
