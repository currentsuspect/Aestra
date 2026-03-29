# Contributing to Aestra

This file is the short entry point. The fuller contributor workflow lives in [docs/developer/contributing.md](docs/developer/contributing.md).

## Quick Start

```powershell
git clone https://github.com/currentsuspect/Aestra.git
cd Aestra
pwsh -File scripts/install-hooks.ps1
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo --parallel
ctest --test-dir build --config RelWithDebInfo --output-on-failure
```

## Expectations

- Work from a topic branch off `develop` unless maintainers direct otherwise
- Keep changes scoped and reviewable
- Update docs when behavior, build flow, or contributor workflow changes
- Add a short item to the `Unreleased` section of [CHANGELOG.md](CHANGELOG.md) for notable changes
- Run `scripts/docs-check.sh` if you changed Markdown or docs structure

## Documentation-specific checks

- Make sure relative links resolve
- Prefer pointing to the current docs in `docs/` rather than historical notes in `meta/`
- If you add public API surface, update Doxygen comments where appropriate

API docs can be generated with:

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
