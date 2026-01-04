# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### UI & Workflow

- Mixer: muted channels keep showing VU activity (rendered in a monochrome/dim style)
- Mixer: Trim/Pan controls now show on selection/drag only (no hover pop-in)
- Playlist: improved mute/solo readability (muted dims; solo highlights + suppresses other lanes)
- Playlist: lane-group mute/solo toggles now propagate to all clips sharing the lane

### Audio & Transport

- Transport: when the project is empty, playback loops over bar 1 instead of running off forever
- Transport: deleting the last clip during playback snaps playhead back to bar 1

## [1.1.0] - 2025-12-14

### 🎯 Major Features Added

- **FL Studio-Style Timeline** - Complete step sequencer implementation with pattern and playlist sequencing
- **Piano Roll Interface** - Full piano keyboard with note editing, velocity lanes, and MIDI-style input
- **Audio Graph System** - Advanced audio routing with MixerBus class and professional mixing capabilities
- **Transport Controls Enhancement** - Green playing indicators, timeline looping, and transport state management
- **Audio Engine Pipeline** - Complete WASAPI integration with multi-tier processing and ultra-low latency

### 🎵 Audio Engine Improvements

- **WASAPI Multi-Tier Architecture** - Professional-grade audio with exclusive/shared mode switching
- **RtAudio Integration** - Cross-platform audio backend with comprehensive device management
- **Audio Resampling** - Fixed sample rate mismatches and improved buffer handling
- **Timeline Looping** - Seamless loop playback with proper sample-accurate timing
- **Audio Metering** - Real-time level monitoring and visual feedback
- **Waveform Caching** - 4096-sample waveform cache for efficient visualization

### 🎨 User Interface Enhancements

- **Adaptive FPS System** - Intelligent rendering optimization (24-60 FPS) for better performance
- **Unbounded Timeline** - Asymmetric culling with generous padding to prevent visual artifacts
- **Transport Bar Fixes** - Green playing state indicators and improved transport control responsiveness
- **Button Hover System** - Fixed hover detection timing and eliminated lingering hover states
- **Scrollbar Improvements** - Stable scrollbar behavior with bounds clamping and smooth scrolling
- **Ruler Accuracy** - Fixed off-by-one errors in timeline ruler display

### 🔧 Technical Infrastructure

- **Platform Abstraction** - Enhanced Win32 integration with proper DPI support and window management
- **Thread Safety** - Comprehensive atomic operations for real-time parameter changes
- **Memory Management** - Optimized allocation patterns and cache-friendly data structures
- **Error Handling** - Robust error recovery and graceful device-in-use handling
- **Performance Profiling** - Integration with Nomad profiler for detailed performance analysis

### 🐛 Critical Bug Fixes

- **Audio Duration Bug** - Fixed audio cutting 7 seconds early due to sample rate mismatch
- **Ruler Display Bug** - Fixed ruler showing bar 9 on startup with corrected off-by-one calculations
- **Scrollbar Disappearing** - Added bounds clamping to prevent scrollbar vanishing on zoom
- **Audio Crackles** - Eliminated crackling from audio graph rebuilds
- **Dropdown Click-through** - Fixed dropdown interfering with buttons underneath
- **File Browser Navigation** - Fixed double-click tracking and keyboard navigation
- **Track Scrollbar Jumping** - Resolved thumb jumping to top/left when dragging

### 📚 Documentation & Organization

- **Comprehensive API Documentation** - Complete Doxygen integration with cross-referenced code
- **MkDocs Portal** - Professional documentation website with searchable content
- **Session Summaries** - Detailed development logs and task completion records
- **Architecture Documentation** - Clear separation of concerns documentation (Core, Audio, UI, Platform)
- **Developer Guides** - Complete contributing guidelines, coding standards, and debugging resources

### 🔧 Build System & CI/CD

- **CMake Optimization** - Streamlined build configuration across all modules
- **Git Hooks Integration** - Pre-commit validation with clang-format and security scanning
- **CI Pipeline** - Automated testing and validation across multiple platforms
- **Platform Support** - Enhanced Windows 10/11 support with future Linux/macOS preparation

### 🎛️ Mixing & Processing

- **MixerBus Class** - Professional mixing with gain, pan, mute, and solo controls
- **Constant Power Panning** - Industry-standard panning algorithm maintaining equal perceived loudness
- **Audio Routing** - Multi-bus architecture with master output mixing
- **Real-time Safety** - All mixing operations are real-time safe with atomic parameters
- **Thread-safe Parameters** - Lock-free parameter changes using C++ atomic operations

### ⚡ Performance & Architecture

- **64-bit Precision Processing** - Toggle between 32-bit float and 64-bit double-precision audio
- **Multi-Threaded Audio** - Parallel track processing with configurable thread count (2-8 threads)
- **Audio Thread Pool** - Real-time optimized thread pool with lock-free task submission
- **Lock-free Communication** - UI→Audio command queue with atomic parameter updates
- **Buffer Management** - Pre-allocated buffer pools for zero-allocation audio processing
- **Audio Callback System** - Comprehensive audio processor with test tone generation

### 🧠 Advanced Audio Features

- **Audio Processor Framework** - Base class for audio processing with virtual process() method
- **Lock-free Command Queue** - 256-message ring buffer for UI→Audio communication
- **Test Tone Generator** - Multi-frequency sine wave generator with real-time control
- **Buffer Manager** - AudioBufferManager with pre-allocated pools (8192 frames × 8 channels)
- **Atomic Parameters** - Thread-safe gain, pan, mute, and frequency control

## [1.0.1] - 2025-02-15

### 🧹 Repository Cleanup

- **Optimized .gitignore** - Consolidated duplicate entries and improved organization
- **Added C++ Code Formatting** - Created .clang-format configuration for consistent code style
- **Git Repository Optimization** - Ran git gc --aggressive to optimize repository size
- **Code Quality** - Reviewed and validated codebase structure and organization

### ✨ Features

- **Improved Button Hover System** - Fixed inconsistent hover detection and eliminated lingering hover states
- **Enhanced Event Propagation** - Reordered hover detection to happen after event handling for better responsiveness
- **Removed Button Press Color Changes** - Eliminated jarring purple color changes on button press for cleaner UI

### 🐛 Bug Fixes

- Fixed hover state detection timing in `NUIComponent::onMouseEvent()`
- Eliminated hover states that persisted after mouse leave
- Removed default purple pressed color from buttons
- Fixed merge conflicts in README.md

### 📚 Documentation

- Updated README.md with current project status
- Fixed copyright year in license section
- Improved project structure documentation

### 🔧 Technical Improvements

- Updated Git configuration for proper contribution tracking
- Synchronized main and develop branches
- Improved code organization in UI components

## [1.0.0] - 2025-01-XX

### 🎯 Foundation Complete

- **NomadCore** - Complete math, threading, file I/O, and logging system
- **NomadPlat** - Complete platform abstraction with DPI support
- **NomadUI** - Complete OpenGL renderer and component system
- **Build System** - CMake-based cross-platform build configuration

### 🎨 UI Framework Features

- Custom OpenGL 3.3+ renderer with MSAA
- Component-based UI system (buttons, labels, sliders, etc.)
- Theme system with dark/light mode support
- SVG icon system with color tinting
- Smooth animations and transitions

### 🖥️ Platform Support

- **Windows** - Complete Win32 API integration
- **Linux** - Planned X11 support
- **macOS** - Planned Cocoa support

---

## Contributing

We welcome contributions! Please see our [Contributing Guidelines](NomadDocs/DEVELOPER_GUIDE.md) for details.

## License

Copyright © 2025 Dylan Makori. All rights reserved.
