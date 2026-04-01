# Aestra

![License](https://img.shields.io/badge/License-ASSAL%20v1.1-blue)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey)
![C++](https://img.shields.io/badge/C%2B%2B-17-orange)

> A digital audio workstation under active development, built in modern C++ with a custom UI stack and a native audio engine.

![Aestra Interface](AestraDocs/images/aestra_daw_interface.png)

## Current Snapshot

As of March 2026, the repo is in active engineering mode rather than release-polish mode. The most reliable currently verified paths are:

- Internal built-in plugin discovery through the normal manager/factory path
- `Aestra Rumble` instantiation, state save/restore, and project round-trips
- Arsenal units holding real plugin instances rather than placeholders
- Headless Arsenal playback routing MIDI into Rumble and producing audible output
- Project and plugin persistence coverage in the confidence suite

For the most truthful current status, start with:

- [docs/technical/roadmap.md](docs/technical/roadmap.md)
- [docs/technical/testing_ci.md](docs/technical/testing_ci.md)
- [meta/CHANGELOGS/CHANGELOG_2026Q1.md](meta/CHANGELOGS/CHANGELOG_2026Q1.md)

## Repository Layout

- `AestraCore` - core utilities and shared infrastructure
- `AestraPlat` - platform abstraction
- `AestraAudio` - audio engine, models, playback, DSP, and plugin plumbing
- `AestraUI` - custom rendering/UI framework
- `Source` - main application sources
- `Tests` - confidence suite and regression coverage
- `docs` - contributor and technical documentation

## Build

### Windows

```powershell
git clone https://github.com/currentsuspect/Aestra.git
cd Aestra
pwsh -File scripts/install-hooks.ps1
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

Primary runtime targets are emitted under `build/bin` or generator-specific `build/bin/<Config>`.

### Linux

```bash
git clone https://github.com/currentsuspect/Aestra.git
cd Aestra
cmake -S . -B build -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

`Aestra_CORE_MODE` defaults to `ON` in the public repo when premium modules are absent. Keeping it explicit in commands makes intent clear for contributors and CI.

More detail:

- [docs/getting-started/building.md](docs/getting-started/building.md)
- [BUILD.md](BUILD.md)

## Important CMake Options

| Option | Meaning | Default |
| --- | --- | --- |
| `Aestra_CORE_MODE` | Build without premium/private modules and use public assets | `ON` in public-only checkouts |
| `AESTRA_ENABLE_UI` | Build the desktop UI application | `ON` |
| `AESTRA_HEADLESS_ONLY` | Disable UI targets and configure headless-only builds | `OFF` |
| `AESTRA_ENABLE_TESTS` | Build test executables under `Tests/` | `ON` |

## Testing

After building, run the confidence suite with:

```bash
ctest --test-dir build --output-on-failure
```

There is also a helper script:

```bash
./scripts/run-confidence-suite.sh
```

The exact tests and CI posture are documented in [docs/technical/testing_ci.md](docs/technical/testing_ci.md).

## Documentation

- Docs home: [docs/README.md](docs/README.md)
- Published docs: <https://currentsuspect.github.io/Aestra/>
- Contributor guide: [docs/developer/contributing.md](docs/developer/contributing.md)
- Build guide: [docs/getting-started/building.md](docs/getting-started/building.md)

## Contributing

Start here:

- [CONTRIBUTING.md](CONTRIBUTING.md)
- [docs/developer/contributing.md](docs/developer/contributing.md)

When you touch behavior, update the relevant docs and add a brief item to the `Unreleased` section in [CHANGELOG.md](CHANGELOG.md).

## License

Aestra is source-available under the Aestra Studios Source-Available License (ASSAL) v1.1.

- Full text: [LICENSE](LICENSE)
- Practical guidance: [LICENSING.md](LICENSING.md)
