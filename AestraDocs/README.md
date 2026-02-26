# Aestra Documentation Index

> **Built from scratch. Perfected with intention.**

This directory contains comprehensive documentation for the Aestra project, organized into logical categories for easy navigation.

## 📁 Directory Structure

### 🏛️ [architecture/](architecture/)

Architectural documentation and design decisions:

- [ADAPTIVE_FPS_ARCHITECTURE.md](architecture/ADAPTIVE_FPS_ARCHITECTURE.md) - Adaptive frame rate system design
- [DROPDOWN_ARCHITECTURE.md](architecture/DROPDOWN_ARCHITECTURE.md) - Dropdown menu system architecture
- [Aestra_MODE_IMPLEMENTATION.md](architecture/nomad-core.md) - Aestra mode implementation details
- [AestraUI_COORDINATE_SYSTEM.md](architecture/nomad-ui.md) - **CRITICAL:** UI coordinate system specification

### 📖 [guides/](guides/)

How-to guides and reference documentation:

- [DEVELOPER_GUIDE.md](guides/DEVELOPER_GUIDE.md) - Philosophy, architecture, and contribution guidelines
- [ADAPTIVE_FPS_GUIDE.md](guides/ADAPTIVE_FPS_GUIDE.md) - Using the adaptive FPS system
- [ADAPTIVE_FPS_README.md](guides/ADAPTIVE_FPS_README.md) - Adaptive FPS quick reference
- [UI_LAYOUT_GUIDE.md](guides/UI_LAYOUT_GUIDE.md) - UI layout and design guide
- [OPENGL_LINKING_GUIDE.md](guides/OPENGL_LINKING_GUIDE.md) - OpenGL setup and linking
- [DROPDOWN_QUICK_REFERENCE.md](guides/DROPDOWN_QUICK_REFERENCE.md) - Dropdown usage reference
- [COORDINATE_UTILITIES_V1.1.md](guides/COORDINATE_UTILITIES_V1.1.md) - Coordinate utility functions
- [DOCUMENTATION_POLISH_V1.1.md](guides/DOCUMENTATION_POLISH_V1.1.md) - Documentation standards

### 🔧 [systems/](systems/)

System-specific technical documentation:

- [AUDIO_DRIVER_SYSTEM.md](systems/AUDIO_DRIVER_SYSTEM.md) - Audio driver architecture and implementation
- [AUDIO_TIMING_QUALITY.md](systems/AUDIO_TIMING_QUALITY.md) - Audio timing and quality analysis
- [CUSTOM_WINDOW_INTEGRATION.md](systems/CUSTOM_WINDOW_INTEGRATION.md) - Custom window system integration
- [DROPDOWN_SYSTEM_V2.0.md](systems/DROPDOWN_SYSTEM_V2.0.md) - Dropdown system v2.0 specification
- [ADAPTIVE_FPS_PERFORMANCE_DIAGNOSTIC.md](systems/ADAPTIVE_FPS_PERFORMANCE_DIAGNOSTIC.md) - FPS performance diagnostics

### 📊 [status/](status/)

Project status, analysis, and planning documents:

- [BUILD_STATUS.md](status/BUILD_STATUS.md) - Current build status and module completion
- [BRANCHING_STRATEGY.md](status/BRANCHING_STRATEGY.md) - Git workflow and commit conventions
- [CURRENT_STATE_ANALYSIS.md](status/CURRENT_STATE_ANALYSIS.md) - Current project state analysis
- [DOCUMENTATION_STATUS.md](status/DOCUMENTATION_STATUS.md) - Documentation coverage status
- [SCREENSHOT_LIMITATION.md](status/SCREENSHOT_LIMITATION.md) - Known screenshot/rendering limitations

### 🐛 [Bug Reports/](Bug%20Reports/)

Bug reports and issue tracking:

- Historical bug reports and analysis

## 📚 Core Documentation

## 🏗️ Module Documentation

### AestraCore

**Location:** `AestraCore/README.md`

Base utilities for the entire Aestra ecosystem:

- Math utilities (vectors, matrices, DSP functions)
- Threading primitives (lock-free structures, thread pools)
- File I/O (binary serialization, JSON parsing)
- Logging system (multi-destination, stream-style)

### AestraPlat

**Location:** `AestraPlat/README.md`

Platform abstraction layer:

- Window management (Win32, X11, Cocoa)
- Input handling (mouse, keyboard, gamepad)
- OpenGL context creation
- DPI awareness and scaling
- Platform utilities (time, dialogs, clipboard)

**Additional Docs:**

- [DPI Support Guide](../AestraPlat/docs/DPI_SUPPORT.md) - Comprehensive DPI implementation details

### AestraUI

**Location:** `AestraUI/docs/`

UI framework with OpenGL rendering:

- Component system
- Theme and styling
- Animation system
- SVG icon support
- Custom windows

**Key Guides:**

- [Platform Migration](../AestraUI/docs/PLATFORM_MIGRATION.md) - Migration from old Windows code to AestraPlat
- [Architecture](../AestraUI/docs/ARCHITECTURE.md) - UI framework architecture
- [Custom Window Integration](../AestraUI/docs/CUSTOM_WINDOW_INTEGRATION.md) - Building custom windows
- [Coordinate System Guide](AestraUI_COORDINATE_SYSTEM.md) - **CRITICAL:** Understanding AestraUI positioning
- [Coordinate Utilities v1.1](COORDINATE_UTILITIES_V1.1.md) - **NEW:** Convenience helpers for positioning
- [Icon System Guide](../AestraUI/docs/ICON_SYSTEM_GUIDE.md) - SVG icon usage
- [Theme Demo Guide](../AestraUI/docs/THEME_DEMO_GUIDE.md) - Theming system
- [Windows Snap Guide](../AestraUI/docs/WINDOWS_SNAP_GUIDE.md) - Window snapping behavior

## 🎯 Quick Links

### For New Contributors

1. Read [Developer Guide](DEVELOPER_GUIDE.md) - Understand the philosophy
2. Check [Build Status](BUILD_STATUS.md) - See what's complete
3. Review [Branching Strategy](BRANCHING_STRATEGY.md) - Learn the workflow
4. Pick a module and dive in!

### For Module Development

- **Working on Core?** → `AestraCore/README.md`
- **Working on Platform?** → `AestraPlat/README.md` + `AestraPlat/docs/DPI_SUPPORT.md`
- **Working on UI?** → `AestraUI/docs/ARCHITECTURE.md`

### For Integration

- **Adding DPI support?** → `AestraPlat/docs/DPI_SUPPORT.md`
- **Migrating from old code?** → `AestraUI/docs/PLATFORM_MIGRATION.md`
- **Building custom windows?** → `AestraUI/docs/CUSTOM_WINDOW_INTEGRATION.md`
- **Component positioning issues?** → `AestraDocs/AestraUI_COORDINATE_SYSTEM.md` ⚠️ **READ THIS FIRST**

## 📊 Project Status

| Module | Status | Version | Documentation |
|--------|--------|---------|---------------|
| AestraCore | ✅ Complete | v1.0.0 | Complete |
| AestraPlat | ✅ Complete | v1.0.0 | Complete |
| AestraUI | ✅ Complete | v0.1.0 | Complete |
| AestraAudio | ⏳ Planned | - | Pending |
| AestraSDK | ⏳ Planned | - | Pending |

## 🔧 Build & Test

### Quick Build

```powershell
.\build.ps1
```

### Manual Build

```bash
cmake -B build
cmake --build build --config Debug
```

### Run Tests

```bash
# Core tests
.\build\AestraCore\Debug\MathTests.exe
.\build\AestraCore\Debug\ThreadingTests.exe
.\build\AestraCore\Debug\FileTests.exe
.\build\AestraCore\Debug\LogTests.exe

# Platform tests
.\build\AestraPlat\Debug\PlatformWindowTest.exe
.\build\AestraPlat\Debug\PlatformDPITest.exe

# UI examples
.\build\bin\Debug\AestraUI_CustomWindowDemo.exe
.\build\bin\Debug\AestraUI_WindowDemo.exe
```

## 📝 Documentation Standards

### File Naming

- `README.md` - Module overview and quick start
- `GUIDE.md` suffix - Comprehensive guides
- `STATUS.md` suffix - Status and progress tracking
- `STRATEGY.md` suffix - Architectural decisions

### Content Structure

1. **Overview** - What is this?
2. **Features** - What can it do?
3. **Usage** - How do I use it?
4. **Examples** - Show me!
5. **API Reference** - Technical details
6. **Notes** - Important considerations

### Code Examples

- Always include complete, runnable examples
- Show both simple and advanced usage
- Include error handling
- Add comments for clarity

## 🎨 Philosophy

> "Branches are intentions. Commits are promises. Merges are rituals."

> "Build like silence is watching."

Aestra is built with intention. Every line of code, every architectural decision, every pixel is deliberate. We don't chase trends or add features for the sake of features. We build what matters, and we build it right.

## 🤝 Contributing

1. Read the [Developer Guide](DEVELOPER_GUIDE.md)
2. Follow the [Branching Strategy](BRANCHING_STRATEGY.md)
3. Write clean, documented code
4. Test thoroughly
5. Submit with clear commit messages

## 📧 Contact

For questions, suggestions, or contributions, please refer to the project repository.

---

**Last Updated:** November 2025  
**Maintained by:** Aestra Development Team
