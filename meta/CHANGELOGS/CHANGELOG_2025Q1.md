# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### 🧹 Repository Cleanup (2025-11-02)

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
