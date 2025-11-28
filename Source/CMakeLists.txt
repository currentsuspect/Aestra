# =============================================================================
# NOMAD DAW - Main Application
# =============================================================================

cmake_minimum_required(VERSION 3.22)



# Create executable
set(NOMAD_DAW_SOURCES
	Main.cpp
	TransportBar.h
	TransportBar.cpp
	TransportInfoContainer.h
	TransportInfoContainer.cpp
	AudioSettingsDialog.h
	AudioSettingsDialog.cpp
	FileBrowser.h
	FileBrowser.cpp
	AudioVisualizer.h
	AudioVisualizer.cpp
	TrackUIComponent.h
	TrackUIComponent.cpp
	TrackManagerUI.h
	TrackManagerUI.cpp
	MixerView.h
	MixerView.cpp
	WindowPanel.h
	WindowPanel.cpp
	PianoRollPanel.h
	PianoRollPanel.cpp
	MixerPanel.h
	MixerPanel.cpp
	ui/InfoTab.h
	ui/InfoTab.cpp
	license/LicenseVerifier.h
	license/LicenseVerifier.cpp
	PerformanceHUD.h
	PerformanceHUD.cpp
)



add_executable(NOMAD_DAW ${NOMAD_DAW_SOURCES})

# Set C++ standard
set_target_properties(NOMAD_DAW PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
)

# Link against NOMAD libraries
target_link_libraries(NOMAD_DAW PRIVATE
	NomadCore
	NomadPlat
	NomadUI_Core
	NomadUI_Platform
	NomadUI_OpenGL
	NomadAudio
)

# Include directories
target_include_directories(NOMAD_DAW PRIVATE
	${CMAKE_SOURCE_DIR}/NomadCore/include
	${CMAKE_SOURCE_DIR}/NomadPlat/include
	${CMAKE_SOURCE_DIR}/NomadUI/Graphics
	${CMAKE_SOURCE_DIR}/NomadUI/Core
	${CMAKE_SOURCE_DIR}/NomadAudio/include
	${CMAKE_CURRENT_SOURCE_DIR}/license
)



# Platform-specific settings
if(WIN32)
	# Windows-specific settings
	target_compile_definitions(NOMAD_DAW PRIVATE
		_CRT_SECURE_NO_WARNINGS
		NOMINMAX
	)
	
	# Use console subsystem for now (can switch to WIN32_EXECUTABLE later)
	# set_target_properties(NOMAD_DAW PROPERTIES
	#     WIN32_EXECUTABLE TRUE
	# )
	
	# Link Windows libraries
	# Note: We don't explicitly link opengl32 here
	# NomadPlat links it privately for WGL functions (context creation)
	# GLAD provides all GL* functions dynamically
	target_link_libraries(NOMAD_DAW PRIVATE
		gdi32
		user32
		shell32
		ole32
		comdlg32
	)
	
	# Force GLAD symbols to take precedence over any opengl32 symbols
	# This ensures GL functions come from GLAD, not opengl32
	target_link_options(NOMAD_DAW PRIVATE
		"/WHOLEARCHIVE:$<TARGET_FILE:glad>"
		"/IGNORE:4098"  # Ignore CRT mismatch warning (FreeType uses static CRT)
	)

	# Add the resource script so the icon is embedded into the executable
	# Note: we expect Source/nomad.ico to be present in the repository. The
	# automatic SVG->ICO conversion was removed to keep CI/tooling simple.
	target_sources(NOMAD_DAW PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/app_icon.rc")
elseif(UNIX AND NOT APPLE)
	# Linux-specific settings
	target_link_libraries(NOMAD_DAW PRIVATE
		GL
		X11
		pthread
		dl
	)
elseif(APPLE)
	# macOS-specific settings
	find_library(COCOA_LIBRARY Cocoa)
	find_library(OPENGL_LIBRARY OpenGL)
	find_library(IOKIT_LIBRARY IOKit)
	find_library(COREVIDEO_LIBRARY CoreVideo)
	
	target_link_libraries(NOMAD_DAW PRIVATE
		${COCOA_LIBRARY}
		${OPENGL_LIBRARY}
		${IOKIT_LIBRARY}
		${COREVIDEO_LIBRARY}
	)
endif()

# Set output directory
set_target_properties(NOMAD_DAW PROPERTIES
	RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy mock license file alongside binary for contributors
add_custom_command(TARGET NOMAD_DAW POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NOMAD_DAW>/data"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/license/user_info.json"
		"$<TARGET_FILE_DIR:NOMAD_DAW>/data/user_info.json"
)

message(STATUS "NOMAD DAW application configured")
