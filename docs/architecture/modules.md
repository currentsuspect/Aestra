# Module Hierarchy

This page details the complete module hierarchy and dependencies in Aestra.

## 🏗️ Dependency Graph

```mermaid
graph TB
    subgraph Application
        DAW[Aestra]
    end
    
    subgraph Framework
        UI[AestraUI]
        Audio[AestraAudio]
    end
    
    subgraph Platform
        Plat[AestraPlat]
    end
    
    subgraph Core
        Core[AestraCore]
    end
    
    DAW --> UI
    DAW --> Audio
    
    UI --> Plat
    Audio --> Plat
    
    Plat --> Core
    
    style DAW fill:#5c6bc0,color:#fff
    style UI fill:#00d9ff,color:#000
    style Audio fill:#00d9ff,color:#000
    style Plat fill:#7e57c2,color:#fff
    style Core fill:#9575cd,color:#fff
```

## 📦 Module Details

### Layer 0: AestraCore (Foundation)

**Purpose:** Provides fundamental utilities and data structures used throughout Aestra.

**Status:** ✅ Complete

**Dependencies:** None (zero external dependencies)

**Key Components:**
- Math library (vectors, matrices, interpolation)
- Threading primitives (lock-free queues, thread pools)
- File I/O system (cross-platform file operations)
- Logging system (structured logging)
- Memory management (custom allocators)
- String utilities (UTF-8, formatting)

**Public API:**
```cpp
#include "AestraCore/Math/Vector.h"
#include "AestraCore/Threading/ThreadPool.h"
#include "AestraCore/IO/File.h"
#include "AestraCore/Logging/Logger.h"
```


---

### Layer 1: AestraPlat (Platform Abstraction)

**Purpose:** Provides unified API for platform-specific operations.

**Status:** ✅ Complete (Windows), 🚧 Pre-beta (Linux), 📅 Post-Beta (macOS)

**Dependencies:** AestraCore

**Key Components:**
- Window management (create, resize, event handling)
- Input system (keyboard, mouse, touch)
- File dialogs (open/save)
- System information (CPU, memory, OS)
- High-resolution timers

**Supported Platforms:**
| Platform | Status | Implementation |
|----------|--------|----------------|
| Windows 10/11 | ✅ Complete | Win32 API |
| Linux | 🚧 Pre-beta | Window, input and portal file dialogs |
| macOS | 📅 Post-Beta | Cocoa | Deferred until after v1 Beta |

**Public API:**
```cpp
#include "AestraPlat/Window.h"
#include "AestraPlat/Input.h"
#include "AestraPlat/FileDialog.h"
```


---

### Layer 2: Framework Modules

#### AestraUI (GPU-Accelerated Renderer)

**Purpose:** Custom OpenGL-based UI framework with modern features.

**Status:** ✅ Complete

**Dependencies:** AestraCore, AestraPlat

**Key Components:**
- OpenGL 3.3+ renderer
- NanoVG vector graphics
- Layout engine (flexbox-inspired)
- Widget library (buttons, sliders, inputs)
- Theme system (dark/light modes)
- SVG icon system
- Animation system

**Performance:**
- Adaptive FPS: 24-60 FPS
- MSAA anti-aliasing
- Texture atlasing
- Dirty rectangle optimization

**Public API:**
```cpp
#include "AestraUI/Renderer.h"
#include "AestraUI/Widget.h"
#include "AestraUI/Layout.h"
#include "AestraUI/Theme.h"
```


---

#### AestraAudio (Audio Engine)

**Purpose:** Professional audio processing with ultra-low latency.

**Status:** ✅ Complete

**Dependencies:** AestraCore, AestraPlat

**Key Components:**
- WASAPI integration (Windows)
- RtAudio backend (cross-platform)
- 64-bit audio pipeline
- Multi-threaded DSP
- Sample-accurate timing
- Low-latency design (<10ms)

**Audio Capabilities:**
- Sample rates: 44.1kHz - 192kHz
- Buffer sizes: 64 - 4096 samples
- Bit depths: 16, 24, 32-bit integer and float
- Channels: Up to 32

**Public API:**
```cpp
#include "AestraAudio/AudioEngine.h"
#include "AestraAudio/AudioDevice.h"
#include "AestraAudio/AudioBuffer.h"
```


---

#### Plugin hosting

There is no separate SDK module. VST3 and CLAP hosting lives in `AestraAudio/` (`Plugin/`, with a scanning helper process), and the editors that embed a plugin's own window live in `AestraUI/`. Aestra's own instruments and effects are in `AestraPlugins/` and `AestraAudio/Plugin/`. See the [Roadmap](../technical/roadmap.md) for scope decisions.

---

### Layer 3: Aestra (Application)

**Purpose:** Main digital audio workstation application.

**Status:** 🚧 Active Development

**Dependencies:** All framework modules (AestraUI, AestraAudio)

**Key Components:**
- Timeline/Sequencer (Pattern-based)
- Mixer console
- Pattern editor
- Project management
- User preferences
- Plugin management

**Current Features:**
| Feature | Status |
|---------|--------|
| Timeline | ✅ Complete |
| Audio Playback | ✅ Complete |
| Theme System | ✅ Complete |
| Sample Manipulation | 🚧 In Progress |
| Mixing Controls | ✅ Complete |
| VST3 and CLAP Hosting | ✅ Implemented |
| MIDI | 🚧 Live input works; file import/export in review |

---

## 🔗 Module Communication

### Inter-Module Communication Patterns

```mermaid
sequenceDiagram
    participant App as Application
    participant UI as AestraUI
    participant Audio as AestraAudio
    participant Plat as AestraPlat
    
    App->>UI: Create Window
    UI->>Plat: Platform Create Window
    Plat-->>UI: Window Handle
    UI-->>App: UI Context
    
    App->>Audio: Initialize Engine
    Audio->>Plat: Get Audio Devices
    Plat-->>Audio: Device List
    Audio-->>App: Engine Ready
    
    loop Main Loop
        Plat->>UI: Input Events
        UI->>App: UI Events
        App->>Audio: Transport Control
        Audio-->>App: Audio Status
        App->>UI: Update UI
        UI->>Plat: Render Frame
    end
```

### Data Flow Patterns

#### 1. Event-Driven (UI → Application)

```cpp
// UI generates events
ui.onButtonClick([&](Button* btn) {
    app.handlePlayButton();
});
```

#### 2. Callback-Based (Audio → Application)

```cpp
// Audio engine calls back on audio thread
audio.setCallback([&](AudioBuffer& buffer) {
    app.processAudioBlock(buffer);
});
```

#### 3. Polling (Application → Platform)

```cpp
// Application polls platform state
while (app.running) {
    plat.pollEvents();
    app.update();
}
```

## 📊 Module Statistics

### Code Metrics

Counted from tracked source files, excluding `External/` (Nov 2026):

| Module | Lines | Headers | Source files |
|--------|------:|--------:|-------------:|
| AestraCore | ~8,100 | 16 | 12 |
| AestraPlat | ~4,600 | 10 | 10 |
| AestraUI | ~72,900 | 131 | 103 |
| AestraAudio | ~98,000 | 280 | 127 |
| AestraPlugins | ~2,100 | 8 | 7 |
| AestraLicense | ~4,900 | 17 | 14 |
| Application (`Source/`) | ~81,200 | 153 | 108 |

## 🎯 Design Guidelines

### Module Design Principles

1. **Single Responsibility**
   - Each module has one clear purpose
   - Avoids feature creep and bloat

2. **Minimal Dependencies**
   - Only depend on lower layers
   - Minimize coupling between modules

3. **Interface Segregation**
   - Small, focused interfaces
   - Clients only depend on what they use

4. **Dependency Inversion**
   - Depend on abstractions, not implementations
   - Use interfaces and abstract classes

5. **Open/Closed Principle**
   - Open for extension
   - Closed for modification

### Adding New Modules

When adding a new module to Aestra:

1. **Determine Layer**
   - Identify which layer the module belongs to
   - Ensure it only depends on lower layers

2. **Define Public API**
   - Create clear header-only interface
   - Document all public functions

3. **Write Tests**
   - Unit tests for core functionality
   - Integration tests with dependent modules

4. **Update Documentation**
   - Add to this hierarchy page
   - Create detailed module documentation

5. **Update CMake**
   - Add to build system
   - Configure dependencies

## 🔍 Module Exploration

Each module is described in its section above, and in its own README at the root of the module directory.


Or return to:
- [Architecture Overview →](overview.md)
- [System Architecture →](overview.md#system-overview)
