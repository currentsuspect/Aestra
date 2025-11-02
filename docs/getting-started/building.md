# 🔨 Building Nomad DAW

![Build Status](https://img.shields.io/badge/Build-Passing-brightgreen)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey)

Complete guide for building Nomad DAW from source on Windows and Linux.

## 📋 Table of Contents

- [Prerequisites](#prerequisites)
- [Windows Build](#windows-build)
- [Linux Build](#linux-build)
- [Build Options](#build-options)
- [Troubleshooting](#troubleshooting)

## 🎯 Prerequisites

### All Platforms

- **CMake** 3.15 or later
- **Git** 2.30 or later
- **C++17 compatible compiler**

### Windows

- **Visual Studio 2022** with C++ workload
- **Windows SDK** (included with Visual Studio)
- **PowerShell 7** or later (recommended)
- **MSVC Toolchain** (v143 or later)

### Linux

- **GCC 9+** or **Clang 10+**
- **Build essentials** (`build-essential` package)
- **ALSA development libraries** (`libasound2-dev`)
- **X11 development libraries** (`libx11-dev`, `libxrandr-dev`, `libxinerama-dev`)
- **OpenGL development libraries** (`libgl1-mesa-dev`)

## 🪟 Windows Build

### Quick Start

```powershell
# 1. Clone the repository
git clone https://github.com/currentsuspect/NOMAD.git
cd NOMAD

# 2. Install Git hooks (recommended)
pwsh -File scripts/install-hooks.ps1

# 3. Configure build (Core-only mode)
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release

# 4. Build the project
cmake --build build --config Release --parallel
```

### Step-by-Step Instructions

#### 1. Clone the Repository

```powershell
git clone https://github.com/currentsuspect/NOMAD.git
cd NOMAD
```

#### 2. Install Git Hooks (Optional but Recommended)

Git hooks help prevent committing secrets or private assets:

```powershell
pwsh -File scripts/install-hooks.ps1
```

#### 3. Configure CMake

**Core-only build (public contributors):**
```powershell
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release
```

**Full build with Visual Studio generator:**
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
```

#### 4. Build the Project

**Release build:**
```powershell
cmake --build build --config Release --parallel
```

**Debug build with symbols:**
```powershell
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo --parallel
```

#### 5. Run the Application

```powershell
cd build\Release
.\NomadDAW.exe
```

### Building with Inno Setup (Installer)

To create a distributable installer:

1. Install [Inno Setup 6](https://jrsoftware.org/isdl.php) or later

2. Build the project in Release mode:
   ```powershell
   cmake --build build --config Release
   ```

3. Run the installer script:
   ```powershell
   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
   ```

The installer will be created as `Output\NOMAD-DAW-Setup.exe`.

## 🐧 Linux Build

### Quick Start

```bash
# 1. Install dependencies (Ubuntu/Debian)
sudo apt update
sudo apt install build-essential cmake git \
                 libasound2-dev libx11-dev libxrandr-dev \
                 libxinerama-dev libgl1-mesa-dev

# 2. Clone the repository
git clone https://github.com/currentsuspect/NOMAD.git
cd NOMAD

# 3. Configure build
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release

# 4. Build the project
cmake --build build --parallel $(nproc)
```

### Step-by-Step Instructions

#### 1. Install Dependencies

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install build-essential cmake git \
                 libasound2-dev libx11-dev libxrandr-dev \
                 libxinerama-dev libgl1-mesa-dev
```

**Fedora/RHEL:**
```bash
sudo dnf groupinstall "Development Tools"
sudo dnf install cmake git alsa-lib-devel \
                 libX11-devel libXrandr-devel \
                 libXinerama-devel mesa-libGL-devel
```

**Arch Linux:**
```bash
sudo pacman -S base-devel cmake git \
               alsa-lib libx11 libxrandr \
               libxinerama mesa
```

#### 2. Clone the Repository

```bash
git clone https://github.com/currentsuspect/NOMAD.git
cd NOMAD
```

#### 3. Configure CMake

```bash
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release
```

#### 4. Build the Project

```bash
cmake --build build --parallel $(nproc)
```

#### 5. Run the Application

```bash
./build/NomadDAW
```

## ⚙️ Build Options

### CMake Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `NOMAD_CORE_MODE` | Build only public core (no premium features) | `OFF` |
| `CMAKE_BUILD_TYPE` | Build type (`Debug`, `Release`, `RelWithDebInfo`) | `Release` |
| `CMAKE_INSTALL_PREFIX` | Installation directory | System default |
| `BUILD_TESTS` | Build unit tests | `OFF` |
| `BUILD_DOCS` | Build documentation | `OFF` |

### Build Configurations

**Development build (with debug symbols):**
```bash
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

**Debug build (full debugging):**
```bash
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

**Release build (optimized):**
```bash
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## 🔍 Troubleshooting

### Common Issues

<details>
<summary><b>CMake cannot find Visual Studio</b></summary>

**Solution**: Ensure Visual Studio 2022 is installed with C++ workload:
```powershell
# List available generators
cmake --help

# Specify generator explicitly
cmake -G "Visual Studio 17 2022" -A x64 -S . -B build
```
</details>

<details>
<summary><b>Missing dependencies on Linux</b></summary>

**Solution**: Install all required development packages:
```bash
# Ubuntu/Debian
sudo apt install build-essential cmake git \
                 libasound2-dev libx11-dev libxrandr-dev \
                 libxinerama-dev libgl1-mesa-dev

# Check installed packages
dpkg -l | grep -E "(alsa|x11|mesa)"
```
</details>

<details>
<summary><b>Git hooks installation fails</b></summary>

**Solution**: Ensure PowerShell 7+ is installed and Git hooks directory exists:
```powershell
# Check PowerShell version
$PSVersionTable.PSVersion

# Install hooks manually
Copy-Item -Path .githooks\* -Destination .git\hooks\ -Force
```
</details>

<details>
<summary><b>Build fails with "private folder present" error</b></summary>

**Solution**: This is expected in public CI. For local development:
```powershell
# Use NOMAD_CORE_MODE to skip private folders
cmake -S . -B build -DNOMAD_CORE_MODE=ON
```
</details>

<details>
<summary><b>OpenGL linking errors on Linux</b></summary>

**Solution**: Install Mesa OpenGL development libraries:
```bash
# Ubuntu/Debian
sudo apt install libgl1-mesa-dev mesa-common-dev

# Fedora/RHEL
sudo dnf install mesa-libGL-devel

# Verify OpenGL installation
glxinfo | grep "OpenGL version"
```
</details>

### Build Performance

**Parallel builds:**
```bash
# Windows (PowerShell)
cmake --build build --config Release --parallel

# Linux
cmake --build build --parallel $(nproc)

# Specify job count manually
cmake --build build --parallel 8
```

**Clean build:**
```bash
# Remove build directory and rebuild
rm -rf build
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## 📚 Additional Resources

- [Architecture Overview](../ARCHITECTURE.md) - Understand Nomad's structure
- [Contributing Guide](../CONTRIBUTING.md) - How to contribute code
- [Coding Style](../developer/coding-style.md) - Code formatting guidelines
- [FAQ](../FAQ.md) - Frequently asked questions

## 🆘 Getting Help

If you encounter issues not covered here:

1. Check [FAQ](../FAQ.md) for common problems
2. Search [GitHub Issues](https://github.com/currentsuspect/NOMAD/issues)
3. Open a new issue with:
   - Operating system and version
   - CMake version (`cmake --version`)
   - Compiler version
   - Complete build log

---

[← Return to Nomad Docs Index](../README.md)
