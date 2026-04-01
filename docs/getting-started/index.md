# Getting Started with Aestra

Welcome to Aestra. This page is the fast path into the current public build and contributor workflow.

## 🎯 Prerequisites

Before you begin, ensure you have:

### All Platforms
- **CMake** 3.22 or later
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

## 🚀 Quick Start

Choose your platform to get started:

### Windows Quick Start

```powershell
# 1. Clone the repository
git clone https://github.com/currentsuspect/Aestra.git
cd Aestra

# 2. Install Git hooks (recommended)
pwsh -File scripts/install-hooks.ps1

# 3. Configure build
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release

# 4. Build the project
cmake --build build --config Release --parallel

# 5. Run Aestra
cd build/bin/Release
./Aestra.exe
```

### Linux Quick Start

```bash
# 1. Install dependencies
sudo apt update
sudo apt install build-essential cmake git libasound2-dev \
                 libx11-dev libxrandr-dev libxinerama-dev libgl1-mesa-dev

# 2. Clone the repository
git clone https://github.com/currentsuspect/Aestra.git
cd Aestra

# 3. Configure and build
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel

# 4. Run Aestra
./build/bin/Aestra
```

## 📖 What's Next?

After successfully building Aestra:

1. **[Read the Full Building Guide](building.md)** — Detailed build instructions and troubleshooting
2. **[Try the Quickstart Tutorial](quickstart.md)** — Learn Aestra's basic workflow
3. **[Explore the Architecture](../architecture/overview.md)** — Understand how Aestra works
4. **[Review the Contributor Workflow](../developer/contributing.md)** — See branch, test, and docs expectations

## 🎵 System Requirements

### Minimum Requirements
- **OS:** Windows 10 64-bit (build 1809+) or Linux with the required development libraries
- **CPU:** Intel Core i5 (4th gen) or AMD Ryzen 3
- **RAM:** 8 GB
- **GPU:** OpenGL 3.3+ compatible with 1 GB VRAM
- **Audio:** WASAPI-compatible audio interface (Windows) or ALSA-compatible device stack (Linux)

### Recommended
- **CPU:** Intel Core i7/i9 or AMD Ryzen 7/9
- **RAM:** 16 GB or more
- **GPU:** Dedicated graphics card with 2+ GB VRAM
- **Audio:** Low-latency audio interface (ASIO support optional)
- **Storage:** SSD for project files and sample libraries

## 🛠️ Build Modes

Aestra supports different build configurations:

### Core Mode
```bash
cmake -S . -B build -DAestra_CORE_MODE=ON
```
Builds the public/source-available shape of the repo and uses public assets when premium modules are absent.

### Full Build
```bash
cmake -S . -B build
```
Builds with default options. In public-only checkouts, `Aestra_CORE_MODE` is still forced on when premium modules are not present.

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
- **[Contributing Guide](../developer/contributing.md)** — How to contribute to Aestra
- **[FAQ](../technical/faq.md)** — Frequently asked questions
- **[Troubleshooting](building.md#troubleshooting)** — Common build issues and solutions

## 💡 Need Help?

If you encounter issues:

1. Check the [Building Guide](building.md) for detailed instructions
2. Review the [FAQ](../technical/faq.md) for common questions
3. Search [GitHub Issues](https://github.com/currentsuspect/Aestra/issues) for similar problems
4. Read [Contributing Guide](../developer/contributing.md) if you are preparing a patch

---

Ready to dive deeper? Continue to the [Building Guide](building.md) for comprehensive instructions.
