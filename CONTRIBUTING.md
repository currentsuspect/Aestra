# Contributing to Aestra

This file is the short entry point. The fuller contributor workflow lives in [docs/developer/contributing.md](docs/developer/contributing.md).

## Quick Start

### Windows

```powershell
git clone https://github.com/currentsuspect/Aestra.git
cd Aestra
pwsh -File scripts/install-hooks.ps1  # Linux/macOS: python3 scripts/install_hooks.py
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo --parallel
ctest --test-dir build --config RelWithDebInfo --output-on-failure
```

### Linux

```bash
git clone https://github.com/currentsuspect/Aestra.git
cd Aestra
python3 scripts/install_hooks.py
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

If you want Git hooks on Linux, run `python3 scripts/install_hooks.py` (Windows alternative: `pwsh -File scripts/install-hooks.ps1`).

## Expectations

- Work from a topic branch off `develop` unless maintainers direct otherwise
- Keep changes scoped and reviewable
- Update docs when behavior, build flow, or contributor workflow changes
- Add a short item to the `Unreleased` section of [CHANGELOG.md](CHANGELOG.md) for notable changes
- Run the relevant local check for your change:
  - `ctest --test-dir build --output-on-failure` for code changes
  - `scripts/docs-check.sh` for docs or Markdown changes

## If you are not on the core team

Outside pull requests are welcome, and they are judged on the code: if it passes review and CI, it can ship.

- **The decision citation is ours to fill.** The pull request template asks for a decision citation (`Objective`, `Decision`, `Kind`). It points at a private record you cannot see, so leave the placeholders as they are; a maintainer completes the block before merging. On a fork, CI only warns about it.
- **Say how the code was made.** In the pull request description, list the tools that wrote it (code written with an AI agent is fine; say so) and any code copied from elsewhere, with its license. Code under a license that cannot be combined with ASSAL (GPL, AGPL and similar) cannot be accepted. You must have the right to submit everything in the pull request (LICENSE §5.2).
- **Bring real test files.** If the change reads files, include small real examples in the tests, not only synthetic ones.
- **Open an issue first for large features**, so nobody builds something that cannot land.

### Importers for other products' file formats

Importers that open projects or presets from other software are welcome, under these rules:

- **Clean room.** Work out the format from files you made yourself and from public descriptions, never from decompiled or disassembled vendor code.
- **Fail closed.** If the importer cannot read a file fully, it refuses with a clear message; it never half-imports in silence.
- **Name what does not come across.** Another product's own instruments and effects cannot open in Aestra. Bring notes, patterns, the arrangement and audio, and tell the user what was left behind.
- **Keep it self-contained**, so a format change upstream breaks the importer and nothing else.
- **No logos or endorsement.** Describe it plainly ("opens FL Studio projects"); do not use the other product's marks or suggest it endorses Aestra.

### Checks that surprise new contributors

- A new UI test must be listed in `AESTRA_REQUIRED_UI_TESTS` in `Tests/CMakeLists.txt` and in the CI workflow, or `UIRequiredTestListsGuard` fails.
- Some large source files are size-pinned and may only shrink; put new logic in a header or a new file.

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
