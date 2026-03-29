# 🧭 Aestra Architecture Overview

![Architecture](https://img.shields.io/badge/Architecture-Modular-blue)
![C++17](https://img.shields.io/badge/C%2B%2B-17-orange)

Comprehensive overview of Aestra's modular architecture, covering Core, UI, Audio, and Platform systems.

## 📋 Table of Contents

- [System Overview](#-system-overview)
- [Core Modules](#-core-modules)
- [Architecture Principles](#-architecture-principles)
- [Data Flow](#-data-flow)
- [Threading Model](#-threading-model)

## 🏗️ System Overview

Aestra is built with a clean, modular architecture that separates concerns into distinct subsystems:

```
┌─────────────────────────────────────────────────────────┐
│                    Aestra Application                     │
└─────────────────────────────────────────────────────────┘
            │                 │
      ┌─────┴─────┐     ┌─────┴─────┐
      │  AestraUI  │     │ AestraAudio│
      │ Framework │     │   Engine  │
      └─────┬─────┘     └─────┬─────┘
            │                 │
      ┌─────┴─────────────────┴─────┐
      │          AestraCore            │
      │  (Platform abstraction,       │
      │   utilities, types)            │
      └───────────────────────────────┘
            │                 │
      ┌─────┴─────┐     ┌─────┴─────┐
      │  Windows  │     │   Linux   │
      │  Platform │     │  Platform │
      └───────────┘     └───────────┘
```

## 🧩 Core Modules

### AestraCore

**Purpose**: Foundation layer providing platform abstraction, utilities, and common types.

**Location**: `Aestra-core/`, `AestraCore/`

**Key Components:**
- **Platform Abstraction** - OS-specific functionality (file I/O, threading, memory)
- **Math Utilities** - Vector math, coordinate transformations
- **Data Structures** - Custom containers optimized for audio processing
- **Type Definitions** - Common types used across all modules
- **Memory Management** - Custom allocators for real-time audio

**Public API:**
```cpp
namespace Aestra {
    namespace core {
        // Platform abstraction
        class FileSystem;
        class Thread;
        class Mutex;

        // Utilities
        class Logger;
        class Timer;
    }
}
```

### AestraUI

**Purpose**: GPU-accelerated custom UI framework with immediate-mode rendering.

**Location**: `AestraUI/`

**Key Components:**
- **Rendering Engine** - OpenGL-based rendering with adaptive FPS
- **Widget System** - Buttons, sliders, text fields, dropdowns
- **Layout Engine** - Flexible layout with absolute and relative positioning
- **Event System** - Mouse, keyboard, and custom events
- **Coordinate System** - Hierarchical coordinate transformations

**Architecture:**
```cpp
AestraUI/
├── Core/           # Core UI framework
│   ├── NUIWidget   # Base widget class
│   ├── NUIWindow   # Window management
│   └── NUIRenderer # OpenGL rendering
├── Widgets/        # Standard UI widgets
│   ├── NUIButton
│   ├── NUISlider
│   ├── NUITextBox
│   └── NUIDropdown
└── Layout/         # Layout system
    ├── NUILayout
    └── NUIConstraints
```

**Key Features:**
- **Adaptive FPS**: Dynamically adjusts frame rate (1-120 FPS) based on activity
- **GPU Acceleration**: Hardware-accelerated rendering for smooth 60+ FPS
- **Immediate Mode**: Simplified widget state management
- **Custom Drawing**: Direct OpenGL access for custom visualizations

### AestraAudio

**Purpose**: Professional audio engine with ultra-low latency processing.

**Location**: `AestraAudio/`

**Key Components:**
- **Audio Driver System** - WASAPI (Windows), ALSA (Linux), CoreAudio (macOS)
- **Track Management** - Multi-track audio with sample-accurate timing
- **Buffer Management** - Lock-free ring buffers for real-time audio
- **Sample Loading** - Lazy-loading with waveform caching
- **Mixing Engine** - 64-bit floating-point audio mixing

**Architecture:**
```cpp
AestraAudio/
├── Drivers/
│   ├── AudioDriver         # Abstract driver interface
│   ├── WASAPIDriver        # Windows WASAPI implementation
│   └── ALSADriver          # Linux ALSA implementation
├── Core/
│   ├── AudioEngine         # Main audio engine
│   ├── Track               # Individual audio track
│   ├── AudioBuffer         # Lock-free buffer
│   └── SampleCache         # Waveform caching
└── Processing/
    ├── Mixer               # Audio mixing
    └── Effects             # Audio effects (future)
```

**Key Features:**
- **WASAPI Integration**: Exclusive and Shared mode support
- **Multi-tier Fallback**: Automatic driver selection for compatibility
- **Sample-accurate Timing**: Precise audio playback and synchronization
- **Lock-free Audio Thread**: Zero-latency audio processing
- **64-bit Processing**: High-quality 64-bit floating-point audio

### Muse AI — Post-Beta

AI features are explicitly deferred until after v1 Beta. The roadmap cuts AI features from Beta scope to focus on core DAW stability. Muse integration will resume only after the base product is shippable.

### AestraPlat

**Purpose**: Platform-specific implementations and windowing.

**Location**: `AestraPlat/`

**Key Components:**
- **Window Management** - Native window creation and handling
- **Input Handling** - Keyboard, mouse, and touch input
- **System Integration** - System dialogs, notifications
- **OpenGL Context** - Graphics context creation

## 🎯 Architecture Principles

### 1. Separation of Concerns

Each module has a clear, focused responsibility:
- **AestraCore**: Platform abstraction and utilities
- **AestraUI**: User interface rendering and interaction
- **AestraAudio**: Audio processing and I/O
- **AestraPlat**: Platform-specific implementations

### 2. Dependency Hierarchy

```
Application
    ↓
AestraUI + AestraAudio + Muse
    ↓
AestraCore
    ↓
AestraPlat (Platform Layer)
    ↓
OS APIs (Windows, Linux, macOS)
```

**Rules:**
- Higher layers depend on lower layers
- Lower layers never depend on higher layers
- Core has minimal dependencies
- Platform layer is the only one touching OS APIs

### 3. Interface-based Design

**Abstract interfaces** allow for multiple implementations:

```cpp
// Abstract audio driver interface
class AudioDriver {
public:
    virtual bool initialize(int sampleRate, int bufferSize) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual ~AudioDriver() = default;
};

// Concrete implementations
class WASAPIDriver : public AudioDriver { ... };
class ALSADriver : public AudioDriver { ... };
```

### 4. Real-time Constraints

**Audio thread is lock-free:**
- No memory allocations in audio callback
- No mutexes or blocking operations
- Lock-free data structures for communication
- Fixed-size buffers allocated upfront

### 5. Data-Oriented Design

**Cache-friendly data layouts** for performance:
- Contiguous arrays for batch processing
- Struct-of-arrays (SoA) where beneficial
- Minimal pointer chasing
- Hot/cold data separation

## 🔄 Data Flow

### Audio Processing Pipeline

```
[Audio Files]
     ↓
[Sample Loader] → [Sample Cache]
     ↓                    ↓
[Track Manager] → [Waveform Cache]
     ↓
[Audio Mixer]
     ↓
[Audio Driver] → [WASAPI/ALSA]
     ↓
[Hardware Output]
```

### UI Rendering Pipeline

```
[User Input]
     ↓
[Event Handler]
     ↓
[Widget Tree] → [Layout Engine]
     ↓
[OpenGL Renderer]
     ↓
[Display Output]
```

### Complete System Flow

```
┌────────────┐     ┌─────────────┐     ┌──────────────┐
│   User     │────→│   AestraUI   │────→│ Application  │
│   Input    │     │   Events    │     │   Logic      │
└────────────┘     └─────────────┘     └──────┬───────┘
                                              │
                   ┌──────────────────────────┘
                   │
        ┌──────────┴──────────┐
        ↓                     ↓
┌───────────────┐     ┌──────────────┐
│  UI Updates   │     │ Audio Engine │
│  (Main Thread)│     │(Audio Thread)│
└───────┬───────┘     └──────┬───────┘
        │                    │
        ↓                    ↓
┌───────────────┐     ┌──────────────┐
│   Renderer    │     │ Audio Driver │
└───────────────┘     └──────────────┘
```

## 🧵 Threading Model

### Thread Architecture

Aestra uses a **multi-threaded architecture** with strict thread separation:

```
┌─────────────────┐
│   Main Thread   │  UI, event handling, application logic
├─────────────────┤
│  Audio Thread   │  Real-time audio processing (lock-free)
├─────────────────┤
│  Loader Thread  │  Async file I/O, sample loading
├─────────────────┤
│  Render Thread  │  GPU rendering (optional, future)
└─────────────────┘
```

### Thread Responsibilities

#### Main Thread (UI Thread)
- **UI rendering and events**
- **User input handling**
- **Application state management**
- **Non-realtime operations**

**Priority**: Normal

#### Audio Thread
- **Audio buffer processing**
- **Mixing and effects**
- **Driver I/O**
- **Sample-accurate timing**

**Priority**: Real-time (highest)
**Constraints**: Lock-free, no allocations, no blocking

#### Loader Thread
- **File I/O** (loading samples)
- **Waveform caching**
- **Background processing**
- **Resource management**

**Priority**: Background (low)

### Thread Communication

**Lock-free communication** between threads:

```cpp
// Main → Audio: Commands via lock-free queue
LockFreeQueue<AudioCommand> commandQueue;

// Audio → Main: State updates via atomic flags
std::atomic<bool> isPlaying;
std::atomic<int> currentPosition;

// Example: Start playback
commandQueue.push(AudioCommand::Start);  // Main thread
// Audio thread processes command in callback
```

### Synchronization Primitives

- **std::atomic**: For simple flags and counters
- **Lock-free queues**: For command passing
- **Ring buffers**: For audio data
- **Mutexes**: Only in non-realtime paths

## 📊 Performance Characteristics

### AestraUI
- **Frame rate**: Adaptive 1-120 FPS
- **Typical rate**: 60 FPS during interaction, 1 FPS idle
- **Render time**: ~1-2ms per frame at 1080p

### AestraAudio
- **Latency**: ~5-10ms (WASAPI Exclusive)
- **Buffer size**: 256-512 samples (typical)
- **CPU usage**: ~5-10% per track (64-bit processing)
- **Jitter**: <0.1ms (sample-accurate)

### Memory
- **Waveform cache**: 4096 samples per visible region
- **Audio buffers**: Fixed-size, pre-allocated
- **UI widgets**: Dynamic allocation on creation only

## 🔐 Security Considerations

### Public vs Private Code

**Public (`Aestra-core/`):**
- Core audio engine
- UI framework
- Platform abstractions
- Build with mock assets

**Private (not in public repo):**
- Premium plugins
- AI models (Muse internals)
- Licensing system
- Code signing

### Security Measures

- **Git hooks**: Prevent committing secrets
- **Gitleaks**: Scan for exposed credentials
- **`.gitignore`**: Block sensitive files
- **Pre-commit validation**: Check for private folders

## 📚 Module Dependencies

```
┌──────────────────────────────────────────────┐
│              Source/ (Application)           │
└──────────────────────────────────────────────┘
        ↓                ↓              ↓
┌──────────────┐  ┌──────────────┐  ┌──────────┐
│   AestraUI    │  │  AestraAudio  │  │   Muse   │
└──────────────┘  └──────────────┘  └──────────┘
        ↓                ↓              ↓
┌──────────────────────────────────────────────┐
│              AestraCore / Aestra-core          │
└──────────────────────────────────────────────┘
        ↓                ↓              ↓
┌──────────────┐  ┌──────────────┐  ┌──────────┐
│  AestraPlat   │  │  Windows API │  │ Linux API│
│ (Win/Linux)  │  │   (WASAPI)   │  │  (ALSA)  │
└──────────────┘  └──────────────┘  └──────────┘
```

## 🔮 Future Architecture Plans

### v1 Beta Focus (Dec 2026)

1. **Internal Arsenal Plugins** — Built-in instruments (Rumble) validated through discovery, persistence, and audible playback
2. **Recording Reliability** — Multi-track audio recording with device stress testing
3. **Offline Render/Export** — Bounce that matches playback
4. **Plugin Decision Gate** — VST3/CLAP hosting ships only if it can be made boringly stable (Phase 4, Sep 2026)

### Post-Beta (Deferred)

- **Muse AI Integration** — AI-powered music generation (post-Beta only)
- **Video Support** — Video timeline and scoring
- **MIDI Support** — Full MIDI I/O and routing
- **Automation** — Parameter automation system

### Scalability

- **Multi-core audio** - Parallel track processing
- **GPU compute** - GPU-accelerated effects
- **Distributed rendering** - Network rendering (future)

## 📚 Additional Resources

- [Building Guide](../BUILD.md) - How to build Aestra
- [Coding Style](developer/coding-style.md) - Code conventions
- [Contributing](../CONTRIBUTING.md) - How to contribute

---

[← Return to Aestra Docs Index](../README.md)
