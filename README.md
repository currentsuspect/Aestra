# Nomad Core

This repository contains the open-source core of the Nomad DAW engine.

- Public: `nomad-core/` (buildable standalone with mock assets)
- Private: `nomad-premium/` (paid plugins, models), `nomad-build/` (build/signing)

Premium features are developed and released privately by Nomad Studios.

## 📚 Documentation

➡️ **[Read the Nomad Docs](docs/README.md)** - Comprehensive contributor and developer portal

Quick links:
- [Building Guide](docs/BUILDING.md) - How to build Nomad from source
- [Contributing Guide](docs/CONTRIBUTING.md) - How to contribute to the project
- [Architecture Overview](docs/ARCHITECTURE.md) - System design and structure
- [FAQ](docs/FAQ.md) - Frequently asked questions

## Build (Windows)

1. Install CMake, Git, a C++ toolchain (MSVC) and PowerShell 7.
2. Install Git hooks:
   - `pwsh -File scripts/install-hooks.ps1`
3. Configure and build:
   - `cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release`
   - `cmake --build build --config Release --parallel`

If private repos are present and you disable core-only mode, full builds can be made in private environments.

## Security

- `.gitignore` and `.gitattributes` block/route large or sensitive files.
- Pre-commit hook prevents committing secrets or private assets.
- Gitleaks workflow scans for secrets on pushes/PRs.

## Contributing

See `CONTRIBUTING.md` for guidelines.
 
