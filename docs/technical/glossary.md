# 📖 Nomad DAW Glossary

![Glossary](https://img.shields.io/badge/Glossary-Technical%20Terms-blue)

Comprehensive glossary of technical terms, acronyms, and concepts used in Nomad DAW development.

## 📋 Table of Contents

- [Audio Terms](#-audio-terms)
- [Programming Concepts](#-programming-concepts)
- [Nomad-Specific Terms](#-nomad-specific-terms)
- [UI/UX Terms](#-uiux-terms)
- [Acronyms](#-acronyms)

## 🎵 Audio Terms

### Audio Buffer
A fixed-size block of audio samples processed together. Smaller buffers reduce latency but increase CPU usage. Typical sizes: 128, 256, 512, or 1024 samples.

### Audio Driver
Software interface between application and audio hardware. Examples: WASAPI, ASIO, ALSA, CoreAudio.

### ASIO (Audio Stream Input/Output)
Low-latency audio driver protocol developed by Steinberg. Widely used in professional audio applications (planned for Nomad).

### Bit Depth
Number of bits used to represent each audio sample. Common values:
- **16-bit**: CD quality
- **24-bit**: Professional audio
- **32-bit float**: Internal processing

### Buffer Size
Number of samples in an audio buffer. Affects latency and CPU usage:
- **Small (64-256)**: Low latency, high CPU
- **Medium (256-512)**: Balanced
- **Large (512-1024+)**: High latency, low CPU

### DAW (Digital Audio Workstation)
Software application for recording, editing, mixing, and producing audio content. Nomad is a DAW.

### Exclusive Mode
Audio driver mode with direct hardware access (WASAPI Exclusive). Provides lowest latency but prevents other applications from using audio device.

### Latency
Delay between input and output in audio processing. Lower is better for real-time performance. Measured in milliseconds (ms).

### Lock-Free
Programming technique that avoids mutexes/locks in real-time audio threads. Prevents priority inversion and ensures consistent timing.

### Sample
Single audio value at a specific point in time. At 44100 Hz, there are 44,100 samples per second.

### Sample Rate
Number of samples per second in digital audio. Common rates:
- **44100 Hz**: CD quality
- **48000 Hz**: Professional video/audio
- **88200 Hz**: High resolution
- **96000 Hz**: Studio quality

### Shared Mode
Audio driver mode that shares hardware access with other applications (WASAPI Shared). Higher latency than Exclusive mode.

### WASAPI (Windows Audio Session API)
Modern audio API for Windows Vista and later. Supports both Exclusive and Shared modes.

### Waveform
Visual representation of audio amplitude over time. Nomad caches waveforms for efficient rendering.

## 💻 Programming Concepts

### Atomic Operation
Operation that completes without interruption. Used for lock-free thread communication. Example: `std::atomic<bool>`.

### Cache Coherency
Ensuring consistent data across CPU caches in multi-threaded programs. Important for audio thread communication.

### clang-format
Tool for automatically formatting C++ code according to a style guide. Nomad uses clang-format for consistent code style.

### CMake
Cross-platform build system generator. Nomad uses CMake for configuring and building the project.

### Continuation Indent
Indentation for line continuations (wrapped lines). Nomad uses 4 spaces.

### CRTP (Curiously Recurring Template Pattern)
C++ template pattern where a derived class passes itself as a template parameter to its base class.

### Data-Oriented Design
Programming approach that organizes data for cache efficiency. Focuses on data layout and access patterns.

### Immediate Mode
UI paradigm where widgets are recreated each frame. Simplifies state management. Used in NomadUI.

### Lock-Free Queue
Thread-safe queue implementation without mutexes. Used for command passing between threads in Nomad.

### Move Semantics
C++11 feature for transferring ownership of resources without copying. Uses `std::move()`.

### RAII (Resource Acquisition Is Initialization)
C++ programming idiom where resource lifetime is tied to object lifetime. Resources released automatically in destructor.

### Real-Time
Requirement that operations complete within strict time constraints. Audio processing must be real-time to avoid glitches.

### Ring Buffer
Circular buffer used for lock-free data transfer between threads. Fixed size, overwrites oldest data when full.

### Smart Pointer
C++ object that automatically manages memory. Types:
- `std::unique_ptr`: Exclusive ownership
- `std::shared_ptr`: Shared ownership
- `std::weak_ptr`: Non-owning reference

### Template
C++ feature for generic programming. Allows functions and classes to work with any type.

### Thread Safety
Property where code can be safely called from multiple threads without race conditions or data corruption.

## 🎯 Nomad-Specific Terms

### Adaptive FPS
NomadUI feature that dynamically adjusts frame rate (1-120 FPS) based on user activity to conserve CPU.

### Core Mode
Build configuration (`NOMAD_CORE_MODE=ON`) that excludes private/premium features. Required for public contributors.

### Muse
Nomad's AI-powered music generation and production assistant (in private development).

### NomadAudio
Nomad's audio engine module responsible for playback, recording, and processing.

### NomadCore
Foundation module providing platform abstraction, utilities, and common types.

### NomadPlat
Platform-specific implementations for Windows, Linux, and macOS.

### NomadUI
Custom GPU-accelerated UI framework built specifically for Nomad DAW.

### Premium Features
Proprietary features available only in paid version (AI models, advanced effects, licensing).

### Public Build
Build from public repository with core features only. No premium features or private assets.

### Sample Clip
Visual representation of an audio sample on the timeline. Can be moved, resized, and edited.

### Track Manager
Component that manages multiple audio tracks, playback state, and mixing.

### Transport Bar
UI component containing playback controls (play, pause, stop, record).

## 🖥️ UI/UX Terms

### Component
Self-contained UI element with its own rendering and event handling. Examples: button, slider, dropdown.

### Coordinate System
System for positioning UI elements. NomadUI uses hierarchical coordinates (parent-relative).

### Culling
Optimization technique that skips rendering elements outside visible area. Nomad uses 200px culling padding.

### Event Handling
Process of responding to user input (mouse, keyboard). Events propagate through widget hierarchy.

### Layout Engine
System for automatically positioning and sizing UI elements based on constraints.

### OpenGL
Cross-platform graphics API used by NomadUI for GPU-accelerated rendering.

### Playhead
Vertical line indicator showing current playback position on the timeline.

### Render Loop
Continuous cycle that updates and draws UI. Nomad's adaptive FPS adjusts loop frequency.

### Ruler
UI component showing time divisions (bars, beats) on the timeline.

### Timeline
Horizontal representation of time where audio clips are arranged and edited.

### Viewport
Visible portion of a larger scrollable area. Timeline viewport shows part of full project.

### Widget
Basic UI element (button, slider, label). Building block of the UI system.

### Z-Order
Rendering order of overlapping UI elements. Higher z-order renders on top.

## 🔤 Acronyms

### ALSA
**Advanced Linux Sound Architecture** - Linux audio driver system.

### API
**Application Programming Interface** - Set of functions and types for using a library or system.

### BPM
**Beats Per Minute** - Tempo measurement in music.

### CI/CD
**Continuous Integration / Continuous Deployment** - Automated testing and deployment.

### CLA
**Contributor License Agreement** - Legal agreement for code contributions.

### CPU
**Central Processing Unit** - Main processor in a computer.

### DAW
**Digital Audio Workstation** - Software for audio production.

### DPI
**Dots Per Inch** - Display resolution measurement.

### DSP
**Digital Signal Processing** - Mathematical manipulation of signals.

### FPS
**Frames Per Second** - Refresh rate of visual display.

### GPU
**Graphics Processing Unit** - Specialized processor for graphics rendering.

### GUI
**Graphical User Interface** - Visual interface for interacting with software.

### IDE
**Integrated Development Environment** - Software for code editing and debugging (e.g., Visual Studio).

### I/O
**Input/Output** - Data transfer between system and external devices.

### JUCE
**Jules' Utility Class Extensions** - C++ framework for audio applications (not used in Nomad, but referenced).

### MIDI
**Musical Instrument Digital Interface** - Protocol for communicating musical data (planned for Nomad).

### ML
**Machine Learning** - AI technique used in Muse.

### MSVC
**Microsoft Visual C++** - Microsoft's C++ compiler.

### PR
**Pull Request** - Proposed code changes on GitHub.

### RAII
**Resource Acquisition Is Initialization** - C++ programming idiom.

### RAM
**Random Access Memory** - Computer memory for active data.

### SDK
**Software Development Kit** - Tools and libraries for development.

### SoA
**Struct of Arrays** - Data layout technique for performance.

### UI
**User Interface** - Visual elements for user interaction.

### UX
**User Experience** - Overall experience of using software.

### VST
**Virtual Studio Technology** - Plugin format for audio effects and instruments (planned for Nomad).

### WASAPI
**Windows Audio Session API** - Modern Windows audio API.

## 📚 Related Terms

### Branching Strategy
Git workflow defining how branches are created, named, and merged. See [Contributing Guide](../CONTRIBUTING.md).

### Code Review
Process of examining code changes before merging. Ensures quality and maintainability.

### Commit Message
Description of changes in a Git commit. Nomad uses conventional commit format (`feat:`, `fix:`, etc.).

### Dependency Injection
Design pattern where dependencies are provided to an object rather than created internally.

### Git Hook
Script that runs automatically on Git events (e.g., pre-commit). Nomad uses hooks for code formatting.

### Integration Testing
Testing multiple components together to verify they work correctly as a system.

### Refactoring
Restructuring code without changing its external behavior. Improves maintainability.

### Regression
Bug that reappears after being fixed, or new bug introduced by changes.

### Technical Debt
Accumulated cost of suboptimal design decisions. Paid back through refactoring.

### Unit Test
Test that verifies a single component in isolation. Helps catch bugs early.

## 💡 Usage Examples

**In conversation:**
- "Increase the **buffer size** to reduce CPU usage."
- "Use **WASAPI Exclusive mode** for lowest **latency**."
- "The **audio thread** must be **lock-free** for **real-time** performance."
- "NomadUI uses **adaptive FPS** to conserve CPU when idle."
- "Public contributors build in **Core Mode** without **premium features**."

## 📚 Additional Resources

- [Architecture Overview](../ARCHITECTURE.md) - Detailed system design
- [Building Guide](../BUILDING.md) - Build instructions
- [Coding Style Guide](../CODING_STYLE.md) - Code conventions
- [FAQ](../FAQ.md) - Common questions

---

[← Return to Nomad Docs Index](../README.md)
