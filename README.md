# 🜂 NOMAD

<div align="center">

**Built from scratch. Perfected with intention.**

*A hand-engineered digital audio workstation and creative framework*

[![License](https://img.shields.io/badge/License-Proprietary-red.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-blue.svg)](#platforms)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C.svg?logo=c%2B%2B)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.22%2B-064F8C.svg?logo=cmake)](https://cmake.org/)
[![Status](https://img.shields.io/badge/Status-v1.0%20Foundation%20Complete-success.svg)](#roadmap)

</div>

---

## 🎯 Philosophy

> *"Every pixel and buffer here exists because someone decided it should. There is no automation without understanding, no shortcut without intent."*

NOMAD is a fully self-authored DAW with **zero borrowed frameworks**. Every layer — from windowing to audio I/O to rendering — is crafted by hand. This is not just software; it's a statement about what's possible when you build with intention.

---

## ✨ What Makes NOMAD Different

- 🎨 **Hand-Crafted UI** - Custom OpenGL renderer with MSAA, no UI framework dependencies
- ⚡ **Ultra-Low Latency** - Direct platform APIs, optimized for real-time audio
- 🎯 **Intentional Design** - Every feature exists for a reason, nothing extra
- 🔧 **Fully Transparent** - Complete source code visibility (proprietary license)
- 🌍 **Cross-Platform Ready** - Unified platform abstraction layer
- 🎵 **Built by Musicians** - Designed for the creative flow state

---

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    NOMAD Creative System                    │
├─────────────────────────────────────────────────────────────┤
│  NomadUI      │  Custom OpenGL Renderer + Component System  │
│  NomadPlat    │  Platform Abstraction (Win32/X11/Cocoa)     │
│  NomadAudio   │  RtAudio + DSP Engine (Planned)             │
│  NomadCore    │  Base Utilities (Math, I/O, Threading)      │
│  NomadSDK     │  Plugin System (Planned)                    │
└─────────────────────────────────────────────────────────────┘
```

### Module Status

| Module | Status | Version | Description |
|--------|--------|---------|-------------|
| **NomadCore** | ✅ Complete | v1.0.0 | Math, threading, file I/O, logging |
| **NomadPlat** | ✅ Complete | v1.0.0 | Platform abstraction with DPI support |
| **NomadUI** | ✅ Complete | v0.1.0 | OpenGL renderer + component system |
| **NomadAudio** | ⏳ Planned | - | RtAudio integration + DSP |
| **NomadSDK** | ⏳ Planned | - | Plugin system |

---

## 🛠️ Technology Stack

### Core Technologies

<table>
<tr>
<td width="50%">

**Graphics & Rendering**
- 🎨 **OpenGL 3.3+** - Hardware-accelerated rendering
- 🖼️ **GLAD** - OpenGL loader
- 📐 **NanoSVG** - SVG parsing and rendering
- 🔤 **FreeType** - High-quality text rendering
- ✨ **MSAA** - Multi-sample anti-aliasing

</td>
<td width="50%">

**Platform Layer**
- 🪟 **Win32 API** - Windows native windowing
- 🐧 **X11** - Linux support (planned)
- 🍎 **Cocoa** - macOS support (planned)
- 📏 **Per-Monitor DPI V2** - High-DPI display support
- ⌨️ **Native Input** - Direct keyboard/mouse handling

</td>
</tr>
<tr>
<td>

**Audio (Planned)**
- 🎵 **RtAudio** - Cross-platform audio I/O
- 🔊 **WASAPI** - Windows audio
- 🎚️ **CoreAudio** - macOS audio
- 🎛️ **ALSA/PipeWire** - Linux audio
- ⚡ **Lock-Free Buffers** - Real-time safe

</td>
<td>

**Build System**
- 🔨 **CMake 3.22+** - Cross-platform build
- 🏗️ **C++17** - Modern C++ features
- 📦 **Modular Design** - Independent libraries
- ✅ **Automated Testing** - Comprehensive test suite
- 🚀 **Optimized Builds** - Release & Debug configs

</td>
</tr>
</table>

### Dependencies

**External Libraries:**
- [FreeType](https://freetype.org/) - Font rendering (MIT License)
- [GLAD](https://glad.dav1d.de/) - OpenGL loader (MIT License)
- [NanoSVG](https://github.com/memononen/nanosvg) - SVG parsing (Zlib License)
- [RtAudio](https://www.music.mcgill.ca/~gary/rtaudio/) - Audio I/O (MIT License) *(Planned)*

**All other code is original and proprietary.**

---

## 🎨 Features

### NomadUI - Custom UI Framework

<table>
<tr>
<td width="33%">

**Rendering**
- OpenGL 3.3+ renderer
- MSAA anti-aliasing
- Custom shader pipeline
- 60 FPS target
- DPI-aware scaling

</td>
<td width="33%">

**Components**
- Buttons & Labels
- Sliders & Checkboxes
- Text Input
- Progress Bars
- Scrollbars
- Context Menus
- Custom Windows

</td>
<td width="33%">

**Theming**
- Dark/Light themes
- Color customization
- SVG icon system
- Color tinting
- Animation system
- Smooth transitions

</td>
</tr>
</table>

### NomadPlat - Platform Abstraction

- ✅ **Window Management** - Create, resize, fullscreen
- ✅ **Input Handling** - Mouse, keyboard, modifiers
- ✅ **DPI Awareness** - Per-Monitor V2 support
- ✅ **OpenGL Context** - Automatic setup
- ✅ **File Dialogs** - Native OS dialogs
- ✅ **Clipboard** - System clipboard access
- ✅ **High-Res Timer** - Precise timing

### NomadCore - Base Utilities

- ✅ **Math Library** - Vectors, matrices, DSP functions
- ✅ **Threading** - Lock-free structures, thread pools
- ✅ **File I/O** - Binary serialization, JSON parsing
- ✅ **Logging** - Multi-destination, stream-style
- ✅ **Assertions** - Debug validation

---

## 🎵 Audio System Features

#### ✅ **Sound Preview System**
- **5-second previews** when clicking on audio files in the file browser
- **Unique tone generation** based on filename hash for each file
- **Automatic preview management** - stops when selecting different files
- **Lower volume playback** (30%) to avoid startling users
- **Preview track hidden** from main track UI for clean interface

#### ✅ **Silent Startup**
- **No more startup sounds** - removed test tones from audio initialization
- **Clean application launch** with immediate silence
- **Professional audio behavior** - only plays when user initiates

#### ✅ **File Browser Integration**
- **Audio file detection** - automatically detects .wav, .mp3, .flac, .aiff files
- **Preview on selection** - click or use arrow keys to trigger preview
- **Visual feedback** - highlights selected files with cyan accent
- **Directory navigation** - double-click folders or use Enter keystream-style

---

## 🚀 Quick Start

### Prerequisites

```bash
# Windows
- Visual Studio 2022 or later
- CMake 3.22+
- Windows SDK 10.0+

# Linux (Planned)
- GCC 9+ or Clang 10+
- CMake 3.22+
- X11 development libraries

# macOS (Planned)
- Xcode 12+
- CMake 3.22+
```

### Build

```powershell
# Clone the repository
git clone https://github.com/currentsuspect/NOMAD.git
cd NOMAD

# Build (Windows)
.\build.ps1              # Normal build
.\build.ps1 -Clean       # Clean rebuild
.\build.ps1 -Help        # Show help

# Or manually
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target NOMAD_DAW
```

### Run

```bash
# Launch NOMAD DAW
.\build\bin\Release\NOMAD_DAW.exe
```

---

## 📁 Project Structure

```
NOMAD/
├── 📦 NomadCore/           # Base utilities
│   ├── include/            # Public headers
│   │   ├── NomadMath.h     # Vector, matrix, DSP math
│   │   ├── NomadThreading.h # Lock-free structures
│   │   ├── NomadFile.h     # File I/O, JSON
│   │   └── NomadLog.h      # Logging system
│   └── src/                # Implementation + tests
│
├── 🖥️ NomadPlat/           # Platform abstraction
│   ├── include/            # Platform interface
│   │   └── NomadPlatform.h # Window, input, DPI
│   └── src/
│       ├── Win32/          # Windows implementation
│       ├── X11/            # Linux (planned)
│       └── Cocoa/          # macOS (planned)
│
├── 🎨 NomadUI/             # UI framework
│   ├── Core/               # Component system
│   │   ├── NUIButton.h     # Button component
│   │   ├── NUILabel.h      # Label component
│   │   ├── NUISlider.h     # Slider component
│   │   └── ...             # More components
│   ├── Graphics/           # Rendering
│   │   ├── NUIRenderer.h   # Renderer interface
│   │   ├── NUIRendererGL.h # OpenGL implementation
│   │   └── NUISVGParser.h  # SVG support
│   ├── Platform/           # Platform bridge
│   │   └── NUIPlatformBridge.h
│   └── Examples/           # Demo applications
│
├── 🎵 NomadAudio/          # Audio engine (planned)
├── 🔌 NomadSDK/            # Plugin system (planned)
├── 📚 NomadDocs/           # Documentation
│   ├── DEVELOPER_GUIDE.md  # Development guide
│   ├── BUILD_STATUS.md     # Build status
│   └── BRANCHING_STRATEGY.md # Git workflow
│
└── 🛠️ Build Files
    ├── CMakeLists.txt      # Root build config
    ├── build.ps1           # Build script
    └── .gitignore          # Git ignore rules
```

---

## 📚 Documentation

### 📖 Getting Started
- **[Documentation Index](NomadDocs/README.md)** - Complete documentation guide
- **[Developer Guide](NomadDocs/DEVELOPER_GUIDE.md)** - Philosophy & architecture
- **[Build Status](NomadDocs/BUILD_STATUS.md)** - Current module status
- **[Branching Strategy](NomadDocs/BRANCHING_STRATEGY.md)** - Git workflow

### 🔧 Module Documentation
- **[NomadCore](NomadCore/README.md)** - Base utilities
- **[NomadPlat](NomadPlat/README.md)** - Platform abstraction
  - [DPI Support Guide](NomadPlat/docs/DPI_SUPPORT.md) - High-DPI implementation
- **[NomadUI](NomadUI/docs/)** - UI framework
  - [Platform Migration](NomadUI/docs/PLATFORM_MIGRATION.md) - Migration guide
  - [Architecture](NomadUI/docs/ARCHITECTURE.md) - UI architecture
  - [Custom Windows](NomadUI/docs/CUSTOM_WINDOW_INTEGRATION.md) - Window guide
  - [Icon System](NomadUI/docs/ICON_SYSTEM_GUIDE.md) - SVG icons
  - [Theme System](NomadUI/docs/THEME_DEMO_GUIDE.md) - Theming

---

## 🎯 Design Principles

<table>
<tr>
<td width="50%">

### Code Philosophy
- **Clarity before speed** - Readable, maintainable code
- **Intentional minimalism** - Nothing extra, everything essential
- **Depth through motion** - Animation defines hierarchy
- **Performance as aesthetic** - Fluidity = design quality

</td>
<td width="50%">

### The Nomad Code
- If it's slow, it's sacred
- No borrowed parts, no borrowed soul
- Simplify before optimizing
- Write for your future self
- Document as you build
- Elegance is correctness you can feel

</td>
</tr>
</table>

---

## 🗺️ Roadmap

| Phase | Description | Status |
|-------|-------------|--------|
| **v1.0** | Foundation: Core + Platform + UI | ✅ **Complete** |
| **v1.5** | Audio: RtAudio + DSP + Mixer | ⏳ In Progress |
| **v2.0** | DSP: Oscillators, Filters, Envelopes | 📋 Planned |
| **v3.0** | Plugins: SDK + Host + Modular DAW | 📋 Planned |
| **v∞** | Vision: Self-contained creative OS | 🌟 Future |

### v1.0 Foundation ✅ (Current)
- ✅ NomadCore - Math, threading, file I/O, logging
- ✅ NomadPlat - Platform abstraction with DPI support
- ✅ NomadUI - OpenGL renderer + component system
- ✅ Build system - CMake, modular architecture
- ✅ Documentation - Comprehensive guides

### v1.5 Audio Integration ⏳ (Next)
- [ ] RtAudio integration
- [ ] AudioDeviceManager
- [ ] Lock-free audio callback
- [ ] Basic mixer (gain, pan)
- [ ] <10ms latency target

### v2.0 DSP Foundation 📋
- [ ] Oscillators (sine, saw, square)
- [ ] Filters (low-pass, high-pass, band-pass)
- [ ] ADSR envelope generator
- [ ] Basic effects (reverb, delay)

### v3.0 Plugin System 📋
- [ ] Plugin API design
- [ ] VST3 host support
- [ ] Native plugin format
- [ ] Full modular DAW

---

## 🌍 Platforms

### ✅ Windows (Complete)
- Win32 API windowing
- WASAPI audio (planned)
- Per-Monitor DPI V2
- Full input support
- OpenGL 3.3+

### ⏳ Linux (Planned)
- X11 windowing
- ALSA/PipeWire audio
- Wayland support
- Full input support
- OpenGL 3.3+

### ⏳ macOS (Planned)
- Cocoa windowing
- CoreAudio
- Retina display support
- Full input support
- OpenGL 3.3+

---

## 🤝 Contributing

Interested in contributing? We welcome bug reports, feature suggestions, and pull requests!

### How to Contribute
1. Read the [Developer Guide](NomadDocs/DEVELOPER_GUIDE.md)
2. Follow the [Branching Strategy](NomadDocs/BRANCHING_STRATEGY.md)
3. Write clean, documented code
4. Test thoroughly
5. Submit with clear commit messages

### Contributor License Agreement
By contributing, you agree that all contributions become property of Dylan Makori.
See [LICENSING.md](LICENSING.md) for details.

### Contact
- **Email**: makoridylan@gmail.com
- **GitHub Issues**: [Report bugs](https://github.com/currentsuspect/NOMAD/issues)
- **Discussions**: [Feature requests](https://github.com/currentsuspect/NOMAD/discussions)

---

## 📜 License

**Proprietary Commercial License**

Copyright © 2025 Dylan Makori. All rights reserved.

NOMAD DAW is proprietary software. The source code is publicly visible for
transparency and educational purposes, but **use requires permission**.

- ✅ View source for educational purposes
- ✅ Report bugs and suggest features
- ✅ Submit pull requests (with CLA)
- ❌ Use without permission
- ❌ Copy, modify, or distribute
- ❌ Create derivative works

See [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md) for complete terms.

**For licensing inquiries:** makoridylan@gmail.com

---

## 🙏 Acknowledgments

### Technologies Used
- **FreeType** - Beautiful text rendering
- **GLAD** - OpenGL loading made easy
- **NanoSVG** - Lightweight SVG parsing
- **CMake** - Cross-platform build system

### Inspiration
Built by musicians, for musicians. Inspired by the belief that software can be
both powerful and intentional, complex yet elegant, professional yet personal.

---

## 📊 Stats

```
Lines of Code:    ~15,000+ (hand-written)
Modules:          5 (3 complete, 2 planned)
Components:       12+ UI components
Tests:            Comprehensive test suite
Documentation:    25+ documentation files
Platforms:        Windows (complete), Linux/macOS (planned)
Dependencies:     Minimal (FreeType, GLAD, NanoSVG)
License:          Proprietary
```

---

<div align="center">

**NOMAD DAW**

*Built from scratch. Perfected with intention.*

*Crafted with ❤️ in 🇰🇪*

[Documentation](NomadDocs/README.md) • [License](LICENSE) • [Contact](mailto:makoridylan@gmail.com)

---

*"Create like silence is watching."*

# Test commit for GPG verification

</div>
