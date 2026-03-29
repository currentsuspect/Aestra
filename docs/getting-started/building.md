# Building Aestra

This guide reflects the current public repository layout and CMake options.

## Supported Build Shapes

- Full desktop build: UI + app + tests
- Headless build: audio/core/test workflows without UI targets

Primary CMake options from the root `CMakeLists.txt`:

| Option | Description | Default |
| --- | --- | --- |
| `Aestra_CORE_MODE` | Build without premium/private modules and use public assets | `ON` in public-only checkouts |
| `AESTRA_ENABLE_UI` | Build the desktop UI application | `ON` |
| `AESTRA_HEADLESS_ONLY` | Force a headless-only configuration | `OFF` |
| `AESTRA_ENABLE_TESTS` | Build tests under `Tests/` | `ON` |

## Prerequisites

### Windows

- Visual Studio 2022 with the C++ workload
- CMake 3.22 or newer
- Git
- PowerShell 7 recommended

### Linux

- GCC 9+ or Clang 10+
- CMake 3.22 or newer
- Git
- Build essentials
- OpenGL/X11 development packages if you are building the UI

## Windows Build

### Full build

```powershell
git clone https://github.com/currentsuspect/Aestra.git
cd Aestra
pwsh -File scripts/install-hooks.ps1
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

### Headless-only build

```powershell
cmake -S . -B build-headless -DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-headless --config Release --parallel
```

### Running

With multi-config generators such as Visual Studio, binaries are typically under:

- `build/bin/Release`
- `build-headless/bin/Release`

Common targets include:

- `AestraHeadless`
- the main desktop app when UI is enabled
- test executables registered with CTest

## Linux Build

### Suggested dependencies

Ubuntu/Debian example:

```bash
sudo apt update
sudo apt install build-essential cmake git libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libgl1-mesa-dev
```

### Full build

```bash
git clone https://github.com/currentsuspect/Aestra.git
cd Aestra
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Headless-only build

```bash
cmake -S . -B build-headless -DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-headless --parallel
```

## Testing

Run the configured test set with:

```bash
ctest --test-dir build --output-on-failure
```

For a headless tree:

```bash
ctest --test-dir build-headless --output-on-failure
```

There is also a helper script in [scripts/run-confidence-suite.sh](../../scripts/run-confidence-suite.sh).

## Notes on `Aestra_CORE_MODE`

The public repo includes `aestra-core/cmake/AestraCoreMode.cmake`, which forces `Aestra_CORE_MODE=ON` when premium modules are not present. You can still pass it explicitly in commands so CI and contributor instructions stay unambiguous.

## Troubleshooting

### Configure succeeds, but UI targets are missing

Check whether `AESTRA_HEADLESS_ONLY=ON` was set, or whether you are intentionally working in a headless build tree.

### Linux configure warns about missing SDL2 or RtAudio

The root `CMakeLists.txt` will fall back to vendored dependencies only if the expected directories are present. Missing warnings usually mean the corresponding system package or vendored dependency tree is absent.

### Windows binary path does not match the command in an older doc

Prefer checking `build/bin` or `build/bin/<Config>`. Older docs may still mention outdated target names or output folders.
