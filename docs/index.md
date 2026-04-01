# Welcome to Aestra

<div class="hero-section" markdown="1">

# 🧭 Aestra

Active engineering repo for Aestra. The public tree includes the core engine, UI framework, platform layer, tests, and contributor docs.

<div class="cta-buttons" markdown="1">
[Get Started](getting-started/index.md){ .cta-button }
[View on GitHub](https://github.com/currentsuspect/Aestra){ .cta-button .secondary }
</div>

</div>

## ✨ Key Features

<div class="feature-grid" markdown="1">

<div class="feature-card" markdown="1">
<span class="icon">⚡</span>
### Native Audio Stack
WASAPI, ASIO, RtAudio, DSP, and headless render/test paths live in one codebase.
</div>

<div class="feature-card" markdown="1">
<span class="icon">🎨</span>
### Custom UI Framework
AestraUI provides the renderer, widgets, theme system, and app-facing UI infrastructure.
</div>

<div class="feature-card" markdown="1">
<span class="icon">🔓</span>
### Modular Layout
Core, platform, audio, UI, and app layers are split into distinct modules with separate docs and tests.
</div>

<div class="feature-card" markdown="1">
<span class="icon">🧩</span>
### Source Available
The public repo is intended for transparency, contribution, and engineering collaboration under ASSAL v1.1.
</div>

<div class="feature-card" markdown="1">
<span class="icon">📖</span>
### Current Focus
The strongest verified paths today are internal plugin discovery, persistence, and headless audible playback.
</div>

<div class="feature-card" markdown="1">
<span class="icon">🎯</span>
### Contributor Surface
Build, test, architecture, and roadmap docs are available directly in the repo for maintenance work.
</div>

</div>

## 🚀 Quick Start

=== "Windows"

    ```powershell
    # Clone the repository
    git clone https://github.com/currentsuspect/Aestra.git
    cd Aestra
    
    # Install Git hooks
    pwsh -File scripts/install-hooks.ps1
    
    # Configure build
    cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
    
    # Build the project
    cmake --build build --config Release --parallel
    
    # Run Aestra
    cd build/bin/Release
    ./Aestra.exe
    ```

=== "Linux"

    ```bash
    # Install dependencies
    sudo apt update
    sudo apt install build-essential cmake git libasound2-dev \
                     libx11-dev libxrandr-dev libxinerama-dev libgl1-mesa-dev
    
    # Clone the repository
    git clone https://github.com/currentsuspect/Aestra.git
    cd Aestra
    
    # Configure build
    cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
    
    # Build and run
    cmake --build build --config Release --parallel
    ./build/bin/Aestra
    ```

For detailed build instructions, see the [Building Guide](getting-started/building.md).

## 📌 Current Engineering Snapshot (March 2026)

Aestra is still in active development, but a few paths are now concretely proven:

- internal built-in plugin discovery works through normal manager lookup
- `Aestra Rumble` can be instantiated, saved, restored, and rendered in headless tests
- Arsenal units can now hold real plugin instances instead of placeholder-only entries
- internal plugin units survive project save/load round-trips
- headless Arsenal pattern playback can route MIDI to Rumble and produce audible output

If you want the most truthful picture of current progress, start with:

- [Roadmap](technical/roadmap.md)
- [Testing & CI](technical/testing_ci.md)
- [Rumble MVP Plan](technical/RUMBLE_MVP_PLAN.md)

## 📊 Project Status

<table class="module-status-table">
  <thead>
    <tr>
      <th>Module</th>
      <th>Status</th>
      <th>Description</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>AestraCore</strong></td>
      <td><span class="status-icon success"></span>Complete</td>
      <td>Core utilities, math, threading, file I/O</td>
    </tr>
    <tr>
      <td><strong>AestraPlat</strong></td>
      <td><span class="status-icon success"></span>Complete</td>
      <td>Platform abstraction (Win32, X11)</td>
    </tr>
    <tr>
      <td><strong>AestraUI</strong></td>
      <td><span class="status-icon success"></span>Complete</td>
      <td>OpenGL UI framework with theme system</td>
    </tr>
    <tr>
      <td><strong>AestraAudio</strong></td>
      <td><span class="status-icon success"></span>Complete</td>
      <td>WASAPI/RtAudio/ASIO integration</td>
    </tr>
    <tr>
      <td><strong>Timeline</strong></td>
      <td><span class="status-icon success"></span>Complete</td>
      <td>Pattern-based sequencer</td>
    </tr>
    <tr>
      <td><strong>Mixing</strong></td>
      <td><span class="status-icon success"></span>Complete</td>
      <td>Volume, pan, mute, solo controls</td>
    </tr>
    <tr>
      <td><strong>Project Persistence</strong></td>
      <td><span class="status-icon success"></span>Complete</td>
      <td>Save/load, autosave, crash-safe recovery, round-trip verified</td>
    </tr>
    <tr>
      <td><strong>Undo/Redo</strong></td>
      <td><span class="status-icon warning"></span>In Progress</td>
      <td>Command history exists, needs broader main-UX integration hardening</td>
    </tr>
    <tr>
      <td><strong>Internal Arsenal Plugins</strong></td>
      <td><span class="status-icon warning"></span>In Progress</td>
      <td>Rumble validated through discovery, project persistence, and audible headless playback</td>
    </tr>
    <tr>
      <td><strong>Third-Party Plugin Hosting</strong></td>
      <td><span class="status-icon info"></span>Decision Gate</td>
      <td>VST3/CLAP scaffolding exists; Phase 4 decision (Sep 2026)</td>
    </tr>
    <tr>
      <td><strong>Recording</strong></td>
      <td><span class="status-icon warning"></span>In Progress</td>
      <td>Phase 3 target (Jul–Sep 2026)</td>
    </tr>
    <tr>
      <td><strong>Offline Render/Export</strong></td>
      <td><span class="status-icon warning"></span>In Progress</td>
      <td>Phase 3 target (Jul–Sep 2026)</td>
    </tr>
  </tbody>
</table>

## 🎵 Code Example

Here's a glimpse of Aestra's clean, modern C++ architecture:

```cpp
// Initialize Aestra Audio Engine
#include "AestraAudio/AudioEngine.h"

int main() {
    // Create audio engine with WASAPI backend
    Aestra::AudioEngine engine;
    
    // Configure for ultra-low latency
    Aestra::AudioConfig config;
    config.sampleRate = 48000;
    config.bufferSize = 512;  // ~10ms latency
    config.exclusiveMode = true;
    
    // Initialize and start
    if (engine.initialize(config)) {
        engine.start();
        
        // Your DAW logic here
        // ...
        
        engine.stop();
    }
    
    return 0;
}
```

## 🧭 Our Philosophy

<div class="philosophy-quote" markdown="1">
At Aestra Studios, we believe software should feel like art — light, native, and human.

Every line of code in Aestra is written with intention. No shortcuts, no legacy cruft, just clean, modern C++ designed for the future of music production.

<footer>— Dylan Makori, Founder of Aestra Studios</footer>
</div>

**Core Values:**

- 🆓 **Transparency First** — Source-available code you can trust and learn from
- 🎯 **Intention Over Features** — Every feature serves a purpose, no bloat
- ⚡ **Performance Matters** — Professional-grade audio with ultra-low latency
- 🎨 **Beauty in Simplicity** — Clean UI that gets out of your way
- 🤝 **Community-Driven** — Built by musicians, for musicians

## 📚 Documentation Navigation

<div class="feature-grid" markdown="1">

<div class="feature-card" markdown="1">
### 🚀 Getting Started
New to Aestra? Start here for setup guides, build instructions, and quickstart tutorials.

[Explore →](getting-started/index.md)
</div>

<div class="feature-card" markdown="1">
### 🏗️ Architecture
Deep dive into Aestra's modular design, audio pipeline, and rendering system.

[Learn More →](architecture/overview.md)
</div>

<div class="feature-card" markdown="1">
### 👨‍💻 Developer Guide
Contributing to Aestra? Find coding standards, debugging tips, and best practices here.

[Start Contributing →](developer/contributing.md)
</div>

<div class="feature-card" markdown="1">
### 📖 Technical Reference
FAQ, glossary, and project roadmap.

[Browse Docs →](technical/faq.md)
</div>

<div class="feature-card" markdown="1">
### 🔌 API Reference
Comprehensive API documentation for all Aestra modules.

[View API →](api/index.md)
</div>

<div class="feature-card" markdown="1">
### 🧪 Testing & Verification
Current confidence suite, CI profile notes, and the highest-signal local verification commands.

[See Testing & CI →](technical/testing_ci.md)
</div>

<div class="feature-card" markdown="1">
### 🤝 Community
Code of conduct, support channels, and security policy.

[Join Community →](community/code-of-conduct.md)
</div>

</div>

## 🌍 Join the Community

<div class="community-section" markdown="1">

## Help Shape Aestra's Future

Aestra is built by musicians, for musicians. Your feedback, contributions, and support make this project possible.

<div class="community-buttons" markdown="1">
[Report Bugs](https://github.com/currentsuspect/Aestra/issues){ .community-button }
[Join Discussions](https://github.com/currentsuspect/Aestra/discussions){ .community-button }
[View Roadmap](technical/roadmap.md){ .community-button }
</div>

</div>

## 📜 License

Aestra is licensed under the **Aestra Studios Source-Available License (ASSAL) v1.1**.

The source code is publicly visible for transparency and education, but is **NOT open-source**. All rights reserved by Dylan Makori / Aestra Studios.

[Learn More About Licensing →](about/licensing.md)

---

<div style="text-align: center; margin-top: 3rem; padding: 2rem; background: var(--md-code-bg-color); border-radius: 1rem;">

**Built by musicians, for musicians. Crafted with intention.** 🎵

*Copyright © 2026 Dylan Makori / Aestra Studios. All rights reserved.*

</div>
