# 🜂 NOMAD DEVELOPER GUIDE

## Welcome

You are stepping into a system built by hand. Every pixel and buffer here exists because someone decided it should. There is no automation without understanding, no shortcut without intent.

When you contribute to NOMAD, write like it will be read decades from now. Build like silence is watching.

---

## 1. Project Identity

**Project Name:** NOMAD  
**Nature:** Independent DAW and creative framework  
**Philosophy:** "Built from scratch. Perfected with intention."  
**Mission:** To craft a hand-engineered, visually and sonically alive environment — one that feels human, timeless, and optimized for creation.

---

## 2. Architectural Overview

NOMAD is built in layers, each with a single responsibility:

| Layer | Purpose | Technology |
|-------|---------|------------|
| **NomadCore** | Base utilities: math, file I/O, threading, logging, serialization | C++20 |
| **NomadPlat** | Cross-platform OS layer: windowing, input, timing, platform abstraction | C++ / Win32 / X11 / Cocoa |
| **NomadUI** | Rendering engine, theming, layout, animation, and component system | C++ / OpenGL / Vulkan |
| **NomadAudio** | Real-time audio engine using RtAudio for cross-platform I/O | C++ |
| **NomadSDK** | Plugin/extension API for third-party or internal modules | C++ |
| **NomadAssets** | Fonts, icons, shaders, sounds — all handmade and owned | — |
| **NomadDocs** | Documentation, philosophy, and design guides | Markdown |

### Repository Structure

```
Nomad/
├── NomadCore/      # Base utilities
├── NomadPlat/      # Platform abstraction
│   ├── src/Win32/
│   ├── src/X11/
│   ├── src/Cocoa/
│   └── Shared/
├── NomadUI/        # Rendering & UI
│   ├── Core/
│   ├── Graphics/
│   └── Platform/
├── NomadAudio/     # Audio engine
│   ├── include/
│   └── src/
├── NomadSDK/       # Plugin system
├── NomadAssets/    # All assets
└── NomadDocs/      # Documentation
```

---

## 3. Development Roadmap

### NOMAD v1.0 (Current)
- ✅ NomadUI rendering engine complete
- ⏳ NomadPlat platform abstraction
- ⏳ NomadAudio with RtAudio backend
- ✅ No external framework dependencies (JUCE removed)

### NOMAD v1.5
- Add NomadMixer and DSP modules
- Implement basic effects (EQ, compression, reverb)

### NOMAD v2.0
- Custom audio drivers (ASIO, CoreAudio, PipeWire)
- Advanced DSP processing

### NOMAD v3.0
- Plugin host (VST3, AU, CLAP)
- Full modular DAW architecture

### NOMAD v∞
- Self-contained creative operating system

---

## 4. Technical Standards

### Code Style

**Language:** C++20 or later

**Naming Conventions:**
- Classes: `PascalCase` (e.g., `AudioEngine`)
- Methods: `camelCase` (e.g., `processAudio()`)
- Variables: `camelCase` (e.g., `sampleRate`)
- Constants: `UPPER_SNAKE_CASE` (e.g., `MAX_BUFFER_SIZE`)

**Formatting:**
- 4-space indentation (no tabs)
- Opening braces on same line
- Use RAII and smart pointers (`std::unique_ptr`, `std::shared_ptr`)

**Example:**
```cpp
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();
    
    bool initialize();
    void processAudio(float* buffer, int numSamples);
    
private:
    std::unique_ptr<Impl> impl_;
};
```

### Performance Targets

- **UI:** 60 FPS minimum
- **Audio:** <10 ms latency
- **Memory:** Optimized for 4 GB RAM systems
- **Allocations:** Avoid heap allocations in real-time threads

### Architectural Rules

1. **Respect layer boundaries** - Don't mix UI, audio, and platform code
2. **Platform abstraction** - All OS-specific code lives in NomadPlat
3. **No blocking in audio thread** - Use lock-free queues for communication
4. **Document everything** - Doxygen headers for all public APIs
5. **⚠️ CRITICAL: NomadUI uses absolute coordinates** - See [NOMADUI_COORDINATE_SYSTEM.md](NOMADUI_COORDINATE_SYSTEM.md) before positioning components

---

## 5. Audio System Design

### RtAudio Backend

NOMAD uses [RtAudio](https://www.music.mcgill.ca/~gary/rtaudio/) for cross-platform audio I/O:

**Supported APIs:**
- Windows: WASAPI, ASIO
- macOS: CoreAudio
- Linux: ALSA, JACK, PipeWire

**License:** MIT (commercial-safe)

### Audio Flow

```
RtAudioCallback → NomadAudioEngine → MixerBus → OutputBuffer
```

### NomadAudio Structure

```
NomadAudio/
├── include/
│   ├── NomadAudio.h
│   ├── AudioDeviceManager.h
│   ├── Mixer.h
│   ├── DSP/
│   │   ├── Filters.h
│   │   ├── Oscillator.h
│   │   └── Envelope.h
│   └── Utils/
│       ├── RingBuffer.h
│       └── AudioMath.h
└── src/
    ├── RtAudioBackend.cpp
    ├── Mixer.cpp
    └── DSP/
```

---

## 6. Platform Layer (NomadPlat)

### Responsibilities

- Window creation and management
- OpenGL/Vulkan context setup
- Input events (keyboard, mouse, touch)
- File dialogs and system integration
- High-resolution timers

### Platform Support

- **Windows:** Win32 API
- **Linux:** X11 (Wayland planned)
- **macOS:** Cocoa

---

## 7. Design Principles

### Core Values

- **Intentional Minimalism** - Nothing extra, everything essential
- **Depth Through Motion** - Animation defines hierarchy
- **Performance as Aesthetic** - Fluidity = design quality
- **Analog Feel** - Subtle imperfections give realism
- **Clarity Over Decoration** - Spacing and light over gloss
- **Silence and Soul** - Design should evoke focus, not noise

> "NOMAD should look like it was designed in silence, but feels loud inside."

### The NOMAD Code

Values for every contributor:

- **Clarity before speed** - Readable code is maintainable code
- **If it's slow, it's sacred** - Take time to do it right
- **No borrowed parts, no borrowed soul** - Build from scratch
- **Simplify before optimizing** - Make it work, then make it fast
- **Write for your future self** - Document as you build
- **Elegance is correctness you can feel** - Beauty in function

---

## 8. Contributing

### Getting Started

1. Read this guide completely
2. Review [BRANCHING_STRATEGY.md](BRANCHING_STRATEGY.md)
3. Check subsystem READMEs:
   - [NomadCore/README.md](../NomadCore/README.md)
   - [NomadPlat/README.md](../NomadPlat/README.md)
   - [NomadUI/README.md](../NomadUI/README.md)
   - [NomadAudio/README.md](../NomadAudio/README.md)

### Workflow

1. **Branch from `develop`**
   ```bash
   git checkout develop
   git checkout -b feature/your-feature-name
   ```

2. **Follow commit format**
   ```
   feat: add audio device enumeration
   
   - Implement device discovery for WASAPI
   - Add device info structure
   - Create unit tests
   
   Architecture: NomadAudio
   ```

3. **Test thoroughly** - Ensure no regressions

4. **Document changes** - Update relevant READMEs

5. **Merge to develop** - After review

### Code Review Checklist

- [ ] Code compiles without warnings
- [ ] No regression in existing features
- [ ] Architecture notes updated
- [ ] Documentation added/updated
- [ ] Performance targets met

---

## 9. Building NOMAD

### Requirements

- CMake 3.22+
- C++20 compiler
- Visual Studio 2022 (Windows) / GCC 11+ (Linux) / Clang 13+ (macOS)

### Quick Build

```bash
.\build.ps1          # Windows
./build.sh           # Linux/macOS (coming soon)
```

### Manual Build

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

---

## 10. Resources

### Documentation

- [BRANCHING_STRATEGY.md](BRANCHING_STRATEGY.md) - Git workflow
- [BUILD_STATUS.md](BUILD_STATUS.md) - Current build state
- Subsystem READMEs - Layer-specific guides

### External Dependencies

- **RtAudio** - Audio I/O (MIT license)
- **FreeType** - Font rendering (FreeType license)
- **nanosvg** - SVG parsing (zlib license)
- **GLAD** - OpenGL loader (MIT license)

All dependencies are commercial-safe and properly attributed.

---

## 11. Philosophy

### Creation Ritual

1. **Concept** - Define emotion + purpose first
2. **Prototype** - Build fast, prove logic
3. **Refine** - Optimize, polish visuals
4. **Ritualize** - Document and lock into standard

### Why NOMAD?

NOMAD exists because we believe software should be:
- **Intentional** - Every decision has purpose
- **Timeless** - Built to last decades
- **Human** - Feels alive, not mechanical
- **Owned** - No borrowed parts, no compromises

We're not building another DAW. We're building a creative operating system that respects the craft of music production.

---

## 12. License

Proprietary. All rights reserved.

For licensing inquiries, contact the NOMAD development team.

---

**Welcome to NOMAD.**

*Build like silence is watching.*
