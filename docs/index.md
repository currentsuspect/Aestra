# Welcome to NOMAD DAW

<div class="hero-section" markdown="1">

# 🧭 NOMAD DAW

**Create Like Silence Is Watching**

A modern, professional digital audio workstation built from the ground up with intention.  
Featuring ultra-low latency audio, GPU-accelerated UI, and an FL Studio-inspired workflow.

<div class="cta-buttons" markdown="1">
[Get Started](getting-started/index.md){ .cta-button }
[View on GitHub](https://github.com/currentsuspect/NOMAD){ .cta-button .secondary }
</div>

</div>

## ✨ Key Features

<div class="feature-grid" markdown="1">

<div class="feature-card" markdown="1">
<span class="icon">⚡</span>
### Ultra-Low Latency
Professional-grade audio with <10ms latency. WASAPI exclusive mode with multi-threaded 64-bit processing for real-time performance.
</div>

<div class="feature-card" markdown="1">
<span class="icon">🎨</span>
### GPU-Accelerated UI
Custom OpenGL 3.3+ renderer with MSAA anti-aliasing. Buttery-smooth 60 FPS performance with adaptive rendering system.
</div>

<div class="feature-card" markdown="1">
<span class="icon">🔓</span>
### Zero Dependencies
Built from scratch with modern C++17. No bloated frameworks, no legacy constraints. Clean, maintainable codebase you can trust.
</div>

<div class="feature-card" markdown="1">
<span class="icon">🧩</span>
### Modular Architecture
Clear separation: Core → Platform → Audio → UI. Professional software design that scales with your needs.
</div>

<div class="feature-card" markdown="1">
<span class="icon">📖</span>
### Source Available
Publicly visible source code for transparency and education. See exactly how your DAW works under the hood.
</div>

<div class="feature-card" markdown="1">
<span class="icon">🎯</span>
### Built with Intention
Every feature serves a purpose. No bloat, just professional tools that get out of your way so you can create.
</div>

</div>

## 🚀 Quick Start

=== "Windows"

    ```powershell
    # Clone the repository
    git clone https://github.com/currentsuspect/NOMAD.git
    cd NOMAD
    
    # Install Git hooks
    pwsh -File scripts/install-hooks.ps1
    
    # Configure build (Core-only mode)
    cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release
    
    # Build the project
    cmake --build build --config Release --parallel
    
    # Run NOMAD
    cd build/bin/Release
    ./NOMAD.exe
    ```

=== "Linux"

    ```bash
    # Install dependencies
    sudo apt update
    sudo apt install build-essential cmake git libasound2-dev \
                     libx11-dev libxrandr-dev libxinerama-dev libgl1-mesa-dev
    
    # Clone the repository
    git clone https://github.com/currentsuspect/NOMAD.git
    cd NOMAD
    
    # Install Git hooks
    bash scripts/install-hooks.sh
    
    # Configure build
    cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release
    
    # Build and run
    cmake --build build --config Release --parallel
    ./build/bin/NOMAD
    ```

=== "macOS"

    ```bash
    # Install Xcode Command Line Tools
    xcode-select --install
    
    # Install Homebrew (if not installed)
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    
    # Install dependencies
    brew install cmake git
    
    # Clone the repository
    git clone https://github.com/currentsuspect/NOMAD.git
    cd NOMAD
    
    # Configure and build
    cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release --parallel
    
    # Run NOMAD
    ./build/bin/NOMAD
    ```

For detailed build instructions, see the [Building Guide](getting-started/building.md).

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
      <td><strong>NomadCore</strong></td>
      <td><span class="status-icon success"></span>Complete</td>
      <td>Core utilities, math, threading, file I/O</td>
    </tr>
    <tr>
      <td><strong>NomadPlat</strong></td>
      <td><span class="status-icon success"></span>Complete</td>
      <td>Platform abstraction (Win32, X11)</td>
    </tr>
    <tr>
      <td><strong>NomadUI</strong></td>
      <td><span class="status-icon success"></span>Complete</td>
      <td>OpenGL UI framework with theme system</td>
    </tr>
    <tr>
      <td><strong>NomadAudio</strong></td>
      <td><span class="status-icon success"></span>Complete</td>
      <td>WASAPI/RtAudio integration</td>
    </tr>
    <tr>
      <td><strong>Timeline</strong></td>
      <td><span class="status-icon success"></span>Complete</td>
      <td>FL Studio-inspired sequencer</td>
    </tr>
    <tr>
      <td><strong>Mixing</strong></td>
      <td><span class="status-icon warning"></span>In Progress</td>
      <td>Volume, pan, mute, solo controls</td>
    </tr>
    <tr>
      <td><strong>VST3 Hosting</strong></td>
      <td><span class="status-icon info"></span>Planned Q2 2025</td>
      <td>Plugin integration system</td>
    </tr>
    <tr>
      <td><strong>MIDI</strong></td>
      <td><span class="status-icon info"></span>Planned Q2 2025</td>
      <td>MIDI input/output and piano roll</td>
    </tr>
  </tbody>
</table>

## 🎵 Code Example

Here's a glimpse of NOMAD's clean, modern C++ architecture:

```cpp
// Initialize NOMAD Audio Engine
#include "NomadAudio/AudioEngine.h"

int main() {
    // Create audio engine with WASAPI backend
    nomad::AudioEngine engine;
    
    // Configure for ultra-low latency
    nomad::AudioConfig config;
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
At Nomad Studios, we believe software should feel like art — light, native, and human.

Every line of code in NOMAD is written with intention. No shortcuts, no legacy cruft, just clean, modern C++ designed for the future of music production.

<footer>— Dylan Makori, Founder of Nomad Studios</footer>
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
New to NOMAD? Start here for setup guides, build instructions, and quickstart tutorials.

[Explore →](getting-started/index.md)
</div>

<div class="feature-card" markdown="1">
### 🏗️ Architecture
Deep dive into NOMAD's modular design, audio pipeline, and rendering system.

[Learn More →](architecture/overview.md)
</div>

<div class="feature-card" markdown="1">
### 👨‍💻 Developer Guide
Contributing to NOMAD? Find coding standards, debugging tips, and best practices here.

[Start Contributing →](developer/contributing.md)
</div>

<div class="feature-card" markdown="1">
### 📖 Technical Reference
FAQ, glossary, AI integration guide, and project roadmap.

[Browse Docs →](technical/faq.md)
</div>

<div class="feature-card" markdown="1">
### 🔌 API Reference
Comprehensive API documentation for all NOMAD modules.

[View API →](api/index.md)
</div>

<div class="feature-card" markdown="1">
### 🤝 Community
Code of conduct, support channels, and security policy.

[Join Community →](community/code-of-conduct.md)
</div>

</div>

## 🌍 Join the Community

<div class="community-section" markdown="1">

## Help Shape NOMAD's Future

NOMAD is built by musicians, for musicians. Your feedback, contributions, and support make this project possible.

<div class="community-buttons" markdown="1">
[Report Bugs](https://github.com/currentsuspect/NOMAD/issues){ .community-button }
[Join Discussions](https://github.com/currentsuspect/NOMAD/discussions){ .community-button }
[View Roadmap](technical/roadmap.md){ .community-button }
</div>

</div>

## 📜 License

NOMAD DAW is licensed under the **Nomad Studios Source-Available License (NSSAL) v1.0**.

The source code is publicly visible for transparency and education, but is **NOT open-source**. All rights reserved by Dylan Makori / Nomad Studios.

[Learn More About Licensing →](about/licensing.md)

---

<div style="text-align: center; margin-top: 3rem; padding: 2rem; background: var(--md-code-bg-color); border-radius: 1rem;">

**Built by musicians, for musicians. Crafted with intention.** 🎵

*Copyright © 2025 Dylan Makori / Nomad Studios. All rights reserved.*

</div>
