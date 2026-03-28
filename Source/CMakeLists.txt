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
	
	# Core
	Core/AestraAudioController.h
	Core/AestraAudioController.cpp
	Core/AestraWindowManager.h
	Core/AestraWindowManager.cpp
	Core/AestraRootComponent.h
	Core/AestraRootComponent.cpp
	Core/AestraContent.h
	Core/AestraContent.cpp
	Core/ProjectSerializer.cpp
	Core/Preferences.h
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
	Settings/AppearanceSettingsPage.h
	Settings/AppearanceSettingsPage.cpp
	
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

# Include directories
# Add all subdirectories so headers can be included without relative paths
# This makes the codebase reorganization-proof - #include "ViewTypes.h" works from anywhere
target_include_directories(Aestra PRIVATE
	${CMAKE_SOURCE_DIR}/AestraCore/include
	${CMAKE_SOURCE_DIR}/AestraPlat/include
	${CMAKE_SOURCE_DIR}/AestraUI/Graphics
	${CMAKE_SOURCE_DIR}/AestraUI/Core
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

# Copy mock license file alongside binary for contributors
add_custom_command(TARGET Aestra POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Aestra>/data"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/license/user_info.json"
		"$<TARGET_FILE_DIR:Aestra>/data/user_info.json"
)

message(STATUS "Aestra application configured")
