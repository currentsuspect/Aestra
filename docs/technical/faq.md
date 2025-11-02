# ❓ Frequently Asked Questions (FAQ)

![FAQ](https://img.shields.io/badge/FAQ-Updated-blue)

Common questions and answers for contributors, developers, and users of Nomad DAW.

## 📋 Table of Contents

- [General Questions](#-general-questions)
- [Building and Setup](#-building-and-setup)
- [Contributing](#-contributing)
- [Technical Questions](#-technical-questions)
- [Licensing](#-licensing)
- [Troubleshooting](#-troubleshooting)

## 🌍 General Questions

### What is Nomad DAW?

Nomad DAW is a modern digital audio workstation built with C++17, featuring:
- Ultra-low latency audio engine with WASAPI support
- GPU-accelerated custom UI framework (NomadUI)
- FL Studio-inspired workflow
- Professional-grade audio processing
- Planned AI integration (Muse)

### Who develops Nomad?

Nomad is developed by Dylan Makori in Kenya, with contributions from the open-source community. While the source code is publicly visible for transparency, Nomad is proprietary commercial software.

### Is Nomad free to use?

**Yes! Nomad Core is free forever.** The hybrid model:
- ✅ **Nomad Core**: Completely free - full DAW with all essential features
- 💰 **Premium Add-ons** (future): Muse AI and premium plugins require paid licenses
- 👁️ Source code is publicly visible for transparency
- 🎓 Student discounts will apply to premium add-ons
- 🤝 Contributing to development is free and welcomed

### What platforms does Nomad support?

**Current support:**
- ✅ Windows 10/11 (64-bit) - Primary platform
- 🚧 Linux - In development
- 📋 macOS - Planned for future

### Is Nomad open-source?

No. Nomad uses the **NSSAL (Nomad Studios Source-Available License)** - a source-available license, not open-source. This means:
- ✅ **Nomad Core is free to use** for all purposes
- 👁️ You can view and study the source code
- 🤝 You can contribute improvements
- ❌ You cannot reuse the code in other projects
- ❌ You cannot distribute modified versions
- 📜 All contributions become property of the project

See [License Reference](../LICENSE_REFERENCE.md) for complete details.

## 🔨 Building and Setup

### How do I build Nomad from source?

Follow our comprehensive [Building Guide](../BUILDING.md):

**Quick start (Windows):**
```powershell
git clone https://github.com/currentsuspect/NOMAD.git
cd NOMAD
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

### What are the build requirements?

**All platforms:**
- CMake 3.15+
- Git 2.30+
- C++17 compatible compiler

**Windows:**
- Visual Studio 2022 with C++ workload
- Windows SDK

**Linux:**
- GCC 9+ or Clang 10+
- ALSA, X11, and OpenGL development libraries

### What is NOMAD_CORE_MODE?

`NOMAD_CORE_MODE=ON` builds only the public core without premium features. This is required for community contributors since premium modules are in private repositories.

```bash
# Public build (community contributors)
cmake -S . -B build -DNOMAD_CORE_MODE=ON

# Full build (requires private repos - internal only)
cmake -S . -B build
```

### Build fails with "private folder present" error?

This is expected in public CI. Use `NOMAD_CORE_MODE=ON` to skip private folders:

```bash
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release
```

### How do I set up my development environment?

1. **Install prerequisites** - See [Building Guide](../BUILDING.md)
2. **Clone the repository** - Fork and clone from GitHub
3. **Install Git hooks** - `pwsh -File scripts/install-hooks.ps1`
4. **Build the project** - Follow build instructions
5. **Configure your IDE** - Use `.clang-format` for code formatting

See [Contributing Guide](../CONTRIBUTING.md) for detailed setup.

## 🤝 Contributing

### How can I contribute to Nomad?

Several ways to contribute:
1. **Report bugs** - Open issues on GitHub
2. **Suggest features** - Share your ideas in discussions
3. **Submit pull requests** - Contribute code improvements
4. **Improve documentation** - Help make docs better
5. **Test and provide feedback** - Try new features and report issues

See [Contributing Guide](../CONTRIBUTING.md) for details.

### Do I need to sign a CLA?

Yes. By submitting a pull request, you agree that:
- All contributed code becomes property of Dylan Makori / Nomad DAW
- You grant full rights to use, modify, and distribute your contributions
- You waive ownership claims to your contributions

This is necessary because Nomad is commercial software. See [Contributing Guide](../CONTRIBUTING.md) for details.

### What kind of contributions are accepted?

**Public contributions** (in `nomad-core/`):
- ✅ Core audio engine improvements
- ✅ UI framework enhancements
- ✅ Bug fixes and optimizations
- ✅ Documentation improvements
- ✅ Unit tests
- ✅ Build system improvements
- ✅ Cross-platform compatibility

**Private modules** (not in public repo):
- ❌ Premium plugins and effects
- ❌ AI models (Muse internals)
- ❌ Licensing system
- ❌ Code signing and distribution

### How do I report a bug?

1. **Search existing issues** - Check if it's already reported
2. **Create a new issue** - Use the bug report template
3. **Provide details**:
   - Operating system and version
   - Steps to reproduce
   - Expected vs actual behavior
   - Screenshots or error messages
   - Nomad version

### What code style should I follow?

Follow our [Coding Style Guide](../developer/coding-style.md):
- **clang-format** for automatic formatting
- **PascalCase** for classes
- **camelCase** for functions and variables
- **m_ prefix** for member variables
- **120 character** line limit

### How long do PR reviews take?

- **Simple fixes**: 1-3 days
- **Small features**: 3-7 days
- **Major features**: 1-2 weeks
- **Large refactors**: 2-4 weeks

Reviews depend on maintainer availability and PR complexity.

## 💻 Technical Questions

### What audio drivers does Nomad support?

**Windows:**
- ✅ WASAPI (Exclusive and Shared modes)
- ✅ DirectSound (fallback)
- 📋 ASIO (planned)

**Linux:**
- 🚧 ALSA (in development)
- 📋 JACK (planned)
- 📋 PulseAudio (planned)

### What's the typical audio latency?

- **WASAPI Exclusive**: 5-10ms (best)
- **WASAPI Shared**: 10-20ms
- **DirectSound**: 20-50ms (fallback)

Latency depends on buffer size, sample rate, and hardware.

### Does Nomad support plugins?

Not yet. Plugin support is planned:
- 📋 VST3 hosting
- 📋 AU hosting (macOS)
- 📋 Native Nomad plugin format

### How does the UI rendering work?

NomadUI uses a custom immediate-mode rendering system:
- **OpenGL-based** rendering
- **Adaptive FPS** (1-120 FPS based on activity)
- **GPU-accelerated** for smooth 60+ FPS
- **Cache-friendly** widget tree

See [Architecture Overview](../ARCHITECTURE.md) for details.

### What's the threading model?

Nomad uses multiple threads:
- **Main Thread**: UI and application logic
- **Audio Thread**: Real-time audio processing (lock-free)
- **Loader Thread**: Async file I/O and caching

See [Architecture Overview](../ARCHITECTURE.md) for threading details.

### Can I use Nomad in my commercial projects?

**Yes! Nomad Core is free for all commercial use.** This includes:
- ✅ Commercial music production
- ✅ Client work and studio projects
- ✅ Released tracks and albums
- ✅ Live performances
- ✅ YouTube, streaming, broadcast

Only premium add-ons (Muse AI, premium plugins - future releases) will require paid licenses.

**Note:** The source code itself is proprietary and cannot be reused in other software projects.

## ⚖️ Licensing

### What license is Nomad under?

Nomad DAW is **proprietary commercial software**. The source code is visible but not open-source. See [License Reference](../LICENSE_REFERENCE.md) for complete terms.

### Can I fork Nomad?

No. You cannot create forks, derivative works, or distribute modified versions. However:
- You can contribute via pull requests
- You can suggest features and improvements
- You can study the code for educational purposes

### Can I use Nomad code in my project?

No. The code is proprietary and cannot be used in other projects. Any use requires explicit written permission from Dylan Makori.

### Why is Nomad not open-source?

We chose a proprietary model to:
- **Fund development** - Sustainable business model
- **Maintain quality** - Professional team and support
- **Provide value** - Fair compensation for developers
- **Competitive edge** - Protect innovative features

Source code transparency allows educational use and community contributions while maintaining a viable business.

## 🔧 Troubleshooting

### Audio is cutting out or glitching

**Possible causes:**
1. **Buffer size too small** - Increase in audio settings
2. **CPU overload** - Close other applications
3. **Wrong driver mode** - Try WASAPI Exclusive mode
4. **Sample rate mismatch** - Check audio settings

**Solutions:**
- Increase buffer size to 512 or 1024 samples
- Use WASAPI Exclusive mode for lowest latency
- Match sample rates (typically 44100 Hz or 48000 Hz)

### Build fails with missing dependencies

**Windows:**
```powershell
# Ensure Visual Studio 2022 with C++ workload is installed
# Check CMake version
cmake --version  # Should be 3.15+
```

**Linux:**
```bash
# Install all development libraries
sudo apt install build-essential cmake git \
                 libasound2-dev libx11-dev libxrandr-dev \
                 libxinerama-dev libgl1-mesa-dev
```

### Git hooks installation fails

**Solution:**
```powershell
# Ensure PowerShell 7+ is installed
$PSVersionTable.PSVersion

# Install hooks manually
Copy-Item -Path .githooks\* -Destination .git\hooks\ -Force
```

### UI is slow or laggy

**Possible causes:**
1. **Low-end GPU** - Integrated graphics may struggle
2. **High resolution** - 4K displays require more GPU power
3. **Driver issues** - Update graphics drivers

**Solutions:**
- Update graphics drivers
- Lower resolution or scaling
- Check GPU usage in Task Manager

### Cannot find audio device

**Solutions:**
1. **Check device manager** - Ensure audio device is enabled
2. **Update drivers** - Get latest audio drivers
3. **Try different driver** - Switch between WASAPI modes
4. **Restart audio service** - Windows Audio service

### Sample playback position is wrong

This was a known bug (fixed). Ensure you're using the latest version:
```bash
git pull origin main
cmake --build build --config Release
```

## 📚 Additional Resources

Still have questions? Check these resources:

- [Building Guide](../BUILDING.md) - Detailed build instructions
- [Contributing Guide](../CONTRIBUTING.md) - How to contribute
- [Architecture Overview](../ARCHITECTURE.md) - Technical deep dive
- [Coding Style Guide](../developer/coding-style.md) - Code conventions
- [Glossary](../GLOSSARY.md) - Technical terminology

## 💬 Getting Help

**Can't find your answer?**

1. **Search GitHub Issues** - Someone may have asked already
2. **Open a new issue** - Describe your problem clearly
3. **Email support** - makoridylan@gmail.com
4. **Community discussions** - GitHub Discussions (coming soon)

---

[← Return to Nomad Docs Index](../README.md)
