# Contributing to Aestra

This file is the short entry point. The fuller contributor workflow lives in [docs/developer/contributing.md](docs/developer/contributing.md).

## Quick Start

### Windows

```powershell
git clone https://github.com/currentsuspect/Aestra.git
cd Aestra
pwsh -File scripts/install-hooks.ps1
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo --parallel
ctest --test-dir build --config RelWithDebInfo --output-on-failure
```

### Linux

```bash
git clone https://github.com/currentsuspect/Aestra.git
cd Aestra
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

If you want Git hooks on Linux, configure Git to use `.githooks/` manually or run the PowerShell helper in an environment that supports it.

## Expectations

- Work from a topic branch off `develop` unless maintainers direct otherwise
- Keep changes scoped and reviewable
- Update docs when behavior, build flow, or contributor workflow changes
- Add a short item to the `Unreleased` section of [CHANGELOG.md](CHANGELOG.md) for notable changes
- Run the relevant local check for your change:
  - `ctest --test-dir build --output-on-failure` for code changes
  - `scripts/docs-check.sh` for docs or Markdown changes

## Documentation-specific checks

- Make sure relative links resolve
- Prefer pointing to the current docs in `docs/` rather than historical notes in `meta/`
- If you add public API surface, update Doxygen comments where appropriate

API docs can be generated on Windows with:

```powershell
.\scripts\generate-api-docs.bat
```

or:

```powershell
.\scripts\generate-api-docs.ps1 generate
```

## Security

Report vulnerabilities privately. See [SECURITY.md](SECURITY.md).

## License

By contributing, you agree your contributions are accepted under the repository's ASSAL v1.1 terms described in [LICENSE](LICENSE) and [LICENSING.md](LICENSING.md).
