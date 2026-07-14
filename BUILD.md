# Building Aestra

Use [docs/getting-started/building.md](docs/getting-started/building.md) as the canonical build guide.

## Quick Start

### Full desktop build

```powershell
git clone https://github.com/currentsuspect/Aestra.git
cd Aestra
pwsh -File scripts/install-hooks.ps1
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

### Headless build

```powershell
cmake -S . -B build-headless -DAestra_CORE_MODE=ON -DAESTRA_HEADLESS_ONLY=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-headless --config Release --parallel
```

Run tests with:

```bash
ctest --test-dir build --output-on-failure
```

## Premium-enabled builds

If a build tree is configured with `-DAESTRA_LICENSE_ENABLE_PREMIUM_LEASES=ON`, the license verification public key is baked in from the environment at configure time. That build directory then requires `AESTRA_LICENSE_PUBLIC_KEY_HEX` on **every reconfigure** (CMake reconfigures automatically when a `CMakeLists.txt` changes), or configuration aborts:

```bash
export AESTRA_LICENSE_PUBLIC_KEY_HEX=<64 hex chars>   # verification public key, not the signing secret
```

Default public/core builds leave this option `OFF` and do not need the variable. See [docs/getting-started/building.md](docs/getting-started/building.md#troubleshooting) for details.
