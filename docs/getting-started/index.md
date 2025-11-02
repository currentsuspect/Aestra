# Getting Started with NOMAD DAW

Welcome to NOMAD DAW! This guide will help you get up and running quickly.

## 🎯 Prerequisites

Before you begin, ensure you have:

### All Platforms
- **CMake** 3.15 or later
- **Git** 2.30 or later
- **C++17 compatible compiler**

### Platform-Specific Requirements

=== "Windows 10/11"

    - **Visual Studio 2022** with C++ workload
    - **Windows SDK** (included with Visual Studio)
    - **PowerShell 7** or later (recommended)
    - **MSVC Toolchain** (v143 or later)

=== "Linux"

    - **GCC 9+** or **Clang 10+**
    - **Build essentials** (`build-essential` package)
    - **ALSA development libraries** (`libasound2-dev`)
    - **X11 development libraries** (`libx11-dev`, `libxrandr-dev`, `libxinerama-dev`)
    - **OpenGL development libraries** (`libgl1-mesa-dev`)

=== "macOS"

    - **Xcode Command Line Tools**
    - **Homebrew** package manager
    - **CMake** (via Homebrew)

## 🚀 Quick Start

Choose your platform to get started:

### Windows Quick Start

```powershell
# 1. Clone the repository
git clone https://github.com/currentsuspect/NOMAD.git
cd NOMAD

# 2. Install Git hooks (recommended)
pwsh -File scripts/install-hooks.ps1

# 3. Configure build (Core-only mode for contributors)
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release

# 4. Build the project
cmake --build build --config Release --parallel

# 5. Run NOMAD
cd build/bin/Release
./NOMAD.exe
```

### Linux Quick Start

```bash
# 1. Install dependencies
sudo apt update
sudo apt install build-essential cmake git libasound2-dev \
                 libx11-dev libxrandr-dev libxinerama-dev libgl1-mesa-dev

# 2. Clone the repository
git clone https://github.com/currentsuspect/NOMAD.git
cd NOMAD

# 3. Install Git hooks
bash scripts/install-hooks.sh

# 4. Configure and build
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel

# 5. Run NOMAD
./build/bin/NOMAD
```

## 📖 What's Next?

After successfully building NOMAD:

1. **[Read the Full Building Guide](building.md)** — Detailed build instructions and troubleshooting
2. **[Try the Quickstart Tutorial](quickstart.md)** — Learn NOMAD's basic workflow
3. **[Explore the Architecture](../architecture/overview.md)** — Understand how NOMAD works
4. **[Join the Community](../community/code-of-conduct.md)** — Connect with other NOMAD users

## 🎵 System Requirements

### Minimum Requirements
- **OS:** Windows 10 64-bit (build 1809+), Linux with X11/Wayland, or macOS 10.15+
- **CPU:** Intel Core i5 (4th gen) or AMD Ryzen 3
- **RAM:** 8 GB
- **GPU:** OpenGL 3.3+ compatible with 1 GB VRAM
- **Audio:** WASAPI-compatible audio interface (Windows), ALSA (Linux), CoreAudio (macOS)

### Recommended
- **CPU:** Intel Core i7/i9 or AMD Ryzen 7/9
- **RAM:** 16 GB or more
- **GPU:** Dedicated graphics card with 2+ GB VRAM
- **Audio:** Low-latency audio interface (ASIO support optional)
- **Storage:** SSD for project files and sample libraries

## 🛠️ Build Modes

NOMAD supports different build configurations:

### Core-Only Mode (Contributors)
```bash
cmake -S . -B build -DNOMAD_CORE_MODE=ON
```
Builds only the publicly available source-available modules. Perfect for contributors and learning.

### Full Build (Internal)
```bash
cmake -S . -B build
```
Includes all modules including private assets. Requires additional permissions.

### Debug Build
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
```
Includes debug symbols and assertions for development.

### Release with Debug Info
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```
Optimized build with debug symbols for profiling.

## 📚 Additional Resources

- **[Building Guide](building.md)** — Complete build instructions for all platforms
- **[Contributing Guide](../developer/contributing.md)** — How to contribute to NOMAD
- **[FAQ](../technical/faq.md)** — Frequently asked questions
- **[Troubleshooting](building.md#troubleshooting)** — Common build issues and solutions

## 💡 Need Help?

If you encounter issues:

1. Check the [Building Guide](building.md) for detailed instructions
2. Review the [FAQ](../technical/faq.md) for common questions
3. Search [GitHub Issues](https://github.com/currentsuspect/NOMAD/issues) for similar problems
4. Join [GitHub Discussions](https://github.com/currentsuspect/NOMAD/discussions) to ask questions
5. Email support: [makoridylan@gmail.com](mailto:makoridylan@gmail.com)

---

Ready to dive deeper? Continue to the [Building Guide](building.md) for comprehensive instructions.
