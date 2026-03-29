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
